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

// Auto-learned tripwire — see Plan v3 (v5.5.162). Three top-scoring grid
// candidates that consistently match target on FOV-rising-edges and stay
// quiet during idle. When all three match in a single frame, we pre-arm
// BlockInput without waiting for the full ROI scan to confirm.
struct TripwireSample {
  int x = 0, y = 0;     // ROI-relative pixel coords
  int hits = 0;         // # of FOV-rising-edges where this pixel matched target
  int noise = 0;        // # of idle frames where this pixel falsely matched target
  int idleSamples = 0;  // # of idle frames sampled (denominator for noise rate)
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

  // Tripwire learning state (persisted)
  std::vector<TripwireSample> tripwireCandidates;  // 9-entry 3x3 grid during learning
  int tripwireEvents = 0;
  bool tripwireReady = false;
  int tripwireActiveIdx[3] = {-1, -1, -1};
  // Snapshot of ROI/colour/tolerance at activation time. If any of these change
  // we drop the learned tripwire and re-learn (the learned pixels would otherwise
  // point at the wrong place / wrong colour).
  int tripwireSavedRoiX = 0, tripwireSavedRoiY = 0;
  int tripwireSavedRoiW = 0, tripwireSavedRoiH = 0;
  COLORREF tripwireSavedColor = 0;
  int tripwireSavedTolerance = 0;

  bool Load(const std::wstring &path);
  bool Save(const std::wstring &path);
};

std::vector<Profile> GetProfiles(const std::wstring &directory);

#endif // PROFILE_H
