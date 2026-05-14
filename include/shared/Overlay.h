#ifndef OVERLAY_H
#define OVERLAY_H

#include <windows.h>
#include <gdiplus.h>
#include "shared/Direct2DRenderer.h"

void InitializeOverlay(HWND hwnd);
void CleanupOverlay();
void DrawOverlay(HWND hwnd, double angle, bool showCrosshair);
void DrawOverlayD2D(Direct2DRenderer& d2d, HWND hwnd, double angle, bool showCrosshair);

#endif // OVERLAY_H
