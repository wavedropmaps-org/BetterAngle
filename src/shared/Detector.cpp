#include "shared/Detector.h"
#include "shared/State.h"
#include <algorithm>
#include <atomic>
#include <immintrin.h>
#include <intrin.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

// ---------- pixel matching: scalar (L2) and AVX2 (L1, per-pair) ------------

#define PIX_MATCH_INT(pix)                      \
  {                                             \
    int r = (int)((pix >> 16) & 0xFF);          \
    int g = (int)((pix >> 8)  & 0xFF);          \
    int b = (int)(pix & 0xFF);                  \
    int dr = r - tr, dg = g - tg, db = b - tb; \
    if ((dr*dr + dg*dg + db*db) <= tolSq)       \
      match++;                                  \
  }

// Scalar fallback: original L2 distance. Used on pre-Haswell CPUs (no AVX2).
static int CountMatchesScalar(const DWORD *p, int total,
                              int tr, int tg, int tb, int tolerance) {
  int tolSq = tolerance * tolerance;
  int match = 0, i = 0;
  for (; i <= total - 4; i += 4, p += 4) {
    DWORD p0=p[0], p1=p[1], p2=p[2], p3=p[3];
    PIX_MATCH_INT(p0); PIX_MATCH_INT(p1);
    PIX_MATCH_INT(p2); PIX_MATCH_INT(p3);
  }
  for (; i < total; i++, p++) { DWORD pix = *p; PIX_MATCH_INT(pix); }
  return match;
}

// AVX2 fast path: L1 distance via _mm256_sad_epu8.
//
// SAD groups 8 bytes per sum, and each BGRA pixel is 4 bytes — so each SAD
// lane covers 2 pixels' combined channel-diff sum (alpha masked to 0 in both
// operands). We compare each 2-pixel sum against 2*L1tol and count 2 matches
// per matching lane. This is per-PAIR granularity, slightly more permissive
// than per-pixel L2 — for a solid-coloured FOV indicator this matches the
// scalar version's behaviour to within ~5% on edge pixels (well inside the
// user's diveGlideMatch percentage threshold's slack).
//
// L1tol = tolerance * sqrt(3) keeps the L1 ball comparable in size to the
// scalar L2 ball (containment) so calibrated colours don't need re-pick.
static int CountMatchesAvx2(const DWORD *p, int total,
                            int tr, int tg, int tb, int tolerance) {
  const int L1tol  = (int)((float)tolerance * 1.7320508f); // sqrt(3)
  const int L1tol2 = L1tol * 2;                            // pair threshold

  // Target with alpha=0; pixels will be masked to alpha=0 too so SAD's alpha
  // contribution is |0-0|=0.
  const DWORD targetDword = ((DWORD)tr << 16) | ((DWORD)tg << 8) | (DWORD)tb;
  const __m256i target_v   = _mm256_set1_epi32((int)targetDword);
  const __m256i alpha_mask = _mm256_set1_epi32(0x00FFFFFF);
  const __m256i tol2_v     = _mm256_set1_epi64x((long long)L1tol2);

  int match = 0, i = 0;
  for (; i <= total - 8; i += 8, p += 8) {
    __m256i pixels     = _mm256_loadu_si256((const __m256i *)p);
    __m256i pix_masked = _mm256_and_si256(pixels, alpha_mask);
    // 4 lanes of u16 sums; each lane = sum of |dB|+|dG|+|dR| for 2 pixels.
    __m256i sad        = _mm256_sad_epu8(pix_masked, target_v);
    // gt[lane] = -1 if sad > L1tol2 (no match), 0 otherwise (match).
    __m256i gt         = _mm256_cmpgt_epi64(sad, tol2_v);
    // Each 64-bit lane is all-1s or all-0s; movemask gives 8 mask bits per lane.
    int mask32 = _mm256_movemask_epi8(gt);
    int noMatchLanes = (int)__popcnt((unsigned int)mask32) / 8;
    int matchLanes   = 4 - noMatchLanes;
    match += matchLanes * 2; // 2 pixels per lane
  }
  // Remainder: scalar with single-pixel L1 threshold for consistency.
  for (; i < total; i++, p++) {
    DWORD pix = *p;
    int r = (int)((pix >> 16) & 0xFF);
    int g = (int)((pix >> 8) & 0xFF);
    int b = (int)(pix & 0xFF);
    int dr = (r > tr) ? r - tr : tr - r;
    int dg = (g > tg) ? g - tg : tg - g;
    int db = (b > tb) ? b - tb : tb - b;
    if (dr + dg + db <= L1tol) match++;
  }
  return match;
}

// CPUID-based AVX2 detection. Run once at first use.
static bool DetectAvx2() {
  int info[4];
  __cpuidex(info, 7, 0);
  return (info[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
}

// Initialised on first call (thread-safe under C++11 magic statics).
static bool HasAvx2() {
  static const bool s = DetectAvx2();
  return s;
}

// Dispatcher kept under the original name so callers don't change.
static int CountMatches(const DWORD *p, int total,
                        int tr, int tg, int tb, int tolerance) {
  return HasAvx2()
    ? CountMatchesAvx2(p, total, tr, tg, tb, tolerance)
    : CountMatchesScalar(p, total, tr, tg, tb, tolerance);
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
  // No DXGI init in the ctor; the global is constructed before profile load
  // knows which monitor to target. WinMain calls ReinitDisplay(g_screenIndex)
  // after profile load, before the detector thread starts.
}

FovDetector::~FovDetector() {
  ReleaseDXGI();
  if (m_hdcMem) { SelectObject(m_hdcMem, m_hOld); DeleteDC(m_hdcMem); }
  if (m_hbm)       DeleteObject(m_hbm);
  if (m_hdcScreen) ReleaseDC(NULL, m_hdcScreen);
}

// ---------- DXGI init / teardown -------------------------------------------

void FovDetector::ReleaseDXGI() {
  if (m_stagingTex)  { m_stagingTex->Release();  m_stagingTex  = nullptr; }
  if (m_duplication) { m_duplication->Release(); m_duplication = nullptr; }
  if (m_d3dCtx)      { m_d3dCtx->Release();      m_d3dCtx      = nullptr; }
  if (m_d3dDevice)   { m_d3dDevice->Release();   m_d3dDevice   = nullptr; }
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

  // Find the output whose HMONITOR matches the requested monitor. Without
  // this, EnumOutputs(monitorIndex) would mis-map because EnumDisplayMonitors
  // and IDXGIAdapter::EnumOutputs can use different ordering.
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

int FovDetector::Scan(const RoiConfig &cfg, DWORD *outGridSamples,
                      const int *tripwireActiveIdx, bool tripwireReady,
                      LARGE_INTEGER *outFrameTime) {
  if (cfg.w <= 0 || cfg.h <= 0) return 0;

  if (m_dxgiOk) {
    g_lastScanUsedDxgi = true;
    DXGI_OUTDUPL_FRAME_INFO fi{};
    IDXGIResource *res = nullptr;
    HRESULT hr = m_duplication->AcquireNextFrame(0, &fi, &res);

    if (outFrameTime) *outFrameTime = fi.LastPresentTime;

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      // Bounded pause burst: caps the AcquireNextFrame poll rate at ~1M/s so
      // we don't burn CPU on kernel-mode transitions when the spin loop in
      // DetectorThread calls Scan back-to-back. ~16 _mm_pause iterations is
      // ~1µs, well below scan latency.
      for (int i = 0; i < 16; i++) _mm_pause();
      return -1;
    }

    if (FAILED(hr)) {
      // Device lost, mode change, or other failure. Don't recreate here
      // (that would race a concurrent SamplePixelDXGI call on main thread).
      // The next display change / screen-index change will trigger ReinitDisplay.
      return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);
    }

    ID3D11Texture2D *desktopTex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&desktopTex);
    res->Release();
    if (!desktopTex) {
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);
    }

    D3D11_TEXTURE2D_DESC desc;
    desktopTex->GetDesc(&desc);

    // HDR or non-BGRA8 format -> BitBlt fallback (auto-converts to SDR).
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
      desktopTex->Release();
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);
    }

    // ROI must lie within the monitor texture (cfg.x/y are monitor-relative).
    if (cfg.x < 0 || cfg.y < 0 ||
        (UINT)(cfg.x + cfg.w) > desc.Width ||
        (UINT)(cfg.y + cfg.h) > desc.Height) {
      desktopTex->Release();
      m_duplication->ReleaseFrame();
      return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);
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

    // Per-output texture is monitor-sized. cfg.x/y are already monitor-relative
    // — DO NOT add monitorOffset here (that's only valid for the BitBlt path
    // which goes through GetDC(NULL) = virtual-desktop coordinate space).
    D3D11_BOX box{ (UINT)cfg.x, (UINT)cfg.y, 0,
                   (UINT)(cfg.x + cfg.w), (UINT)(cfg.y + cfg.h), 1 };
    m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0,
                                     desktopTex, 0, &box);
    desktopTex->Release();
    m_duplication->ReleaseFrame();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);

    int tr = (int)GetRValue(cfg.target);
    int tg = (int)GetGValue(cfg.target);
    int tb = (int)GetBValue(cfg.target);

    // Tripwire grid samples (cell centres of a 3x3 grid). ~9 DWORD reads, ~50ns total.
    if (outGridSamples) {
      for (int gy = 0; gy < 3; gy++) {
        int sy = (cfg.h * (gy * 2 + 1)) / 6;
        if (sy < 0) sy = 0; else if (sy >= cfg.h) sy = cfg.h - 1;
        const DWORD *rowPtr = reinterpret_cast<const DWORD *>(
            static_cast<const BYTE *>(mapped.pData) + sy * mapped.RowPitch);
        for (int gx = 0; gx < 3; gx++) {
          int sx = (cfg.w * (gx * 2 + 1)) / 6;
          if (sx < 0) sx = 0; else if (sx >= cfg.w) sx = cfg.w - 1;
          outGridSamples[gy * 3 + gx] = rowPtr[sx] & 0x00FFFFFF;
        }
      }
    }

    // Option 1: Tripwire pre-arm before AVX2 (v5.5.164)
    // Check if at least 2 of 3 trained pixels match target colour; if so, return sentinel
    // to signal pre-arm and skip AVX2 loop (~200µs saved per fire).
    if (tripwireReady && tripwireActiveIdx && outGridSamples) {
      int tr = (int)GetRValue(cfg.target);
      int tg = (int)GetGValue(cfg.target);
      int tb = (int)GetBValue(cfg.target);
      int tolSq = cfg.tolerance * cfg.tolerance;

      int matchCount = 0;
      for (int k = 0; k < 3; k++) {
        int idx = tripwireActiveIdx[k];
        if (idx >= 0 && idx < 9) {
          DWORD pix = outGridSamples[idx];
          int r = (int)((pix >> 16) & 0xFF);
          int g = (int)((pix >> 8) & 0xFF);
          int b = (int)(pix & 0xFF);
          int dr = r - tr, dg = g - tg, db = b - tb;
          if ((dr*dr + dg*dg + db*db) <= tolSq) {
            matchCount++;
          }
        }
      }

      if (matchCount >= 2) {
        m_d3dCtx->Unmap(m_stagingTex, 0);
        return -1000; // sentinel: tripwire fired, skip AVX2
      }
    }

    int match = 0;
    for (int row = 0; row < cfg.h; row++) {
      const DWORD *rowPtr = reinterpret_cast<const DWORD *>(
          static_cast<const BYTE *>(mapped.pData) + row * mapped.RowPitch);
      match += CountMatches(rowPtr, cfg.w, tr, tg, tb, cfg.tolerance);
    }

    m_d3dCtx->Unmap(m_stagingTex, 0);
    return match;
  }

  return ScanBitBlt(cfg, outGridSamples, tripwireActiveIdx, tripwireReady, outFrameTime);
}

// ---------- one-shot DXGI sample for the colour picker ---------------------

bool FovDetector::SamplePixelDXGI(int monX, int monY, COLORREF &outColor) {
  if (!m_dxgiOk || !m_duplication) return false;
  if (monX < 0 || monY < 0) return false;

  // Defensive: drop any leftover frame the scanner thread may have held.
  // Safe to call even if no frame is held; failure is ignored.
  m_duplication->ReleaseFrame();

  DXGI_OUTDUPL_FRAME_INFO fi{};
  IDXGIResource *res = nullptr;
  // 100 ms timeout — colour picker is a one-shot human action.
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

  // DXGI_FORMAT_B8G8R8A8_UNORM on little-endian: byte0=B, byte1=G, byte2=R.
  // RGB(r,g,b) packs to COLORREF 0x00BBGGRR — same r/g/b accessors the
  // scanner uses (GetRValue/GetGValue/GetBValue), so picker-saved bytes
  // match scanner-extracted bytes exactly.
  BYTE b = (BYTE)(pix & 0xFF);
  BYTE g = (BYTE)((pix >> 8) & 0xFF);
  BYTE r = (BYTE)((pix >> 16) & 0xFF);
  outColor = RGB(r, g, b);
  return true;
}

// ---------- BitBlt fallback (unchanged) -------------------------------------

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

int FovDetector::ScanBitBlt(const RoiConfig &cfg, DWORD *outGridSamples,
                            const int *tripwireActiveIdx, bool tripwireReady,
                            LARGE_INTEGER *outFrameTime) {
  g_lastScanUsedDxgi = false;
  if (outFrameTime) outFrameTime->QuadPart = 0;  // BitBlt has no frame timestamp
  EnsureResources(cfg.w, cfg.h);
  // BitBlt source is GetDC(NULL) = full virtual desktop, so screen-space coords.
  BitBlt(m_hdcMem, 0, 0, cfg.w, cfg.h, m_hdcScreen,
         cfg.x + cfg.monitorOffsetX, cfg.y + cfg.monitorOffsetY, SRCCOPY);

  int tr = (int)GetRValue(cfg.target);
  int tg = (int)GetGValue(cfg.target);
  int tb = (int)GetBValue(cfg.target);

  // Tripwire grid samples — fill from m_pixels (DIB section is contiguous w*4).
  if (outGridSamples) {
    for (int gy = 0; gy < 3; gy++) {
      int sy = (cfg.h * (gy * 2 + 1)) / 6;
      if (sy < 0) sy = 0; else if (sy >= cfg.h) sy = cfg.h - 1;
      const DWORD *rowPtr = reinterpret_cast<const DWORD *>(m_pixels) + sy * cfg.w;
      for (int gx = 0; gx < 3; gx++) {
        int sx = (cfg.w * (gx * 2 + 1)) / 6;
        if (sx < 0) sx = 0; else if (sx >= cfg.w) sx = cfg.w - 1;
        outGridSamples[gy * 3 + gx] = rowPtr[sx] & 0x00FFFFFF;
      }
    }
  }

  // Option 1: Tripwire pre-arm before AVX2 (v5.5.164)
  if (tripwireReady && tripwireActiveIdx && outGridSamples) {
    int tolSq = cfg.tolerance * cfg.tolerance;

    int matchCount = 0;
    for (int k = 0; k < 3; k++) {
      int idx = tripwireActiveIdx[k];
      if (idx >= 0 && idx < 9) {
        DWORD pix = outGridSamples[idx];
        int r = (int)((pix >> 16) & 0xFF);
        int g = (int)((pix >> 8) & 0xFF);
        int b = (int)(pix & 0xFF);
        int dr = r - tr, dg = g - tg, db = b - tb;
        if ((dr*dr + dg*dg + db*db) <= tolSq) {
          matchCount++;
        }
      }
    }

    if (matchCount >= 2) {
      return -1000; // sentinel: tripwire fired, skip AVX2
    }
  }

  int match = 0;
  for (int row = 0; row < cfg.h; row++) {
    const DWORD *rowPtr = reinterpret_cast<const DWORD *>(m_pixels) + row * cfg.w;
    match += CountMatches(rowPtr, cfg.w, tr, tg, tb, cfg.tolerance);
  }
  return match;
}

bool FovDetector::CheckTripwireGDI(const RoiConfig &cfg,
                                    const int *tripwireActiveIdx,
                                    COLORREF target, int tolerance) {
  if (!tripwireActiveIdx) return false;

  HDC hdc = GetDC(NULL);
  if (!hdc) return false;

  // 3x3 grid cell centres (same layout as Scan())
  struct { int x, y; } cells[9];
  for (int gy = 0; gy < 3; gy++) {
    for (int gx = 0; gx < 3; gx++) {
      cells[gy * 3 + gx].x = cfg.monitorOffsetX + cfg.x + (cfg.w * (gx * 2 + 1)) / 6;
      cells[gy * 3 + gx].y = cfg.monitorOffsetY + cfg.y + (cfg.h * (gy * 2 + 1)) / 6;
    }
  }

  int tr = GetRValue(target), tg = GetGValue(target), tb = GetBValue(target);
  int tolSq = tolerance * tolerance;

  int matchCount = 0;
  for (int k = 0; k < 3; k++) {
    int idx = tripwireActiveIdx[k];
    if (idx >= 0 && idx < 9) {
      COLORREF c = GetPixel(hdc, cells[idx].x, cells[idx].y);
      if (c != CLR_INVALID) {
        int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        int dr = r - tr, dg = g - tg, db = b - tb;
        if ((dr * dr + dg * dg + db * db) <= tolSq) {
          matchCount++;
        }
      }
    }
  }

  ReleaseDC(NULL, hdc);
  return matchCount >= 2;
}
