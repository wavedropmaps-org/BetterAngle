#ifndef DETECTOR_H
#define DETECTOR_H

#include <windows.h>

struct RoiConfig {
  int x, y, w, h;
  COLORREF target;
  int tolerance;
  int monitorOffsetX = 0; // Screen-space offset; cfg.x/y are monitor-relative
  int monitorOffsetY = 0;
};

class FovDetector {
public:
  FovDetector();
  ~FovDetector();
  int Scan(const RoiConfig &cfg);

  // Kept as a no-op for callers that previously triggered DXGI re-init on
  // monitor switch. BitBlt path queries GetDC(NULL) which always reflects the
  // current virtual desktop, so no per-monitor re-init is needed.
  void ReinitDisplay(int monitorIndex);

private:
  HDC     m_hdcScreen = NULL;
  HDC     m_hdcMem    = NULL;
  HBITMAP m_hbm       = NULL;
  HGDIOBJ m_hOld      = NULL;
  int     m_curW = 0, m_curH = 0;
  void   *m_pixels    = nullptr;

  void EnsureScreenDC();
  void EnsureResources(int w, int h);
  int  ScanBitBlt(const RoiConfig &cfg);
};

#endif // DETECTOR_H
