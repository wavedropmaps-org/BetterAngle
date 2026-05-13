#ifndef DETECTOR_H
#define DETECTOR_H

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

struct RoiConfig {
  int x, y, w, h;
  COLORREF target;
  int tolerance;
  int monitorOffsetX = 0; // Screen-space offset; only used by BitBlt fallback
  int monitorOffsetY = 0;
};

class FovDetector {
public:
  FovDetector();
  ~FovDetector();
  int Scan(const RoiConfig &cfg);

  // Reinit the DXGI duplication for the given monitor index. Strict — if the
  // monitor's output isn't reachable from this adapter, m_dxgiOk stays false
  // and Scan/SamplePixelDXGI fall back to BitBlt. Must be called from the
  // detector thread (or before that thread starts) to avoid races on
  // m_duplication.
  void ReinitDisplay(int monitorIndex);

  // One-shot DXGI sample at a monitor-relative pixel. Used by the colour
  // picker so the saved COLORREF is the exact byte the scanner sees (avoids
  // GDI/DXGI byte drift). Returns false if DXGI can't satisfy the request —
  // caller should fall back to BitBlt.
  bool SamplePixelDXGI(int monX, int monY, COLORREF &outColor);

private:
  // DXGI path
  ID3D11Device           *m_d3dDevice   = nullptr;
  ID3D11DeviceContext    *m_d3dCtx      = nullptr;
  IDXGIOutputDuplication *m_duplication = nullptr;
  ID3D11Texture2D        *m_stagingTex  = nullptr;
  int  m_stagingW = 0, m_stagingH = 0;
  bool m_dxgiOk   = false;

  // BitBlt fallback path
  HDC     m_hdcScreen = NULL;
  HDC     m_hdcMem    = NULL;
  HBITMAP m_hbm       = NULL;
  HGDIOBJ m_hOld      = NULL;
  int     m_curW = 0, m_curH = 0;
  void   *m_pixels    = nullptr;

  void ReleaseDXGI();
  void EnsureScreenDC();
  void EnsureResources(int w, int h);
  int  ScanBitBlt(const RoiConfig &cfg);
};

#endif // DETECTOR_H
