// Overlay.cpp - BetterAngle Pro
// All comments use ASCII only to avoid encoding issues across contributors.
#include "shared/Input.h"
#include "shared/Logic.h"
#include "shared/State.h"
#include <gdiplus.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tlhelp32.h>
#include <windows.h>

using namespace Gdiplus;

void AddRoundedRect(GraphicsPath &path, int x, int y, int width, int height,
                    int radius) {
  int d = radius * 2;
  path.AddArc(x, y, d, d, 180, 90);
  path.AddArc(x + width - d, y, d, d, 270, 90);
  path.AddArc(x + width - d, y + height - d, d, d, 0, 90);
  path.AddArc(x, y + height - d, d, d, 90, 90);
  path.CloseFigure();
}

// Helper: format float to N decimal places (stack-based, no stream allocation)
static std::wstring FmtFloat(double v, int decimals = 2) {
  wchar_t buf[32];
  swprintf_s(buf, L"%.*f", decimals, v);
  return buf;
}

// Static FPS tracking
static ULONGLONG s_lastFrameTime = 0;
static float s_fps = 0.0f;
static int s_frameCount = 0;
static ULONGLONG s_fpsTimer = 0;

static void TickFPS() {
  ULONGLONG now = GetTickCount64();
  s_frameCount++;
  if (now - s_fpsTimer >= 500) {
    s_fps = s_frameCount * 1000.0f / float(now - s_fpsTimer);
    s_frameCount = 0;
    s_fpsTimer = now;
  }
}

static bool CheckFortniteProcessFast() {
  static bool lastRunning = false;
  static ULONGLONG lastCheck = 0;
  ULONGLONG now = GetTickCount64();
  if (now - lastCheck < 500)
    return lastRunning;
  lastCheck = now;
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return lastRunning;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  bool found = false;
  if (Process32FirstW(hSnap, &pe)) {
    do {
      if (pe.szExeFile[0] &&
          (_wcsnicmp(pe.szExeFile, L"FortniteClient-Win64-Shipping", 29) == 0 ||
           _wcsnicmp(pe.szExeFile, L"FortniteLauncher", 16) == 0 ||
           _wcsnicmp(pe.szExeFile, L"FortniteClient", 14) == 0)) {
        found = true;
        break;
      }
    } while (Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  lastRunning = found;
  return found;
}

void DrawOverlay(HWND hwnd, double angle, bool showCrosshair) {
  TickFPS();

  // Static cached GDI+ objects (initialized once, reused every frame)
  static FontFamily *s_ff = nullptr;
  static Font *s_labelFont = nullptr;
  static Font *s_angleFont = nullptr;
  static Font *s_subFont = nullptr;
  static Font *s_tinyFont = nullptr;
  static Font *s_roiFont = nullptr;
  static Font *s_selFont = nullptr;
  static Font *s_selSubFont = nullptr;
  static StringFormat *s_fmtCenter = nullptr;
  static StringFormat *s_fmtAngle = nullptr;
  static SolidBrush *s_labelBrush = nullptr;
  static SolidBrush *s_matchLabelBrush = nullptr;
  static SolidBrush *s_barBgBrush = nullptr;
  static SolidBrush *s_whiteBrush = nullptr;
  static Pen *s_barPen = nullptr;
  static Pen *s_swatchPen = nullptr;
  static Font *s_dbgFont = nullptr;

  if (!s_ff) {
    s_ff = new FontFamily(L"Segoe UI");
    s_labelFont = new Font(s_ff, 9, FontStyleBold, UnitPixel);
    s_angleFont = new Font(s_ff, 68, FontStyleBold, UnitPixel);
    s_subFont = new Font(s_ff, 12, FontStyleBold, UnitPixel);
    s_tinyFont = new Font(s_ff, 9, FontStyleRegular, UnitPixel);
    s_roiFont = new Font(s_ff, 10, FontStyleBold, UnitPixel);
    s_selFont = new Font(s_ff, 28, FontStyleBold, UnitPixel);
    s_selSubFont = new Font(s_ff, 15, FontStyleRegular, UnitPixel);
    s_dbgFont = new Font(s_ff, 10, FontStyleRegular, UnitPixel);
    s_fmtCenter = new StringFormat();
    s_fmtCenter->SetAlignment(StringAlignmentCenter);
    s_fmtAngle = new StringFormat();
    s_fmtAngle->SetAlignment(StringAlignmentCenter);
    s_fmtAngle->SetLineAlignment(StringAlignmentNear);
    s_labelBrush = new SolidBrush(Color(160, 180, 185, 195));
    s_matchLabelBrush = new SolidBrush(Color(200, 160, 170, 185));
    s_barBgBrush = new SolidBrush(Color(60, 255, 255, 255));
    s_whiteBrush = new SolidBrush(Color(255, 255, 255, 255));
    s_barPen = new Pen(Color(40, 255, 255, 255), 1.0f);
    s_swatchPen = new Pen(Color(100, 220, 220, 220), 1.0f);
  }

  RECT rect;
  GetClientRect(hwnd, &rect);
  int sw = rect.right - rect.left;
  int sh = rect.bottom - rect.top;
  if (sw <= 0 || sh <= 0)
    return;

  static HDC hdcMem = NULL;
  static HBITMAP hbmMem = NULL;
  static void *pBits = NULL;
  static int cachedW = 0, cachedH = 0;

  HDC hdcScreen = GetDC(NULL);
  if (!hdcMem)
    hdcMem = CreateCompatibleDC(hdcScreen);

  if (sw != cachedW || sh != cachedH) {
    if (hbmMem)
      DeleteObject(hbmMem);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sw;
    bmi.bmiHeader.biHeight = -sh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbmMem = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    cachedW = sw;
    cachedH = sh;
  }
  HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

  if (!pBits) {
    ReleaseDC(NULL, hdcScreen);
    return;
  }

  // Pre-fill selection background
  if (g_screenSnapshot && g_currentSelection != NONE) {
    HDC hdcSnap = CreateCompatibleDC(hdcMem);
    HGDIOBJ hOldSnap = SelectObject(hdcSnap, g_screenSnapshot);
    // The snapshot is the FULL virtual desktop. Blit the slice corresponding
    // to the configured monitor — without this offset, secondary monitors
    // would render the primary monitor's content (Discord/browser etc.).
    RECT mRect = GetMonitorRectByIndex(g_screenIndex);
    int virX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    BitBlt(hdcMem, 0, 0, sw, sh, hdcSnap,
           mRect.left - virX, mRect.top - virY, SRCCOPY);
    SelectObject(hdcSnap, hOldSnap);
    DeleteDC(hdcSnap);

    // Fast Alpha Fix
    DWORD *pixels = (DWORD *)pBits;
    int count = sw * sh;
    for (int i = 0; i < count; ++i)
      pixels[i] |= 0xFF000000;
  } else {
    memset(pBits, 0, sw * sh * 4);
  }

  Bitmap bmp(sw, sh, sw * 4, PixelFormat32bppPARGB, (BYTE *)pBits);
  Graphics graphics(&bmp);
  graphics.SetSmoothingMode(SmoothingModeHighQuality);
  graphics.SetInterpolationMode(InterpolationModeHighQuality);
  // Use PixelOffsetModeHalf for precise sub-pixel centering of lines.
  // This is often more reliable than PixelOffsetModeHighQuality for very thin
  // lines.
  graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
  graphics.SetCompositingQuality(CompositingQualityHighQuality);
  graphics.SetCompositingMode(CompositingModeSourceOver);
  graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

  // Two-stage selection overlay - show when Fortnite is focused OR selection is
  // active
  if (g_currentSelection != NONE &&
      (IsFortniteForeground() || g_currentSelection != NONE)) {
    SolidBrush dimBrush(Color(120, 0, 0, 0));
    graphics.FillRectangle(&dimBrush, 0, 0, sw, sh);

    SolidBrush dimWhite(Color(180, 220, 220, 220));

    // For ROI/Selection drawing, we need to map from Screen to Client
    // Since our window is exactly at mRect.left/top, we subtract those.
    RECT mRect = GetMonitorRectByIndex(g_screenIndex);
    int ox = mRect.left;
    int oy = mRect.top;

    if (g_currentSelection == SELECTING_ROI) {
      graphics.DrawString(L"STAGE 1  \xB7  Drag to select the dive prompt area",
                          -1, s_selFont, PointF(50.0f, 42.0f), s_whiteBrush);
      graphics.DrawString(L"Press the hotkey again to cancel", -1, s_selSubFont,
                          PointF(52.0f, 80.0f), &dimWhite);

      if (g_selectionRect.right > g_selectionRect.left) {
        Pen dashPen(Color(200, 255, 255, 255), 1.5f);
        REAL dash[] = {6.0f, 4.0f};
        dashPen.SetDashPattern(dash, 2);
        graphics.DrawRectangle(
            &dashPen, (int)(g_selectionRect.left - ox),
            (int)(g_selectionRect.top - oy),
            (int)(g_selectionRect.right - g_selectionRect.left),
            (int)(g_selectionRect.bottom - g_selectionRect.top));
      }

    } else if (g_currentSelection == SELECTING_COLOR) {
      graphics.DrawString(L"STAGE 2  \xB7  Click to pick the prompt colour", -1,
                          s_selFont, PointF(50.0f, 42.0f), s_whiteBrush);
      graphics.DrawString(L"Hover over the brightest part of the prompt text",
                          -1, s_selSubFont, PointF(52.0f, 80.0f), &dimWhite);

      // Draw the selected ROI rectangle
      if (g_selectionRect.right > g_selectionRect.left) {
        Pen dashPen(Color(200, 255, 255, 255), 1.5f);
        REAL dash[] = {6.0f, 4.0f};
        dashPen.SetDashPattern(dash, 2);
        graphics.DrawRectangle(
            &dashPen, (int)(g_selectionRect.left - ox),
            (int)(g_selectionRect.top - oy),
            (int)(g_selectionRect.right - g_selectionRect.left),
            (int)(g_selectionRect.bottom - g_selectionRect.top));
      }

      // Live magnifier
      POINT curScr;
      GetCursorPos(&curScr);
      POINT cur = curScr;
      ScreenToClient(hwnd, &cur);

      int mx = curScr.x - 40, my = curScr.y - 40, mw = 80, mh = 80;
      HDC hdcScr = GetDC(NULL);
      HDC hdcZoom = CreateCompatibleDC(hdcMem);
      HBITMAP hbmZoom = CreateCompatibleBitmap(hdcMem, mw * 3, mh * 3);
      SelectObject(hdcZoom, hbmZoom);
      StretchBlt(hdcZoom, 0, 0, mw * 3, mh * 3, hdcScr, mx, my, mw, mh,
                 SRCCOPY);

      // Position magnifier relative to client cursor
      int zx =
          (cur.x + 20 + mw * 3 < sw) ? (cur.x + 20) : (cur.x - mw * 3 - 20);
      int zy = (cur.y + mh * 3 < sh) ? cur.y : (sh - mh * 3);

      // Draw magnifier content and border
      Bitmap zoomBmp(hbmZoom, NULL);
      graphics.DrawImage(&zoomBmp, zx, zy, mw * 3, mh * 3);
      Pen magBorder(Color(255, 255, 255, 255), 2.0f);
      graphics.DrawRectangle(&magBorder, zx, zy, mw * 3, mh * 3);

      // Magnifier center crosshair
      Pen magCross(Color(180, 255, 0, 0), 1.0f);
      graphics.DrawLine(&magCross, zx + (mw * 3 / 2), zy, zx + (mw * 3 / 2),
                        zy + (mh * 3));
      graphics.DrawLine(&magCross, zx, zy + (mh * 3 / 2), zx + (mw * 3),
                        zy + (mh * 3 / 2));

      // Precision dot removed

      DeleteObject(hbmZoom);
      DeleteDC(hdcZoom);
      ReleaseDC(NULL, hdcScr);
    }

    goto render_done;
  }

  {
    // ROI box visualizer — only when user has configured an ROI
    if (g_showROIBox && !g_allProfiles.empty()) {
      auto &p = g_allProfiles[g_selectedProfileIdx];
      if (p.roi_w > 0 && p.roi_h > 0) {
        // Purple during focus-steal suspend, red when diving, green when
        // gliding
        bool suspended = (g_mouseSuspendedUntil > 0 &&
                          GetTickCount64() < g_mouseSuspendedUntil);
        Color roiCol = suspended    ? Color(200, 180, 60, 220) // purple
                       : g_isDiving ? Color(200, 255, 60, 60)  // red
                                    : Color(200, 60, 220, 80); // green
        Pen roiPen(roiCol, 2.0f);
        REAL dash[] = {8.0f, 4.0f};
        roiPen.SetDashPattern(dash, 2);
        // roi_x/y in the profile are ALREADY local to the monitor's top-left
        graphics.DrawRectangle(&roiPen, p.roi_x, p.roi_y, p.roi_w, p.roi_h);

        std::wstring stateLabel = suspended    ? L"LOCKING"
                                  : g_isDiving ? L"DIVING"
                                               : L"GLIDING";
        std::wstring label = stateLabel;

        SolidBrush roiLabelBrush(roiCol);
        graphics.DrawString(label.c_str(), -1, s_roiFont,
                            PointF(float(p.roi_x + 4), float(p.roi_y + 4)),
                            &roiLabelBrush);
      }
    }

    // Crosshair
    if (showCrosshair) {
      // Center crosshair dynamically on the Fortnite game window if active
      float cx, cy;
      if (g_fortniteWindow && g_fortniteRect.right > g_fortniteRect.left) {
        // Find physical center of the active game client in screen space
        float gameCenterX = g_fortniteRect.left + (g_fortniteRect.right - g_fortniteRect.left) * 0.5f;
        float gameCenterY = g_fortniteRect.top + (g_fortniteRect.bottom - g_fortniteRect.top) * 0.5f;
        
        // HUD starts at mRect.left, mRect.top. Map screen space to HUD local space
        RECT mRect = GetMonitorRectByIndex(g_screenIndex);
        cx = gameCenterX - mRect.left + g_crossOffsetX;
        cy = gameCenterY - mRect.top + g_crossOffsetY;
      } else {
        // Fallback: Map the monitor's center to the HUD's client coordinate space
        cx = (float)sw * 0.5f + g_crossOffsetX;
        cy = (float)sh * 0.5f + g_crossOffsetY;
      }

      // Make crosshair massive like the Java reference
      float hw = (sw > sh ? sw : sh) * 3.0f;
      float hh = hw;

      float pulse = 1.0f;
      if (g_crossPulse) {
        ULONGLONG t = GetTickCount64();
        ULONGLONG period = 3000; // 3 second total cycle
        ULONGLONG modT = t % period;

        if (modT < 1200) {
          // Phase 1: Fade Down (1.2s)
          pulse = 1.0f - (float)modT / 1200.0f;
        } else if (modT < 1500) {
          // Phase 2: Transparency Pause (0.3s)
          pulse = 0.0f;
        } else {
          // Phase 3: Slow Fade Up (1.5s)
          pulse = (float)(modT - 1500) / 1500.0f;
        }
      }

      BYTE alpha = BYTE(255 * pulse);

// Debug logging for crosshair thickness
#ifdef _DEBUG
      OutputDebugStringW((L"[Overlay] Drawing crosshair with thickness: " +
                          std::to_wstring(g_crossThickness) + L" (raw: " +
                          std::to_wstring(g_crossThickness) + L")\n")
                             .c_str());
#endif

      COLORREF cc = g_crossColor;
      float drawThickness = g_crossThickness;
      float finalAlpha = (float)alpha;

      // "Cool Shit" Trick: If thickness is sub-pixel (< 1.0), use a 1.0px pen
      // but scale the alpha to simulate the thinness. This prevents GDI+ from
      // clamping or dropping the line due to mathematical disappearing bugs
      // on certain resolutions/displays.
      if (drawThickness < 1.0f) {
        finalAlpha *= drawThickness;
        drawThickness = 1.0f;
      }

      Pen cPen(
          Color((BYTE)finalAlpha, GetRValue(cc), GetGValue(cc), GetBValue(cc)),
          drawThickness);
      cPen.SetAlignment(PenAlignmentCenter);
      // Set line join and cap for better thin line rendering
      cPen.SetLineJoin(LineJoinRound);
      cPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);

      Matrix rot;
      rot.RotateAt(g_crossAngle, PointF(cx, cy));
      graphics.SetTransform(&rot);

      // Use AntiAlias specifically for the crosshair
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);

      // Use a GraphicsPath for drawing the crosshair lines.
      // This can provide better sub-pixel precision in GDI+ than DrawLine.
      GraphicsPath crossPath;
      crossPath.AddLine(cx - hw, cy, cx + hw, cy);
      crossPath.StartFigure(); // New segment
      crossPath.AddLine(cx, cy - hh, cx, cy + hh);

      graphics.DrawPath(&cPen, &crossPath);
      graphics.ResetTransform();
      graphics.ResetClip();
    }

    // HUD box
    int rw = 260, rh = 150;
    int rx = g_hudX, ry = g_hudY;

    // Background gradient (More transparent for sleekness)
    LinearGradientBrush bgBrush(Point(rx, ry), Point(rx, ry + rh),
                                Color(150, 6, 8, 12), Color(150, 2, 3, 5));
    GraphicsPath path;
    AddRoundedRect(path, rx, ry, rw, rh, 8); // 8px rounded corners
    graphics.FillPath(&bgBrush, &path);

    // Border: white when diving, subtle when not
    Color borderCol =
        g_isDiving ? Color(200, 255, 255, 255) : Color(90, 50, 65, 80);
    Pen borderPen(borderCol, 1.5f);
    graphics.DrawPath(&borderPen, &path);

    // "CURRENT ANGLE" label
    graphics.DrawString(L"CURRENT ANGLE", -1, s_labelFont,
                        RectF(float(rx), float(ry + 8), float(rw), 18.0f),
                        s_fmtCenter, s_labelBrush);

    // Angle text — L"\xB0" is the degree symbol (safe ASCII escape)
    double dispAngle = std::abs(angle);
    double roundedAngle = std::round(dispAngle * 10.0) / 10.0;
    if (roundedAngle >= 360.0)
      roundedAngle -= 360.0;
    std::wstring angleStr = FmtFloat(roundedAngle, 1) + L"\xB0";
    Color angleCol =
        g_isDiving ? Color(255, 0, 220, 255) : Color(255, 0, 210, 140);
    SolidBrush angleBrush(angleCol);

    graphics.DrawString(angleStr.c_str(), -1, s_angleFont,
                        RectF(float(rx), float(ry + 26), float(rw), 80.0f),
                        s_fmtAngle, &angleBrush);

    // Match % label
    int matchCount = g_matchCount.load();
    int area = (g_allProfiles.empty())
                   ? 10000
                   : (g_allProfiles[g_selectedProfileIdx].roi_w *
                      g_allProfiles[g_selectedProfileIdx].roi_h);
    if (area <= 0)
      area = 1;
    float detectionRatio = (float)matchCount / area;
    int matchPct = int(detectionRatio * 100.0f);
    std::wstring matchStr = L"Match  " + std::to_wstring(matchPct) + L"%";
    graphics.DrawString(matchStr.c_str(), -1, s_subFont,
                        PointF(float(rx + 14), float(ry + rh - 54)),
                        s_matchLabelBrush);

    // Match progress bar
    int barX = rx + 14, barY = ry + rh - 38, barW = rw - 28, barH = 8;
    graphics.FillRectangle(s_barBgBrush, barX, barY, barW, barH);
    float clampedRatio = detectionRatio > 1.0f ? 1.0f : detectionRatio;
    int fillW = int(clampedRatio * barW);
    if (fillW > 0) {
      BYTE r = BYTE((1.0f - clampedRatio) * 255);
      BYTE g = BYTE(clampedRatio * 255);
      LinearGradientBrush barFill(Point(barX, barY), Point(barX + fillW, barY),
                                  Color(200, r, g, 40),
                                  Color(200, r / 2, g, 80));
      graphics.FillRectangle(&barFill, barX, barY, fillW, barH);
    }
    graphics.DrawRectangle(s_barPen, barX, barY, barW, barH);

    // Target colour swatch (top-right corner)
    int swatchX = rx + rw - 28, swatchY = ry + 8;
    Color swatch(255, GetBValue(g_targetColor), GetGValue(g_targetColor),
                 GetRValue(g_targetColor));
    SolidBrush swatchB(swatch);
    graphics.FillEllipse(&swatchB, swatchX, swatchY, 16, 16);
    graphics.DrawEllipse(s_swatchPen, swatchX, swatchY, 16, 16);

    // Drag hint
    SolidBrush tinyBrush(Color(g_isDraggingHUD ? 130 : 50, 200, 210, 220));
    graphics.DrawString(L":: drag", -1, s_tinyFont,
                        RectF(float(rx), float(ry + rh - 14), float(rw), 12.0f),
                        s_fmtCenter, &tinyBrush);

    // DEBUG Overlay Box
    if (g_showDebugOverlay && !g_allProfiles.empty()) {
      auto &dbgP = g_allProfiles[g_selectedProfileIdx];
      bool suspended = (g_mouseSuspendedUntil > 0 &&
                        GetTickCount64() < g_mouseSuspendedUntil);
      int dx = rx;
      int dy = ry + rh + 8;
      int dw = rw * 2; // Double width for columns (v5.5.17)
      int dh = 396;    // v5.5.99: + 3 rows for last-lock per-key snapshot

      LinearGradientBrush dbgBrush(Point(dx, dy), Point(dx, dy + dh),
                                   Color(175, 8, 10, 14), Color(175, 3, 5, 8));
      GraphicsPath dPath;
      AddRoundedRect(dPath, dx, dy, dw, dh, 6);
      graphics.FillPath(&dbgBrush, &dPath);

      Pen dBorder(Color(100, 0, 204, 153), 1.0f);
      graphics.DrawPath(&dBorder, &dPath);

      SolidBrush dbgTextL(Color(255, 160, 160, 160));

      auto DrawRow = [&](int row, int col, const wchar_t *label,
                         const std::wstring &val, bool isGood = true) {
        float xOff = float(dx + 10 + (col * (dw / 2)));
        float yPos = float(dy + 8 + (row * 16));
        graphics.DrawString(label, -1, s_dbgFont, PointF(xOff, yPos), &dbgTextL);
        SolidBrush valBrush(isGood ? Color(255, 0, 220, 170)
                                   : Color(255, 255, 80, 80));

        float xVal = xOff + (dw / 2) - 140;
        graphics.DrawString(val.c_str(), -1, s_dbgFont, PointF(xVal, yPos),
                            &valBrush);
      };

      bool fnRun = CheckFortniteProcessFast();
      bool fnFoc = g_fortniteFocusedCache.load();
      bool msHdd = !g_isCursorVisible.load();

      std::wstring suspStr = L"NO";
      if (suspended) {
        long long rem =
            (long long)g_mouseSuspendedUntil - (long long)GetTickCount64();
        suspStr = std::to_wstring(rem) + L" ms";
      }

      int matchCount = g_matchCount.load();
      int area = (g_allProfiles.empty()) ? 10000 : (dbgP.roi_w * dbgP.roi_h);
      if (area <= 0)
        area = 1;
      float detectionRatio = (float)matchCount / area;

      // Column 0: Engine & State
      DrawRow(0, 0, L"Engine FPS:", std::to_wstring((int)std::round(s_fps)));
      DrawRow(1, 0, L"Scanner Delay:",
              std::to_wstring((long long)g_detectionDelayMs) + L" ms",
              g_detectionDelayMs < 15);
      DrawRow(2, 0, L"Match Ratio:",
              std::to_wstring((int)(detectionRatio * 100)) + L"%");
      int peakCount = g_peakMatchCount.load();
      float peakRatio = (float)peakCount / area;
      DrawRow(3, 0, L"Peak Match (2s):",
              std::to_wstring((int)(peakRatio * 100)) + L"%",
              peakRatio < (dbgP.diveGlideMatch / 100.0f));
      DrawRow(4, 0, L"Threshold:",
              std::to_wstring((int)dbgP.diveGlideMatch) + L"%");
      DrawRow(5, 0, L"State:", g_isDiving ? L"DIVING" : L"GLIDING",
              !g_isDiving);
      DrawRow(6, 0, L"Input Locked:", suspended ? suspStr : L"NO", !suspended);


      DrawRow(8, 0, L"Fortnite Running:", fnRun ? L"YES" : L"NO", fnRun);
      DrawRow(9, 0, L"Fortnite Focused:", fnFoc ? L"YES" : L"NO", fnFoc);
      DrawRow(10, 0, L"Mouse Focus:", msHdd ? L"YES" : L"NO", msHdd);

      std::wstring roiStr = std::to_wstring(dbgP.roi_x) + L"," +
                            std::to_wstring(dbgP.roi_y) + L" " +
                            std::to_wstring(dbgP.roi_w) + L"x" +
                            std::to_wstring(dbgP.roi_h);
      DrawRow(11, 0, L"ROI:", roiStr);

      DrawRow(12, 0, L"Scanner CPU:",
              std::to_wstring(g_scannerCpuPct.load()) + L"%",
              g_scannerCpuPct.load() < 50);

      DrawRow(7, 1, L"Version:", L"v" + std::wstring(VERSION_WSTR), true);

      // Capture-path diagnostics (added v5.5.157). Helps verify DXGI is
      // actually active and the picker stored the right byte.
      bool dxgiActive = g_lastScanUsedDxgi.load();
      DrawRow(0, 1, L"Capture Path:",
              dxgiActive ? L"DXGI" : L"BitBlt", dxgiActive);

      int pickSrc = g_lastPickSource.load();
      const wchar_t *pickStr = (pickSrc == 1) ? L"DXGI"
                              : (pickSrc == 2) ? L"BitBlt"
                                               : L"-";
      DrawRow(1, 1, L"Pick Source:", pickStr, pickSrc == 1);

      COLORREF pc = g_pickedColor;
      std::wstring rgbStr = L"(" + std::to_wstring(GetRValue(pc)) + L"," +
                            std::to_wstring(GetGValue(pc)) + L"," +
                            std::to_wstring(GetBValue(pc)) + L")";
      DrawRow(2, 1, L"Picked RGB:", rgbStr);
    }
  }

render_done:
  POINT ptSrc = {0, 0};
  RECT wRect;
  GetWindowRect(hwnd, &wRect);
  POINT ptWin = {wRect.left, wRect.top};
  SIZE size = {sw, sh};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

  UpdateLayeredWindow(hwnd, hdcScreen, &ptWin, &size, hdcMem, &ptSrc, 0, &blend,
                      ULW_ALPHA);

  SelectObject(hdcMem, hOld);
  ReleaseDC(NULL, hdcScreen);
}
