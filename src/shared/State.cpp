#include "shared/State.h"
#include "shared/Logic.h"

SelectionState g_currentSelection = NONE;
bool g_isSelectionActive = false;
HBITMAP g_screenSnapshot = NULL;
bool g_isDiving = false;
bool g_showROIBox = true;
int g_currentTab = 0;
std::atomic<int> g_matchCount{0};
bool g_isCheckingForUpdates = false;
bool g_hasCheckedForUpdates = false;
float g_updateSpinAngle = 0.0f;
bool g_updateAvailable = false;
bool g_isDownloadingUpdate = false;
bool g_downloadComplete = false;
std::string g_updateHistory = "";
std::atomic<bool> g_fortniteFocusedCache(false);
std::string g_lastVersionRun = "";
std::atomic<bool> g_forceRedraw(true);
std::atomic<bool> g_keybindAssignmentActive(false);
std::atomic<long long> g_detectionDelayMs(0);
std::atomic<bool> g_showDebugOverlay(false);
std::atomic<ULONGLONG> g_mouseSuspendedUntil(0);
std::atomic<int> g_lockTriggerReason(0);
std::atomic<int> g_lockCount(0);
std::atomic<DWORD> g_lockThreadId(0);
std::atomic<long long> g_lockDurationMs(0);
std::atomic<short> g_wPreLock(0);
std::atomic<short> g_wPostUnlock(0);
std::atomic<short> g_wPostFlush(0);
std::atomic<bool> g_preState[4];
std::atomic<bool> g_postState[4];
std::atomic<bool> g_blockInputActive(false);
std::atomic<bool> g_tableRefreshed(false);
std::atomic<bool> g_hasSynced(false);
std::atomic<int> g_activeFallback(0);
std::atomic<bool> g_fb1Active(false);
std::atomic<bool> g_rawKeyUpDetected[256] = {};
std::atomic<bool> g_rawKeyMakeDetected[256] = {};
std::atomic<ULONGLONG> g_lastLockTime(0);
std::atomic<bool> g_lockInProgress(false);
std::atomic<bool> g_ghostFixInProgress(false);
std::atomic<long long> g_ghostFixDurationMs(0);
std::atomic<bool> g_ghostFixVerifyOk(true);
std::mutex g_lockMutex;
std::mutex g_blockInputMutex;
std::string g_nitroSyncLog = "No sync events yet";
std::atomic<int> g_peakMatchCount{0};
std::atomic<int> g_requiredMatchCount{0};
std::atomic<int> g_scannerCpuPct(0);
std::atomic<bool> g_physicalKeys[256] = {};
std::atomic<bool> g_running(true);
int g_screenIndex = 0;

Profile g_currentProfile;
std::vector<Profile> g_allProfiles;
int g_selectedProfileIdx = 0;

// Global g_keybinds removed (v4.20.37)
std::wstring g_lastLoadedProfileName = L"";

#include <fstream>
#include <locale>
#include <shlobj.h>
#include <sstream>
#include <string>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

std::wstring GetAppRootPath() {
  wchar_t appdata[MAX_PATH];
  if (SUCCEEDED(
          SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) {
    std::wstring path = std::wstring(appdata) + L"\\BetterAngle";
    CreateDirectoryW(path.c_str(), NULL);
    return path + L"\\";
  }
  return L"";
}

std::wstring GetProfilesPath() {
  std::wstring root = GetAppRootPath();
  if (root.empty())
    return L"";
  std::wstring pPath = root + L"profiles";
  CreateDirectoryW(pPath.c_str(), NULL);
  SetFileAttributesW(pPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
  return pPath + L"\\";
}

// Legacy Registry functions removed in favor of unified hidden JSON storage.

void LoadSettings() {
  std::wstring sp = GetAppRootPath() + L"settings.json";
  std::ifstream ifs(sp.c_str());
  if (ifs.is_open()) {
    std::string content;
    ifs.seekg(0, std::ios::end);
    content.reserve((size_t)ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    content.assign((std::istreambuf_iterator<char>(ifs)),
                   std::istreambuf_iterator<char>());

    auto eFloat = [&](std::string k, float def) -> float {
      size_t p = content.find("\"" + k + "\":");
      if (p == std::string::npos)
        return def;
      size_t valStart =
          content.find_first_not_of(" \t\n\r", p + k.length() + 2);
      if (valStart == std::string::npos)
        return def;
      try {
        std::istringstream iss(content.substr(valStart));
        iss.imbue(std::locale("C"));
        float v;
        iss >> v;
        return v;
      } catch (...) {
        return def;
      }
    };
    auto eInt = [&](std::string k, int def) -> int {
      size_t p = content.find("\"" + k + "\":");
      if (p == std::string::npos)
        return def;
      size_t valStart =
          content.find_first_not_of(" \t\n\r", p + k.length() + 2);
      if (valStart == std::string::npos)
        return def;
      try {
        return std::stoi(content.substr(valStart));
      } catch (...) {
        return def;
      }
    };

    g_hudX = eInt("hudX", 40);
    g_hudY = eInt("hudY", 40);

    g_crossPulse = eFloat("crossPulse", 0.0f) > 0.5f;
    g_showCrosshair = eFloat("showCrosshair", 1.0f) > 0.5f;
    g_selectedProfileIdx = eInt("selectedProfileIdx", 0);
    g_screenIndex = eInt("screenIndex", 0);

    size_t vp = content.find("\"lastVersionRun\":\"");
    if (vp != std::string::npos) {
      size_t valS = vp + 18;
      size_t end = content.find("\"", valS);
      if (end != std::string::npos)
        g_lastVersionRun = content.substr(valS, end - valS);
    }

    size_t pp = content.find("\"lastProfile\":\"");
    if (pp != std::string::npos) {
      size_t valS = pp + 15;
      size_t end = content.find("\"", valS);
      if (end != std::string::npos) {
        std::string n = content.substr(valS, end - valS);
        g_lastLoadedProfileName = std::wstring(n.begin(), n.end());
      }
    }
  } else {
    // Migration: Check if it exists in the OLD path (profiles/settings.json)
    std::wstring oldPath = GetProfilesPath() + L"settings.json";
    if (GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
      MoveFileW(oldPath.c_str(), sp.c_str());
      LoadSettings();
      return;
    }
  }
}

void SaveSettings() {
  std::wstring sp = GetAppRootPath() + L"settings.json";
  std::wstring tempPath = sp + L".tmp";

  // Ensure file is not hidden before writing to avoid permission issues
  SetFileAttributesW(sp.c_str(), FILE_ATTRIBUTE_NORMAL);

  std::ofstream ofs(tempPath.c_str(), std::ios::trunc);
  if (!ofs.is_open())
    return;

  std::ostringstream oss;
  oss.imbue(std::locale("C"));

  oss << "{\n";
  oss << "  \"hudX\": " << g_hudX << ",\n";
  oss << "  \"hudY\": " << g_hudY << ",\n";
  oss << "  \"showCrosshair\": " << (g_showCrosshair ? 1 : 0) << ",\n";
  oss << "  \"selectedProfileIdx\": " << g_selectedProfileIdx << ",\n";
  oss << "  \"screenIndex\": " << g_screenIndex << ",\n";

  oss << "  \"lastVersionRun\":\"" << VERSION_STR << "\",\n";

  std::string lp;
  for (wchar_t c : g_lastLoadedProfileName)
    lp += (char)c;
  oss << "  \"lastProfile\":\"" << lp << "\"\n";
  oss << "}\n";

  ofs << oss.str();
  ofs.close();

  // Atomic swap: delete old, rename temp to real
  DeleteFileW(sp.c_str());
  MoveFileW(tempPath.c_str(), sp.c_str());

  // Set to hidden after saving
  SetFileAttributesW(sp.c_str(), FILE_ATTRIBUTE_HIDDEN);
}

bool g_showCrosshair = false;
float g_crossThickness = 1.0f;
COLORREF g_crossColor = RGB(255, 0, 0);
float g_crossOffsetX = 0.0f;
float g_crossOffsetY = 0.0f;
float g_crossAngle = 0.0f;
bool g_crossPulse = false;

COLORREF g_targetColor = RGB(255, 255, 255);
COLORREF g_pickedColor = RGB(255, 255, 255);
float g_latestVersion = 4.920f;
std::wstring g_latestName = L"Pending Scan";
RECT g_selectionRect = {0, 0, 0, 0};
POINT g_startPoint = {0};

std::string g_latestVersionOnline = "v" VERSION_STR;
float g_currentAngle = 0.0f;
std::atomic<bool> g_isCursorVisible(false);
AngleLogic g_logic(0.05);

int g_hudX = -1;
int g_hudY = 40;
bool g_isDraggingHUD = false;
POINT g_dragStartHUD = {0, 0};
POINT g_dragStartMouse = {0, 0};
HWND g_hHUD = NULL;
HWND g_hPanel = NULL;
HWND g_hMsgWnd = NULL;

RECT GetMonitorRectByIndex(int index) {
  struct RectData {
    int targetIndex;
    int currentIndex;
    RECT rect;
  } data = {index, 0, {0, 0, 0, 0}};

  EnumDisplayMonitors(
      NULL, NULL,
      [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor,
         LPARAM dwData) -> BOOL {
        auto d = reinterpret_cast<RectData *>(dwData);
        if (d->currentIndex == d->targetIndex) {
          d->rect = *lprcMonitor;
          return FALSE; // Found it
        }
        d->currentIndex++;
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&data));

  return data.rect;
}
