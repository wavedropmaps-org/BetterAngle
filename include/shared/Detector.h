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
  int Scan(const RoiConfig &cfg, int earlyExitThreshold = 0);

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

  // Learn 3 tripwire pixel positions from the current DXGI frame: one matching
  // pixel per horizontal third of the ROI. Writes ROI-relative offsets into
  // rx[3] and ry[3]. Returns true if at least 2 thirds contained a match.
  // Call once after calibration completes (while DetectorThread is idle).
  bool LearnTripwire(const RoiConfig &cfg, int rx[3], int ry[3]);

  // Fast pre-arm check: reads the 3 learned pixels via 1×1 GPU copies and
  // returns true if 2-of-3 match the target colour. Acquires and releases its
  // own DXGI frame — do not call while Scan() is in flight on the same thread.
  bool CheckTripwireDXGI(const RoiConfig &cfg, const int rx[3], const int ry[3]);

private:
  // DXGI path
  ID3D11Device           *m_d3dDevice   = nullptr;
  ID3D11DeviceContext    *m_d3dCtx      = nullptr;
  IDXGIOutputDuplication *m_duplication = nullptr;
  ID3D11Texture2D        *m_stagingTex  = nullptr;
  ID3D11Texture2D        *m_twStagingTex = nullptr; // Reusable 1×1 texture for tripwire reads
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
  int  ScanBitBlt(const RoiConfig &cfg, int earlyExitThreshold = 0);
};

#endif // DETECTOR_H
