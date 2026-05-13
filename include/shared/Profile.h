#ifndef PROFILE_H
#define PROFILE_H

#include <string>
#include <vector>
#include <windows.h>

struct Keybinds {
  UINT toggleMod = MOD_CONTROL;
  UINT toggleKey = 'U';
  UINT roiMod = MOD_CONTROL;
  UINT roiKey = 'R';
  UINT crossMod = 0;
  UINT crossKey = VK_F10;
  UINT zeroMod = MOD_CONTROL;
  UINT zeroKey = 'G';
};

struct CrosshairPreset {
  std::wstring name;
  float offsetX;
  float offsetY;
  float angle;
  float thickness;
  COLORREF color;
  bool pulse;
};

struct Profile {
  std::wstring name;
  double sensitivityX = 0.05;
  double sensitivityY = 0.05;

  // Reference Metadata
  float fov = 80.0f;
  int resolutionWidth = 1920;
  int resolutionHeight = 1080;
  float renderScale = 100.0f;

  // Detector Logic
  int roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;
  COLORREF target_color = 0;
  int tolerance = 5;
  float diveGlideMatch = 9.0f;
  int screenIndex = 0;

  // Crosshair Settings
  bool showCrosshair = false;
  float crossThickness = 2.0f;
  COLORREF crossColor = RGB(255, 0, 0);
  float crossOffsetX = 0.0f;
  float crossOffsetY = 0.0f;
  float crossAngle = 0.0f;
  bool crossPulse = false;

  Keybinds keybinds;
  std::vector<CrosshairPreset> crosshairPresets;

  bool Load(const std::wstring &path);
  bool Save(const std::wstring &path);
};

std::vector<Profile> GetProfiles(const std::wstring &directory);

#endif // PROFILE_H
