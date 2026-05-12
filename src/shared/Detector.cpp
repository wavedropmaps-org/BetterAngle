#include "shared/Detector.h"
#include "shared/State.h"
#include <algorithm>
#include <immintrin.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

// ---------- shared pixel matching ------------------------------------------

#define PIX_MATCH_INT(pix)                      \
  {                                             \
    int r = (int)((pix >> 16) & 0xFF);          \
    int g = (int)((pix >> 8)  & 0xFF);          \
    int b = (int)(pix & 0xFF);                  \
    int dr = r - tr, dg = g - tg, db = b - tb; \
    if ((dr*dr + dg*dg + db*db) <= tolSq)       \
      match++;                                  \
  }

static int CountMatches(const DWORD *p, int total,
                        int tr, int tg, int tb, int tolSq) {
  int match = 0, i = 0;
  for (; i <= total - 4; i += 4, p += 4) {
    DWORD p0=p[0], p1=p[1], p2=p[2], p3=p[3];
    PIX_MATCH_INT(p0); PIX_MATCH_INT(p1);
    PIX_MATCH_INT(p2); PIX_MATCH_INT(p3);
  }
  for (; i < total; i++, p++) { DWORD pix = *p; PIX_MATCH_INT(pix); }
  return match;
}

// Resolve a monitor index (in EnumDisplayMonitors order) to its HMONITOR.
static HMONITOR HMonitorForIndex(int index) {
  struct Data { int targetIndex; int currentIndex; HMONITOR result; };
  Data data{index, 0, NULL};
  EnumDisplayMonitors(NULL, NULL,
    [](HMONITOR h, HDC, LPRECT, LPARAM dwData) -> BOOL {
      auto *d = reinterpret_cast<Data *>(dwData);
      if (d->currentIndex == d->targetIndex) { d->result = h; return FALSE; }
      d->currentIndex++;
      return TRUE;
    },
    reinterpret_cast<LPARAM>(&data));
  return data.result;
}

// ---------- ctor / dtor ----------------------------------------------------

FovDetector::FovDetector() {
}

FovDetector::~FovDetector() {
  ReleaseDXGI();
  if (m_hdcMem) { SelectObject(m_hdcMem, m_hOld); DeleteDC(m_hdcMem); }
  if (m_hbm)       DeleteObject(m_hbm);
  if (m_hdcScreen) ReleaseDC(NULL, m_hdcScreen);
}

// ---------- DXGI init / teardown -------------------------------------------

void FovDetector::ReleaseDXGI() {
  if (m_twStagingTex) { m_twStagingTex->Release(); m_twStagingTex = nullptr; }
  if (m_stagingTex)   { m_stagingTex->Release();   m_stagingTex   = nullptr; }
  if (m_duplication)  { m_duplication->Release();  m_duplication  = nullptr; }
  if (m_d3dCtx)       { m_d3dCtx->Release();       m_d3dCtx       = nullptr; }
  if (m_d3dDevice)    { m_d3dDevice->Release();     m_d3dDevice    = nullptr; }
  m_dxgiOk = false;
  m_stagingW = 0;
  m_stagingH = 0;
}

void FovDetector::ReinitDisplay(int monitorIndex) {
  ReleaseDXGI();

  HMONITOR hMon = HMonitorForIndex(monitorIndex);
  if (!hMon) return;

  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                  0, nullptr, 0, D3D11_SDK_VERSION,
                                  &m_d3dDevice, &fl, &m_d3dCtx);
  if (FAILED(hr)) { ReleaseDXGI(); return; }

  IDXGIDevice *dxgiDevice = nullptr;
  m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice);
  if (!dxgiDevice) { ReleaseDXGI(); return; }

  IDXGIAdapter *adapter = nullptr;
  dxgiDevice->GetAdapter(&adapter);
  dxgiDevice->Release();
  if (!adapter) { ReleaseDXGI(); return; }

  IDXGIOutput *targetOutput = nullptr;
  for (UINT i = 0; ; i++) {
    IDXGIOutput *output = nullptr;
    if (FAILED(adapter->EnumOutputs(i, &output)) || !output) break;
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);
    if (desc.Monitor == hMon) {
      targetOutput = output;
      break;
    }
    output->Release();
  }
  adapter->Release();
  if (!targetOutput) { ReleaseDXGI(); return; }

  IDXGIOutput1 *output1 = nullptr;
  targetOutput->QueryInterface(__uuidof(IDXGIOutput1), (void **)&output1);
  targetOutput->Release();
  if (!output1) { ReleaseDXGI(); return; }

  hr = output1->DuplicateOutput(m_d3dDevice, &m_duplication);
  output1->Release();

  m_dxgiOk = SUCCEEDED(hr);
  if (!m_dxgiOk) ReleaseDXGI();
}

// ---------- main scan ------------------------------------------------------

int FovDetector::Scan(const RoiConfig &cfg, int earlyExitThreshold) {
  if (cfg.w <= 0 || cfg.h <= 0) return 0;

  if (m_dxgiOk && m_duplication) {
    g_lastScanUsedDxgi = true;
    DXGI_OUTDUPL_FRAME_INFO fi{};
    IDXGIResource *res = nullptr;
    HRESULT hr = m_duplication->AcquireNextFrame(0, &fi, &res);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      for (int i = 0; i < 16; i++) _mm_pause();
      return -1;
    }

    if (FAILED(hr)) {
      return ScanBitBlt(cfg, earlyExitThreshold);
    }

    ID3D11Texture2D *desktopTex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&desktopTex);
    res->Release();
    if (!desktopTex) {
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, earlyExitThreshold);
    }

    D3D11_TEXTURE2D_DESC desc;
    desktopTex->GetDesc(&desc);

    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
      desktopTex->Release();
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, earlyExitThreshold);
    }

    if (cfg.x < 0 || cfg.y < 0 ||
        (UINT)(cfg.x + cfg.w) > desc.Width ||
        (UINT)(cfg.y + cfg.h) > desc.Height) {
      desktopTex->Release();
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, earlyExitThreshold);
    }

    if (!m_stagingTex || m_stagingW != cfg.w || m_stagingH != cfg.h) {
      if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }
      D3D11_TEXTURE2D_DESC sd{};
      sd.Width            = (UINT)cfg.w;
      sd.Height           = (UINT)cfg.h;
      sd.MipLevels        = 1;
      sd.ArraySize        = 1;
      sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
      sd.SampleDesc.Count = 1;
      sd.Usage            = D3D11_USAGE_STAGING;
      sd.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
      m_d3dDevice->CreateTexture2D(&sd, nullptr, &m_stagingTex);
      m_stagingW = cfg.w;
      m_stagingH = cfg.h;
    }

    D3D11_BOX box{ (UINT)cfg.x, (UINT)cfg.y, 0,
                   (UINT)(cfg.x + cfg.w), (UINT)(cfg.y + cfg.h), 1 };
    m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0,
                                     desktopTex, 0, &box);
    desktopTex->Release();
    m_duplication->ReleaseFrame();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return ScanBitBlt(cfg, earlyExitThreshold);

    int tr = (int)GetRValue(cfg.target);
    int tg = (int)GetGValue(cfg.target);
    int tb = (int)GetBValue(cfg.target);
    int tolSq = cfg.tolerance * cfg.tolerance;

    int match = 0;
    for (int row = 0; row < cfg.h; row++) {
      const DWORD *rowPtr = reinterpret_cast<const DWORD *>(
          static_cast<const BYTE *>(mapped.pData) + row * mapped.RowPitch);
      match += CountMatches(rowPtr, cfg.w, tr, tg, tb, tolSq);
      
      if (earlyExitThreshold > 0 && match >= earlyExitThreshold) break;
    }

    m_d3dCtx->Unmap(m_stagingTex, 0);
    return match;
  }

  return ScanBitBlt(cfg, earlyExitThreshold);
}

bool FovDetector::SamplePixelDXGI(int monX, int monY, COLORREF &outColor) {
  if (!m_dxgiOk || !m_duplication) return false;
  if (monX < 0 || monY < 0) return false;

  m_duplication->ReleaseFrame();

  DXGI_OUTDUPL_FRAME_INFO fi{};
  IDXGIResource *res = nullptr;
  HRESULT hr = m_duplication->AcquireNextFrame(100, &fi, &res);
  if (FAILED(hr)) return false;

  ID3D11Texture2D *desktopTex = nullptr;
  res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&desktopTex);
  res->Release();
  if (!desktopTex) {
    m_duplication->ReleaseFrame();
    return false;
  }

  D3D11_TEXTURE2D_DESC desc;
  desktopTex->GetDesc(&desc);

  if ((desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
       desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
      (UINT)monX >= desc.Width || (UINT)monY >= desc.Height) {
    desktopTex->Release();
    m_duplication->ReleaseFrame();
    return false;
  }

  ID3D11Texture2D *staging = nullptr;
  D3D11_TEXTURE2D_DESC sd{};
  sd.Width            = 1;
  sd.Height           = 1;
  sd.MipLevels        = 1;
  sd.ArraySize        = 1;
  sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.Usage            = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
  if (FAILED(m_d3dDevice->CreateTexture2D(&sd, nullptr, &staging)) || !staging) {
    desktopTex->Release();
    m_duplication->ReleaseFrame();
    return false;
  }

  D3D11_BOX box{ (UINT)monX, (UINT)monY, 0,
                 (UINT)(monX + 1), (UINT)(monY + 1), 1 };
  m_d3dCtx->CopySubresourceRegion(staging, 0, 0, 0, 0, desktopTex, 0, &box);
  desktopTex->Release();
  m_duplication->ReleaseFrame();

  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = m_d3dCtx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    staging->Release();
    return false;
  }

  DWORD pix = *static_cast<const DWORD *>(mapped.pData);
  m_d3dCtx->Unmap(staging, 0);
  staging->Release();

  BYTE b = (BYTE)(pix & 0xFF);
  BYTE g = (BYTE)((pix >> 8) & 0xFF);
  BYTE r = (BYTE)((pix >> 16) & 0xFF);
  outColor = RGB(r, g, b);
  return true;
}

void FovDetector::EnsureScreenDC() {
  if (!m_hdcScreen) m_hdcScreen = GetDC(NULL);
}

void FovDetector::EnsureResources(int w, int h) {
  EnsureScreenDC();
  if (w == m_curW && h == m_curH && m_hdcMem) return;

  if (m_hdcMem) {
    SelectObject(m_hdcMem, m_hOld);
    DeleteDC(m_hdcMem);
    DeleteObject(m_hbm);
  }

  m_hdcMem = CreateCompatibleDC(m_hdcScreen);

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth       = w;
  bmi.bmiHeader.biHeight      = -h;
  bmi.bmiHeader.biPlanes      = 1;
  bmi.bmiHeader.biBitCount    = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  m_hbm  = CreateDIBSection(m_hdcScreen, &bmi, DIB_RGB_COLORS, &m_pixels, NULL, 0);
  m_hOld = SelectObject(m_hdcMem, m_hbm);
  m_curW = w;
  m_curH = h;
}

int FovDetector::ScanBitBlt(const RoiConfig &cfg, int earlyExitThreshold) {
  g_lastScanUsedDxgi = false;
  EnsureResources(cfg.w, cfg.h);
  BitBlt(m_hdcMem, 0, 0, cfg.w, cfg.h, m_hdcScreen,
         cfg.x + cfg.monitorOffsetX, cfg.y + cfg.monitorOffsetY, SRCCOPY);

  int tr = (int)GetRValue(cfg.target);
  int tg = (int)GetGValue(cfg.target);
  int tb = (int)GetBValue(cfg.target);
  int tolSq = cfg.tolerance * cfg.tolerance;

  int match = 0;
  for (int row = 0; row < cfg.h; row++) {
    const DWORD *rowPtr = reinterpret_cast<const DWORD *>(m_pixels) + row * cfg.w;
    match += CountMatches(rowPtr, cfg.w, tr, tg, tb, tolSq);
    
    if (earlyExitThreshold > 0 && match >= earlyExitThreshold) break;
  }
  return match;
}

// ---------- tripwire helpers -----------------------------------------------

// Lazily create the shared 1×1 staging texture used by both LearnTripwire and
// CheckTripwireDXGI. Returns false if creation fails.
static bool EnsureTripwireStaging(ID3D11Device *dev, ID3D11Texture2D **tex) {
  if (*tex) return true;
  D3D11_TEXTURE2D_DESC sd{};
  sd.Width = 1; sd.Height = 1; sd.MipLevels = 1; sd.ArraySize = 1;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  return SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, tex));
}

bool FovDetector::LearnTripwire(const RoiConfig &cfg, int rx[3], int ry[3]) {
  if (!m_dxgiOk || !m_duplication || cfg.w <= 0 || cfg.h <= 0) return false;

  DXGI_OUTDUPL_FRAME_INFO fi{};
  IDXGIResource *res = nullptr;
  HRESULT hr = m_duplication->AcquireNextFrame(100, &fi, &res);
  if (FAILED(hr)) return false;

  ID3D11Texture2D *desktopTex = nullptr;
  res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&desktopTex);
  res->Release();
  if (!desktopTex) { m_duplication->ReleaseFrame(); return false; }

  D3D11_TEXTURE2D_DESC desc;
  desktopTex->GetDesc(&desc);
  if ((desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
       desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
      cfg.x < 0 || cfg.y < 0 ||
      (UINT)(cfg.x + cfg.w) > desc.Width ||
      (UINT)(cfg.y + cfg.h) > desc.Height) {
    desktopTex->Release(); m_duplication->ReleaseFrame(); return false;
  }

  // Copy the full ROI into the shared staging texture (resize if needed)
  if (!m_stagingTex || m_stagingW != cfg.w || m_stagingH != cfg.h) {
    if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = (UINT)cfg.w; sd.Height = (UINT)cfg.h;
    sd.MipLevels = 1; sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(m_d3dDevice->CreateTexture2D(&sd, nullptr, &m_stagingTex))) {
      desktopTex->Release(); m_duplication->ReleaseFrame(); return false;
    }
    m_stagingW = cfg.w; m_stagingH = cfg.h;
  }

  D3D11_BOX box{ (UINT)cfg.x, (UINT)cfg.y, 0,
                 (UINT)(cfg.x + cfg.w), (UINT)(cfg.y + cfg.h), 1 };
  m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0, desktopTex, 0, &box);
  desktopTex->Release();
  m_duplication->ReleaseFrame();

  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) return false;

  int tr = GetRValue(cfg.target), tg = GetGValue(cfg.target), tb = GetBValue(cfg.target);
  int tolSq = cfg.tolerance * cfg.tolerance;
  int midY  = cfg.h / 2;
  int found = 0;

  // One representative matching pixel per horizontal third of the ROI.
  // Spiral outward from mid-height so we bias toward the center of the ROI.
  for (int k = 0; k < 3; k++) {
    int xStart = (cfg.w * k) / 3;
    int xEnd   = (cfg.w * (k + 1)) / 3;
    // Default fallback: geometric center of this third
    rx[k] = (xStart + xEnd) / 2;
    ry[k] = midY;
    for (int dy = 0; dy < cfg.h; dy++) {
      int yOff = (dy % 2 == 0) ? (midY + dy / 2) : (midY - (dy + 1) / 2);
      if (yOff < 0 || yOff >= cfg.h) continue;
      const DWORD *row = reinterpret_cast<const DWORD *>(
          static_cast<const BYTE *>(mapped.pData) + yOff * mapped.RowPitch);
      bool hit = false;
      for (int x = xStart; x < xEnd && !hit; x++) {
        DWORD pix = row[x];
        int r = (int)((pix >> 16) & 0xFF);
        int g = (int)((pix >> 8)  & 0xFF);
        int b = (int)(pix & 0xFF);
        int dr = r - tr, dg = g - tg, db = b - tb;
        if (dr * dr + dg * dg + db * db <= tolSq) {
          rx[k] = x; ry[k] = yOff; hit = true; found++;
        }
      }
      if (hit) break;
    }
  }

  m_d3dCtx->Unmap(m_stagingTex, 0);
  return found >= 2;
}

bool FovDetector::CheckTripwireDXGI(const RoiConfig &cfg,
                                     const int rx[3], const int ry[3]) {
  if (!m_dxgiOk || !m_duplication) return false;

  DXGI_OUTDUPL_FRAME_INFO fi{};
  IDXGIResource *res = nullptr;
  HRESULT hr = m_duplication->AcquireNextFrame(0, &fi, &res);
  if (FAILED(hr)) return false;

  ID3D11Texture2D *desktopTex = nullptr;
  res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&desktopTex);
  res->Release();
  if (!desktopTex) { m_duplication->ReleaseFrame(); return false; }

  D3D11_TEXTURE2D_DESC desc;
  desktopTex->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
      desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
    desktopTex->Release(); m_duplication->ReleaseFrame(); return false;
  }

  if (!EnsureTripwireStaging(m_d3dDevice, &m_twStagingTex)) {
    desktopTex->Release(); m_duplication->ReleaseFrame(); return false;
  }

  int tr = GetRValue(cfg.target), tg = GetGValue(cfg.target), tb = GetBValue(cfg.target);
  int tolSq = cfg.tolerance * cfg.tolerance;
  int matches = 0;

  for (int k = 0; k < 3; k++) {
    int px = cfg.x + rx[k];
    int py = cfg.y + ry[k];
    if (px < 0 || py < 0 || (UINT)px >= desc.Width || (UINT)py >= desc.Height)
      continue;
    D3D11_BOX box{ (UINT)px, (UINT)py, 0, (UINT)(px + 1), (UINT)(py + 1), 1 };
    m_d3dCtx->CopySubresourceRegion(m_twStagingTex, 0, 0, 0, 0, desktopTex, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_d3dCtx->Map(m_twStagingTex, 0, D3D11_MAP_READ, 0, &mapped))) {
      DWORD pix = *static_cast<const DWORD *>(mapped.pData);
      m_d3dCtx->Unmap(m_twStagingTex, 0);
      int r = (int)((pix >> 16) & 0xFF);
      int g = (int)((pix >> 8)  & 0xFF);
      int b = (int)(pix & 0xFF);
      int dr = r - tr, dg = g - tg, db = b - tb;
      if (dr * dr + dg * dg + db * db <= tolSq) matches++;
    }
  }

  desktopTex->Release();
  m_duplication->ReleaseFrame();
  return matches >= 2;
}
