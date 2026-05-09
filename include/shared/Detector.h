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
  // outGridSamples (optional, length 9): 3x3 grid of cell-centre BGRA pixels
  // sampled inside the ROI. Used by the tripwire learner. If null, skipped.
  // tripwireActiveIdx: if non-null, checks if all 3 pixels (at these indices)
  // match target colour; if so, returns -1000 (skip AVX2, pre-arm fires early).
  // outFrameTime (optional): filled with DXGI LastPresentTime if DXGI path succeeds;
  // set to {0} if BitBlt fallback. Used for retroactive angle correction.
  int Scan(const RoiConfig &cfg, DWORD *outGridSamples = nullptr,
           const int *tripwireActiveIdx = nullptr, bool tripwireReady = false,
           LARGE_INTEGER *outFrameTime = nullptr,
           int earlyExitThreshold = 0);

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

  // Sub-frame tripwire check via GetPixel(). Samples the 3 pixels at
  // tripwireActiveIdx positions using GDI. Returns true if all 3 match target
  // colour within tolerance. Called between DXGI frames to detect mid-frame FOV changes.
  bool CheckTripwireGDI(const RoiConfig &cfg,
                        const int *tripwireActiveIdx,
                        COLORREF target, int tolerance);

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
  int  ScanBitBlt(const RoiConfig &cfg, DWORD *outGridSamples = nullptr,
                  const int *tripwireActiveIdx = nullptr, bool tripwireReady = false,
                  LARGE_INTEGER *outFrameTime = nullptr,
                  int earlyExitThreshold = 0);
};

#endif // DETECTOR_H
