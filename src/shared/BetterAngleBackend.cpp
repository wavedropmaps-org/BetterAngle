#include "shared/BetterAngleBackend.h"
#include "shared/Detector.h"
#include "shared/Input.h"
#include "shared/Logic.h"
#include "shared/Profile.h"
#include "shared/State.h"
#include "shared/Updater.h"
#include <QGuiApplication>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <shlobj.h>
#include <sstream>
#include <thread>
#include <tlhelp32.h>
#include <windows.h>
#include <psapi.h>

extern std::vector<Profile> g_allProfiles;
extern int g_selectedProfileIdx;
extern AngleLogic g_logic;
extern HWND g_hHUD;
extern FovDetector g_detector;

static double g_pendingSetupSensX = 0.05;
static double g_pendingSetupSensY = 0.05;

struct MonitorData {
    QStringList names;
    int count = 0;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MonitorData* data = reinterpret_cast<MonitorData*>(dwData);
    MONITORINFOEXA mi;
    mi.cbSize = sizeof(MONITORINFOEXA);
    if (GetMonitorInfoA(hMonitor, &mi)) {
        int width = lprcMonitor->right - lprcMonitor->left;
        int height = lprcMonitor->bottom - lprcMonitor->top;
        QString name = QString("Monitor %1 (%2x%3)").arg(data->count + 1).arg(width).arg(height);
        data->names << name;
        data->count++;
    }
    return TRUE;
}

static BetterAngleBackend *s_backendInstance = nullptr;

void NotifyBackendCrosshairChanged() {
  if (s_backendInstance) {
    emit s_backendInstance->crosshairChanged();
  }
}

BetterAngleBackend::BetterAngleBackend(QObject *parent) : QObject(parent) {
  s_backendInstance = this;
  // Emit profileChanged once the Qt event loop starts so QML fields
  // refresh with whatever sensitivityX is in g_allProfiles (set by setup or
  // loaded from disk).
  QTimer::singleShot(0, this, [this]() {
    emit profileChanged();
    emit crosshairChanged();
    emit hotkeysChanged();
  });

  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this]() {
    static bool lastChecking = false;
    static bool lastDownloading = false;
    static bool lastComplete = false;
    static bool lastChecked = false;
    if (g_hasCheckedForUpdates != lastChecked) {
      lastChecked = g_hasCheckedForUpdates;
      emit updateStatusChanged();
    }

    if (g_isCheckingForUpdates != lastChecking) {
      lastChecking = g_isCheckingForUpdates;
      emit updateStatusChanged();
    }
    if (g_isDownloadingUpdate != lastDownloading) {
      lastDownloading = g_isDownloadingUpdate;
      emit updateStatusChanged();
    }
    if (g_downloadComplete != lastComplete) {
      lastComplete = g_downloadComplete;
      emit updateStatusChanged();
    }
  });
  timer->start(100);

  // Auto-check for updates on startup
  QTimer::singleShot(2000, this, [this]() {
    if (!g_hasCheckedForUpdates && !g_isCheckingForUpdates) {
      checkForUpdates();
    }
  });
}

double BetterAngleBackend::sensX() const {
  if (g_allProfiles.empty())
    return g_pendingSetupSensX;
  return g_allProfiles[g_selectedProfileIdx].sensitivityX;
}
void BetterAngleBackend::setSensX(double v) {
  double normalized = (std::max)(0.0001, std::round(v * 10.0) / 10.0);
  if (g_allProfiles.empty()) {
    g_pendingSetupSensX = normalized;
    emit profileChanged();
    return;
  }
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  p.sensitivityX = normalized;
  g_logic.LoadProfile(p.sensitivityX);
  p.Save(GetProfilesPath() + p.name + L".json");
  SaveSettings();
  emit profileChanged();
}

double BetterAngleBackend::sensY() const {
  if (g_allProfiles.empty())
    return g_pendingSetupSensY;
  return g_allProfiles[g_selectedProfileIdx].sensitivityY;
}
void BetterAngleBackend::setSensY(double v) {
  double normalized = (std::max)(0.0001, std::round(v * 10.0) / 10.0);
  if (g_allProfiles.empty()) {
    g_pendingSetupSensY = normalized;
    emit profileChanged();
    return;
  }
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  p.sensitivityY = normalized;
  p.Save(GetProfilesPath() + p.name + L".json");
  SaveSettings();
  emit profileChanged();
}

int BetterAngleBackend::tolerance() const {
  if (g_allProfiles.empty())
    return 2;
  return g_allProfiles[g_selectedProfileIdx].tolerance;
}
void BetterAngleBackend::setTolerance(int v) {
  if (g_allProfiles.empty())
    return;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  p.tolerance = v;
  p.Save(GetProfilesPath() + p.name + L".json");
  SaveSettings();
  emit profileChanged();
}

double BetterAngleBackend::diveGlideMatch() const {
  if (g_allProfiles.empty())
    return 9.0;
  return g_allProfiles[g_selectedProfileIdx].diveGlideMatch;
}
void BetterAngleBackend::setDiveGlideMatch(double v) {
  if (g_allProfiles.empty())
    return;
  if (v < 1.0)
    v = 1.0;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  p.diveGlideMatch = (float)v;
  p.Save(GetProfilesPath() + p.name + L".json");
  SaveSettings();
  emit profileChanged();
}

int BetterAngleBackend::screenIndex() const {
  if (g_allProfiles.empty())
    return g_screenIndex;
  return g_allProfiles[g_selectedProfileIdx].screenIndex;
}

void BetterAngleBackend::setScreenIndex(int v) {
  // Clamp to actual monitor count so invalid indices don't silently fall back
  int monitorCount = GetSystemMetrics(SM_CMONITORS);
  if (v >= monitorCount) v = monitorCount - 1;
  if (v < 0) v = 0;

  g_screenIndex = v;
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.screenIndex = v;
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();

  // Move the HUD window to the new monitor
  if (g_hHUD) {
    RECT mRect = GetMonitorRectByIndex(v);
    SetWindowPos(g_hHUD, HWND_TOPMOST, mRect.left, mRect.top,
                 mRect.right - mRect.left, mRect.bottom - mRect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_forceRedraw = true;
  }

  // Reinit DXGI desktop duplication on the correct monitor
  g_detector.ReinitDisplay(v);

  emit profileChanged();
}

QStringList BetterAngleBackend::availableScreens() const {
  MonitorData data;
  EnumDisplayMonitors(NULL, NULL, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&data));
  return data.names;
}

bool BetterAngleBackend::crosshairOn() const { return g_showCrosshair; }
void BetterAngleBackend::setCrosshairOn(bool v) {
  g_showCrosshair = v;
  g_forceRedraw = true;
  if (v) {
    g_crossOffsetX = 0.0f;
    g_crossOffsetY = 0.0f;
  }
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.showCrosshair = v;
    if (v) {
      p.crossOffsetX = 0.0f;
      p.crossOffsetY = 0.0f;
    }
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();
  if (v) Beep(750, 50);
  else Beep(500, 50);
  emit crosshairChanged();
}

float BetterAngleBackend::crossThickness() const { return g_crossThickness; }
void BetterAngleBackend::setCrossThickness(float v) {
  // Enforce integer values 1-10 (no decimals)
  v = std::round(v);

  // Clamp to valid range (1 to 10) to match UI slider
  const float minThickness = 1.0f;
  const float maxThickness = 10.0f;

  if (v < minThickness)
    v = minThickness;
  if (v > maxThickness)
    v = maxThickness;

  if (std::abs(g_crossThickness - v) > 0.001f) {
    g_crossThickness = v;
    g_forceRedraw = true;
    if (!g_allProfiles.empty()) {
      Profile &p = g_allProfiles[g_selectedProfileIdx];
      p.crossThickness = v;
      p.Save(GetProfilesPath() + p.name + L".json");
    }
    SaveSettings();
    emit crosshairChanged();
  }
}

float BetterAngleBackend::crossOffsetX() const { return g_crossOffsetX; }
void BetterAngleBackend::setCrossOffsetX(float v) {
  g_crossOffsetX = (std::round(v * 2.0f) / 2.0f);
  g_forceRedraw = true;
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossOffsetX = g_crossOffsetX;
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();
  emit crosshairChanged();
}

float BetterAngleBackend::crossOffsetY() const { return g_crossOffsetY; }
void BetterAngleBackend::setCrossOffsetY(float v) {
  g_crossOffsetY = (std::round(v * 2.0f) / 2.0f);
  g_forceRedraw = true;
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossOffsetY = g_crossOffsetY;
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();
  emit crosshairChanged();
}

bool BetterAngleBackend::crossPulse() const { return g_crossPulse; }
void BetterAngleBackend::setCrossPulse(bool v) {
  g_crossPulse = v;
  g_forceRedraw = true;
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossPulse = v;
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();
  emit crosshairChanged();
}

QColor BetterAngleBackend::crossColor() const {
  return QColor(GetRValue(g_crossColor), GetGValue(g_crossColor),
                GetBValue(g_crossColor));
}
void BetterAngleBackend::setCrossColor(const QColor &c) {
  g_crossColor = RGB(c.red(), c.green(), c.blue());
  g_forceRedraw = true;
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossColor = g_crossColor;
    p.Save(GetProfilesPath() + p.name + L".json");
  }
  SaveSettings();
  emit crosshairChanged();
}

QString BetterAngleBackend::versionStr() const {
  return QString::fromLatin1(VERSION_STR);
}
QString BetterAngleBackend::latestVersion() const {
  return QString::fromStdString(g_latestVersionOnline);
}
bool BetterAngleBackend::updateAvailable() const { return g_updateAvailable; }
bool BetterAngleBackend::isDownloading() const { return g_isDownloadingUpdate; }
bool BetterAngleBackend::isCheckingForUpdates() const {
  return g_isCheckingForUpdates;
}
bool BetterAngleBackend::downloadComplete() const { return g_downloadComplete; }
bool BetterAngleBackend::hasCheckedForUpdates() const {
  return g_hasCheckedForUpdates;
}
QString BetterAngleBackend::updateHistory() const {
  return QString::fromStdString(g_updateHistory);
}

QString BetterAngleBackend::updateStatus() const {
  if (g_isDownloadingUpdate)
    return "Downloading installer...";
  if (g_downloadComplete)
    return "Installer ready. Click to install update.";
  if (g_isCheckingForUpdates)
    return "Checking for updates...";
  if (g_hasCheckedForUpdates) {
    if (g_updateHistory.find("Downloaded update was invalid") !=
        std::string::npos)
      return "Downloaded update was invalid. Click to retry.";
    if (g_updateHistory.find("Update check failed") != std::string::npos)
      return "Update check failed";
    if (g_updateAvailable)
      return "New update available!";
    return "Application is up to date.";
  }
  return "";
}

void BetterAngleBackend::terminateApp() {
  SaveSettings();
  if (g_hHUD) {
    PostMessage(g_hHUD, WM_CLOSE, 0, 0);
  }
  // Also terminate the Qt loop immediately to ensure everything shuts down
  QCoreApplication::quit();
}
void BetterAngleBackend::checkForUpdates() {
  if (g_isCheckingForUpdates)
    return;

  g_isCheckingForUpdates = true;
  g_hasCheckedForUpdates = false;
  g_updateAvailable = false;
  emit updateStatusChanged();
  std::thread([]() { CheckForUpdates(); }).detach();
}

void BetterAngleBackend::downloadUpdate() {
  if (g_downloadComplete) {
    ApplyUpdateAndRestart();
    return;
  }

  g_updateHistory.clear();
  g_downloadComplete = false;
  emit updateStatusChanged();
  UpdateApp();
}

void BetterAngleBackend::requestShowControlPanel() {
  emit showControlPanelRequested();
}

QStringList BetterAngleBackend::crosshairPresetNames() const {
  QStringList list;
  if (g_allProfiles.empty())
    return list;
  for (const auto &cp : g_allProfiles[g_selectedProfileIdx].crosshairPresets) {
    list << QString::fromStdWString(cp.name) + QString(" (%1, %2)")
                                                   .arg(cp.offsetX, 0, 'f', 1)
                                                   .arg(cp.offsetY, 0, 'f', 1);
  }
  return list;
}

void BetterAngleBackend::saveCrosshairPreset(const QString &name) {
  if (g_allProfiles.empty())
    return;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  CrosshairPreset cp;
  cp.name = name.toStdWString();
  cp.offsetX = g_crossOffsetX;
  cp.offsetY = g_crossOffsetY;
  cp.angle = g_crossAngle;
  cp.thickness = g_crossThickness;
  cp.color = g_crossColor;
  cp.pulse = g_crossPulse;
  // Replace existing with same name, otherwise append
  for (auto &existing : p.crosshairPresets) {
    if (existing.name == cp.name) {
      existing = cp;
      p.Save(GetProfilesPath() + p.name + L".json");
      emit crosshairPresetsChanged();
      return;
    }
  }
  p.crosshairPresets.push_back(cp);
  p.Save(GetProfilesPath() + p.name + L".json");

  emit crosshairPresetsChanged();
}

void BetterAngleBackend::loadCrosshairPreset(int index) {
  if (g_allProfiles.empty())
    return;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  if (index < 0 || index >= (int)p.crosshairPresets.size())
    return;
  const auto &cp = p.crosshairPresets[index];
  g_crossOffsetX = cp.offsetX;
  g_crossOffsetY = cp.offsetY;
  g_crossAngle = cp.angle;
  g_crossThickness = cp.thickness;
  g_crossColor = cp.color;
  g_crossPulse = cp.pulse;

  p.crossOffsetX = cp.offsetX;
  p.crossOffsetY = cp.offsetY;
  p.crossAngle = cp.angle;
  p.crossThickness = cp.thickness;
  p.crossColor = cp.color;
  p.crossPulse = cp.pulse;

  p.Save(GetProfilesPath() + p.name + L".json");
  g_forceRedraw = true;
  SaveSettings();

  emit crosshairPresetsChanged();
  emit crosshairChanged();
}

void BetterAngleBackend::deleteCrosshairPreset(int index) {
  if (g_allProfiles.empty())
    return;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  if (index < 0 || index >= (int)p.crosshairPresets.size())
    return;
  p.crosshairPresets.erase(p.crosshairPresets.begin() + index);
  p.Save(GetProfilesPath() + p.name + L".json");
  emit crosshairPresetsChanged();
}

void BetterAngleBackend::resetCrosshairToDefaults() {
  // Reset global state to defaults
  g_showCrosshair = false;
  g_crossThickness = 1.0f;       // Reset to standard 1.0px default
  g_crossColor = RGB(255, 0, 0); // Red
  g_crossOffsetX = 0.0f;
  g_crossOffsetY = 0.0f;
  g_crossAngle = 0.0f;
  g_crossPulse = false;

  // Update current profile
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.showCrosshair = g_showCrosshair;
    p.crossThickness = g_crossThickness;
    p.crossColor = g_crossColor;
    p.crossOffsetX = g_crossOffsetX;
    p.crossOffsetY = g_crossOffsetY;
    p.crossAngle = g_crossAngle;
    p.crossPulse = g_crossPulse;

    // Save to disk
    p.Save(GetProfilesPath() + p.name + L".json");
  }

  SaveSettings();

  // Force redraw and notify UI
  g_forceRedraw = true;
  emit crosshairChanged();
}

// --- Hotkey Helpers ---
static UINT stringToVk(const QString &s) {
  QString upper = s.toUpper().trimmed();
  if (upper.isEmpty())
    return 0;

  // Single character keys
  if (upper.length() == 1) {
    QChar c = upper[0];
    if (c >= 'A' && c <= 'Z')
      return (UINT)c.unicode();
    if (c >= '0' && c <= '9')
      return (UINT)c.unicode();
  }

  // Function keys
  static const std::pair<const char *, UINT> keyMap[] = {
      {"F1", VK_F1},
      {"F2", VK_F2},
      {"F3", VK_F3},
      {"F4", VK_F4},
      {"F5", VK_F5},
      {"F6", VK_F6},
      {"F7", VK_F7},
      {"F8", VK_F8},
      {"F9", VK_F9},
      {"F10", VK_F10},
      {"F11", VK_F11},
      {"F12", VK_F12},
      {"F13", 0x7C},
      {"F14", 0x7D},
      {"F15", 0x7E},
      {"F16", 0x7F},
      {"F17", 0x80},
      {"F18", 0x81},
      {"F19", 0x82},
      {"F20", 0x83},
      {"F21", 0x84},
      {"F22", 0x85},
      {"F23", 0x86},
      {"F24", 0x87},
      {"TAB", VK_TAB},
      {"SPACE", VK_SPACE},
      {"ESC", VK_ESCAPE},
      {"ESCAPE", VK_ESCAPE},
      {"CTRL", VK_CONTROL},
      {"CONTROL", VK_CONTROL},
      {"SHIFT", VK_SHIFT},
      {"ALT", VK_MENU},
      {"MENU", VK_MENU},
      {"LWIN", VK_LWIN},
      {"RWIN", VK_RWIN},
      {"WIN", VK_LWIN},
      {"LCTRL", VK_LCONTROL},
      {"RCTRL", VK_RCONTROL},
      {"LSHIFT", VK_LSHIFT},
      {"RSHIFT", VK_RSHIFT},
      {"LALT", VK_LMENU},
      {"RALT", VK_RMENU},
      {"ENTER", VK_RETURN},
      {"RETURN", VK_RETURN},
      {"CAPSLOCK", VK_CAPITAL},
      {"CAPITAL", VK_CAPITAL},
      {"CAPS", VK_CAPITAL},
      {"INSERT", VK_INSERT},
      {"INS", VK_INSERT},
      {"DELETE", VK_DELETE},
      {"DEL", VK_DELETE},
      {"HOME", VK_HOME},
      {"END", VK_END},
      {"PAGEUP", VK_PRIOR},
      {"PGUP", VK_PRIOR},
      {"PAGEDOWN", VK_NEXT},
      {"PGDN", VK_NEXT},
      {"PRIOR", VK_PRIOR},
      {"NEXT", VK_NEXT},
      {"UP", VK_UP},
      {"DOWN", VK_DOWN},
      {"LEFT", VK_LEFT},
      {"RIGHT", VK_RIGHT},
      {"NUMLOCK", VK_NUMLOCK},
      {"SCROLLLOCK", VK_SCROLL},
      {"SCROLL", VK_SCROLL},
      {"PRINTSCREEN", VK_SNAPSHOT},
      {"PRTSC", VK_SNAPSHOT},
      {"SNAPSHOT", VK_SNAPSHOT},
      {"PAUSE", VK_PAUSE},
      {"BREAK", VK_PAUSE},
      // Multimedia keys
      {"VOLUME_MUTE", 0xAD},
      {"VOLUME_DOWN", 0xAE},
      {"VOLUME_UP", 0xAF},
      {"MEDIA_NEXT", 0xB0},
      {"MEDIA_PREV", 0xB1},
      {"MEDIA_STOP", 0xB2},
      {"MEDIA_PLAY_PAUSE", 0xB3},
      // Browser keys
      {"BROWSER_BACK", 0xA6},
      {"BROWSER_FORWARD", 0xA7},
      {"BROWSER_REFRESH", 0xA8},
      {"BROWSER_STOP", 0xA9},
      {"BROWSER_SEARCH", 0xAA},
      {"BROWSER_FAVORITES", 0xAB},
      {"BROWSER_HOME", 0xAC},
      // Application key
      {"APP", 0x5D},
      {"APPLICATION", 0x5D},
      {"MENUKEY", 0x5D},
      // OEM keys
      {"OEM_PLUS", 0xBB},   // =/+
      {"OEM_COMMA", 0xBC},  // ,/<
      {"OEM_MINUS", 0xBD},  // -/_
      {"OEM_PERIOD", 0xBE}, // ./>
      {"OEM_1", 0xBA},      // ;/:
      {"OEM_2", 0xBF},      // /?
      {"OEM_3", 0xC0},      // `/~
      {"OEM_4", 0xDB},      // [/{
      {"OEM_5", 0xDC},      // \/|
      {"OEM_6", 0xDD},      // ]/}
      {"OEM_7", 0xDE},      // '/"
      {"OEM_8", 0xDF},
      {"MOUSE1", 0x01},
      {"MOUSE2", 0x02},
      {"MOUSE3", 0x04},
      {"MOUSE4", 0x05},
      {"MOUSE5", 0x06},
      {"MB1", 0x01},
      {"MB2", 0x02},
      {"MB3", 0x04},
      {"MB4", 0x05},
      {"MB5", 0x06},
      {"BACKSPACE", VK_BACK},
      {"BACK", VK_BACK},
      {"ADD", VK_ADD},
      {"SUBTRACT", VK_SUBTRACT},
      {"MULTIPLY", VK_MULTIPLY},
      {"DIVIDE", VK_DIVIDE},
      {"DECIMAL", VK_DECIMAL},
      {"NUMPAD0", VK_NUMPAD0},
      {"NUMPAD1", VK_NUMPAD1},
      {"NUMPAD2", VK_NUMPAD2},
      {"NUMPAD3", VK_NUMPAD3},
      {"NUMPAD4", VK_NUMPAD4},
      {"NUMPAD5", VK_NUMPAD5},
      {"NUMPAD6", VK_NUMPAD6},
      {"NUMPAD7", VK_NUMPAD7},
      {"NUMPAD8", VK_NUMPAD8},
      {"NUMPAD9", VK_NUMPAD9}};

  for (const auto &pair : keyMap) {
    if (upper == pair.first)
      return pair.second;
  }

  return 0;
}

static QString vkToString(UINT vk) {
  // Single character keys
  if (vk >= 'A' && vk <= 'Z')
    return QString((char)vk);
  if (vk >= '0' && vk <= '9')
    return QString((char)vk);

  // Map virtual key codes to string names
  static const std::pair<UINT, const char *> keyMap[] = {
      {VK_F1, "F1"},
      {VK_F2, "F2"},
      {VK_F3, "F3"},
      {VK_F4, "F4"},
      {VK_F5, "F5"},
      {VK_F6, "F6"},
      {VK_F7, "F7"},
      {VK_F8, "F8"},
      {VK_F9, "F9"},
      {VK_F10, "F10"},
      {VK_F11, "F11"},
      {VK_F12, "F12"},
      {0x7C, "F13"},
      {0x7D, "F14"},
      {0x7E, "F15"},
      {0x7F, "F16"},
      {0x80, "F17"},
      {0x81, "F18"},
      {0x82, "F19"},
      {0x83, "F20"},
      {0x84, "F21"},
      {0x85, "F22"},
      {0x86, "F23"},
      {0x87, "F24"},
      {VK_TAB, "TAB"},
      {VK_SPACE, "SPACE"},
      {VK_ESCAPE, "ESC"},
      {VK_CONTROL, "CTRL"},
      {VK_LCONTROL, "LCTRL"},
      {VK_RCONTROL, "RCTRL"},
      {VK_SHIFT, "SHIFT"},
      {VK_LSHIFT, "LSHIFT"},
      {VK_RSHIFT, "RSHIFT"},
      {VK_MENU, "ALT"},
      {VK_LMENU, "LALT"},
      {VK_RMENU, "RALT"},
      {VK_LWIN, "LWIN"},
      {VK_RWIN, "RWIN"},
      {VK_RETURN, "ENTER"},
      {VK_CAPITAL, "CAPSLOCK"},
      {VK_INSERT, "INSERT"},
      {VK_DELETE, "DELETE"},
      {VK_HOME, "HOME"},
      {VK_END, "END"},
      {VK_PRIOR, "PAGEUP"},
      {VK_NEXT, "PAGEDOWN"},
      {VK_UP, "UP"},
      {VK_DOWN, "DOWN"},
      {VK_LEFT, "LEFT"},
      {VK_RIGHT, "RIGHT"},
      {VK_NUMLOCK, "NUMLOCK"},
      {VK_SCROLL, "SCROLLLOCK"},
      {VK_SNAPSHOT, "PRINTSCREEN"},
      {VK_PAUSE, "PAUSE"},
      // Multimedia keys
      {0xAD, "VOLUME_MUTE"},
      {0xAE, "VOLUME_DOWN"},
      {0xAF, "VOLUME_UP"},
      {0xB0, "MEDIA_NEXT"},
      {0xB1, "MEDIA_PREV"},
      {0xB2, "MEDIA_STOP"},
      {0xB3, "MEDIA_PLAY_PAUSE"},
      // Browser keys
      {0xA6, "BROWSER_BACK"},
      {0xA7, "BROWSER_FORWARD"},
      {0xA8, "BROWSER_REFRESH"},
      {0xA9, "BROWSER_STOP"},
      {0xAA, "BROWSER_SEARCH"},
      {0xAB, "BROWSER_FAVORITES"},
      {0xAC, "BROWSER_HOME"},
      // Application key
      {0x5D, "APP"},
      // OEM keys
      {0xBA, "OEM_1"},      // ;/:
      {0xBB, "OEM_PLUS"},   // =/+
      {0xBC, "OEM_COMMA"},  // ,/<
      {0xBD, "OEM_MINUS"},  // -/_
      {0xBE, "OEM_PERIOD"}, // ./>
      {0xBF, "OEM_2"},      // /?
      {0xC0, "OEM_3"},      // `/~
      {0xDB, "OEM_4"},      // [/{
      {0xDC, "OEM_5"},      // \/|
      {0xDD, "OEM_6"},      // ]/}
      {0xDE, "OEM_7"},      // '/"
      {0xDF, "OEM_8"},
      {0x01, "MOUSE1"},
      {0x02, "MOUSE2"},
      {0x04, "MOUSE3"},
      {0x05, "MOUSE4"},
      {0x06, "MOUSE5"},
      {VK_BACK, "BACKSPACE"},
      {VK_ADD, "ADD"},
      {VK_SUBTRACT, "SUBTRACT"},
      {VK_MULTIPLY, "MULTIPLY"},
      {VK_DIVIDE, "DIVIDE"},
      {VK_DECIMAL, "DECIMAL"},
      {VK_NUMPAD0, "NUMPAD0"},
      {VK_NUMPAD1, "NUMPAD1"},
      {VK_NUMPAD2, "NUMPAD2"},
      {VK_NUMPAD3, "NUMPAD3"},
      {VK_NUMPAD4, "NUMPAD4"},
      {VK_NUMPAD5, "NUMPAD5"},
      {VK_NUMPAD6, "NUMPAD6"},
      {VK_NUMPAD7, "NUMPAD7"},
      {VK_NUMPAD8, "NUMPAD8"},
      {VK_NUMPAD9, "NUMPAD9"}};

  for (const auto &pair : keyMap) {
    if (vk == pair.first)
      return QString(pair.second);
  }

  return "";
}

static QString fullKeyToString(UINT mod, UINT vk) {
  QString res;
  if (mod & MOD_CONTROL)
    res += "Ctrl + ";
  if (mod & MOD_SHIFT)
    res += "Shift + ";
  if (mod & MOD_ALT)
    res += "Alt + ";
  res += vkToString(vk);
  return res;
}

static void parseFullKey(const QString &s, UINT &outMod, UINT &outKey) {
  outMod = 0;
  outKey = 0;

  if (s.isEmpty())
    return;

  // Split by '+' and handle each part
  QStringList parts = s.split('+', Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return;

  // Process modifiers
  for (int i = 0; i < parts.size() - 1; ++i) {
    QString part = parts[i].trimmed().toLower();
    if (part == "ctrl" || part == "control" || part == "ctl")
      outMod |= MOD_CONTROL;
    else if (part == "shift" || part == "shft")
      outMod |= MOD_SHIFT;
    else if (part == "alt" || part == "menu")
      outMod |= MOD_ALT;
    else if (part == "win" || part == "windows" || part == "meta")
      outMod |= MOD_WIN;
    else if (part == "lctrl" || part == "leftctrl")
      outMod |= MOD_CONTROL; // Windows doesn't distinguish left/right in
                             // RegisterHotKey
    else if (part == "rctrl" || part == "rightctrl")
      outMod |= MOD_CONTROL;
    else if (part == "lshift" || part == "leftshift")
      outMod |= MOD_SHIFT;
    else if (part == "rshift" || part == "rightshift")
      outMod |= MOD_SHIFT;
    else if (part == "lalt" || part == "leftalt")
      outMod |= MOD_ALT;
    else if (part == "ralt" || part == "rightalt")
      outMod |= MOD_ALT;
  }

  // Last part is the main key
  QString keyPart = parts.last().trimmed();
  outKey = stringToVk(keyPart);

  // If no key was found but we have a single part, try to parse it as a key
  if (outKey == 0 && parts.size() == 1) {
    outKey = stringToVk(parts[0].trimmed());
  }
}

QString BetterAngleBackend::keyToggle() const {
  if (g_allProfiles.empty())
    return "Ctrl + U";
  const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
  return fullKeyToString(k.toggleMod, k.toggleKey);
}
void BetterAngleBackend::setKeyToggle(const QString &s) {
  if (!g_allProfiles.empty()) {
    UINT mod, vk;
    parseFullKey(s, mod, vk);

    // Check for duplicates
    const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
    if ((mod == k.roiMod && vk == k.roiKey) ||
        (mod == k.crossMod && vk == k.crossKey) ||
        (mod == k.zeroMod && vk == k.zeroKey)) {
      emit hotkeysChanged(); // Force UI refresh to revert text
      return;
    }

    g_allProfiles[g_selectedProfileIdx].keybinds.toggleMod = mod;
    g_allProfiles[g_selectedProfileIdx].keybinds.toggleKey = vk;
    saveKeybinds();
    SaveSettings();
    emit hotkeysChanged();
  }
}

QString BetterAngleBackend::keyRoi() const {
  if (g_allProfiles.empty())
    return "Ctrl + R";
  const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
  return fullKeyToString(k.roiMod, k.roiKey);
}
void BetterAngleBackend::setKeyRoi(const QString &s) {
  if (!g_allProfiles.empty()) {
    UINT mod, vk;
    parseFullKey(s, mod, vk);

    // Check for duplicates
    const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
    if ((mod == k.toggleMod && vk == k.toggleKey) ||
        (mod == k.crossMod && vk == k.crossKey) ||
        (mod == k.zeroMod && vk == k.zeroKey)) {
      emit hotkeysChanged();
      return;
    }

    g_allProfiles[g_selectedProfileIdx].keybinds.roiMod = mod;
    g_allProfiles[g_selectedProfileIdx].keybinds.roiKey = vk;
    saveKeybinds();
    SaveSettings();
    emit hotkeysChanged();
  }
}

QString BetterAngleBackend::keyCross() const {
  if (g_allProfiles.empty())
    return "F10";
  const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
  return fullKeyToString(k.crossMod, k.crossKey);
}
void BetterAngleBackend::setKeyCross(const QString &s) {
  if (!g_allProfiles.empty()) {
    UINT mod, vk;
    parseFullKey(s, mod, vk);

    // Check for duplicates
    const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
    if ((mod == k.toggleMod && vk == k.toggleKey) ||
        (mod == k.roiMod && vk == k.roiKey) ||
        (mod == k.zeroMod && vk == k.zeroKey)) {
      emit hotkeysChanged();
      return;
    }

    g_allProfiles[g_selectedProfileIdx].keybinds.crossMod = mod;
    g_allProfiles[g_selectedProfileIdx].keybinds.crossKey = vk;
    saveKeybinds();
    SaveSettings();
    emit hotkeysChanged();
  }
}

QString BetterAngleBackend::keyZero() const {
  if (g_allProfiles.empty())
    return "Ctrl + G";
  const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
  return fullKeyToString(k.zeroMod, k.zeroKey);
}
void BetterAngleBackend::setKeyZero(const QString &s) {
  if (!g_allProfiles.empty()) {
    UINT mod, vk;
    parseFullKey(s, mod, vk);

    // Check for duplicates
    const auto &k = g_allProfiles[g_selectedProfileIdx].keybinds;
    if ((mod == k.toggleMod && vk == k.toggleKey) ||
        (mod == k.roiMod && vk == k.roiKey) ||
        (mod == k.crossMod && vk == k.crossKey)) {
      emit hotkeysChanged();
      return;
    }

    g_allProfiles[g_selectedProfileIdx].keybinds.zeroMod = mod;
    g_allProfiles[g_selectedProfileIdx].keybinds.zeroKey = vk;
    saveKeybinds();
    SaveSettings();
    emit hotkeysChanged();
  }
}

void BetterAngleBackend::saveKeybinds() {
  if (g_allProfiles.empty())
    return;
  Profile &p = g_allProfiles[g_selectedProfileIdx];
  p.Save(GetProfilesPath() + p.name + L".json");
  RefreshHotkeys(g_hHUD);
}

void BetterAngleBackend::startKeybindAssignment() {
  g_keybindAssignmentActive = true;
}

void BetterAngleBackend::endKeybindAssignment() {
  g_keybindAssignmentActive = false;
}

void NotifyBackendUpdateStatusChanged() {
  if (s_backendInstance) {
    emit s_backendInstance->updateStatusChanged();
  }
}

static bool IsFortniteExe(const wchar_t *processName) {
  if (!processName || !processName[0])
    return false;
  return (_wcsnicmp(processName, L"FortniteClient-Win64-Shipping", 29) == 0 ||
          _wcsnicmp(processName, L"FortniteLauncher", 16) == 0 ||
          _wcsnicmp(processName, L"FortniteClient", 14) == 0);
}

bool BetterAngleBackend::fnRunning() const {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(hSnap, &pe)) {
    do {
      if (IsFortniteExe(pe.szExeFile)) {
        CloseHandle(hSnap);
        return true;
      }
    } while (Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return false;
}

bool BetterAngleBackend::fnFocused() const { return IsFortniteForeground(); }

bool BetterAngleBackend::fnMouseHidden() const {
  return !IsCursorCurrentlyVisible();
}

bool BetterAngleBackend::showDebugOverlay() const { return g_showDebugOverlay; }

void BetterAngleBackend::setShowDebugOverlay(bool v) {
  if (g_showDebugOverlay != v) {
    g_showDebugOverlay = v;
    emit debugDataChanged();
  }
}

void BetterAngleBackend::refreshDebugData() { emit debugDataChanged(); }

long long BetterAngleBackend::detectionDelayMs() const {
  return (long long)g_detectionDelayMs;
}
int BetterAngleBackend::detectionRatioPct() const {
  if (g_allProfiles.empty()) return 0;
  const Profile &p = g_allProfiles[g_selectedProfileIdx];
  int area = p.roi_w * p.roi_h;
  if (area <= 0) return 0;
  return (int)((g_matchCount.load() * 100) / area);
}
bool BetterAngleBackend::inputLocked() const {
  return g_mouseSuspendedUntil > 0 && GetTickCount64() < g_mouseSuspendedUntil;
}
bool BetterAngleBackend::isDiving() const { return g_isDiving; }


int BetterAngleBackend::peakMatchPct() const {
  if (g_allProfiles.empty()) return 0;
  const Profile &p = g_allProfiles[g_selectedProfileIdx];
  int area = p.roi_w * p.roi_h;
  if (area <= 0) return 0;
  return (int)((g_peakMatchCount.load() * 100) / area);
}

QString BetterAngleBackend::roiDimensions() const {
  if (g_allProfiles.empty())
    return "N/A";
  auto &p = g_allProfiles[g_selectedProfileIdx];
  return QString("%1,%2 %3x%4")
      .arg(p.roi_x)
      .arg(p.roi_y)
      .arg(p.roi_w)
      .arg(p.roi_h);
}

int BetterAngleBackend::scannerCpuPct() const {
  return g_scannerCpuPct.load();
}
QString BetterAngleBackend::physicalKeyStates() const {
  QString states;
  static const int keys[] = {'W', 'A', 'S', 'D', VK_SPACE};
  static const char* names[] = {"W", "A", "S", "D", "SPC"};
  
  for (int i = 0; i < 5; ++i) {
    bool phys = g_physicalKeys[keys[i]].load(std::memory_order_relaxed);
    bool tracked = (GetKeyState(keys[i]) & 0x8000) != 0;
    
    // Format: W:P1/T1 (1=Down, 0=Up)
    states += QString("%1:%2/%3 ").arg(names[i]).arg(phys?1:0).arg(tracked?1:0);
  }
  return states;
}

QString BetterAngleBackend::inputLockStatus() const {
    bool isLocked = (GetTickCount64() < g_mouseSuspendedUntil.load());
    return isLocked ? "ACTIVE" : "IDLE";
}


void BetterAngleBackend::finishBooting() {
  if (g_hHUD) {
    ShowWindow(g_hHUD, SW_SHOW);
  }
}

void BetterAngleBackend::setZero() {
  extern float g_currentAngle;
  g_currentAngle = 0.0f;
  g_logic.SetZero();
  Beep(1000, 80);
}

static ULONGLONG FileTimeToInt64(const FILETIME& ft) {
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ((ULONGLONG)ft.dwLowDateTime);
}

double BetterAngleBackend::cpuUsage() const {
    static ULONGLONG lastSystemTime = 0;
    static ULONGLONG lastProcessTime = 0;
    static double lastCpu = 0.0;

    FILETIME fCreation, fExit, fKernel, fUser;
    FILETIME fSystemIdle, fSystemKernel, fSystemUser;

    if (GetProcessTimes(GetCurrentProcess(), &fCreation, &fExit, &fKernel, &fUser) &&
        GetSystemTimes(&fSystemIdle, &fSystemKernel, &fSystemUser)) {
        
        ULONGLONG sysTime = FileTimeToInt64(fSystemKernel) + FileTimeToInt64(fSystemUser);
        ULONGLONG procTime = FileTimeToInt64(fKernel) + FileTimeToInt64(fUser);

        if (lastSystemTime != 0) {
            ULONGLONG sysDiff = sysTime - lastSystemTime;
            ULONGLONG procDiff = procTime - lastProcessTime;
            if (sysDiff > 0) {
                lastCpu = (double)procDiff / (double)sysDiff * 100.0;
            }
        }
        lastSystemTime = sysTime;
        lastProcessTime = procTime;
    }
    return lastCpu;
}

double BetterAngleBackend::ramUsageMb() const {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
    return 0.0;
}
