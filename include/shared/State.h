#ifndef STATE_H
#define STATE_H

#include "shared/Logic.h"
#include "shared/Profile.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

std::wstring GetAppRootPath();
std::wstring GetProfilesPath();

// Versioning system
#define APP_STR_Z(x) #x
#define APP_STR_Y(x) APP_STR_Z(x)
#define APP_WSTR_Z(x) L#x
#define APP_WSTR_Y(x) APP_WSTR_Z(x)

extern std::atomic<long long> g_detectionDelayMs;
extern std::atomic<bool> g_showDebugOverlay;
extern std::atomic<ULONGLONG> g_mouseSuspendedUntil;
extern std::atomic<int>
    g_lockTriggerReason; // 0=None,1=Glide?Dive,2=Dive?Glide,3=Alt-Tab
extern std::atomic<int> g_scannerCpuPct;
extern std::atomic<bool> g_physicalKeys[256];
extern std::atomic<bool> g_running;
extern std::string g_nitroSyncLog;
extern int g_screenIndex;
extern std::atomic<int> g_displayChangeGen;
extern std::atomic<int> g_hudDecimalPlaces;
extern std::atomic<bool> g_fortniteFocusedCache;
extern std::atomic<int> g_lockCount;
extern std::atomic<bool> g_blockInputActive;
extern std::mutex g_lockMutex;
extern std::atomic<ULONGLONG> g_lastLockTime;
extern std::mutex g_blockInputMutex;

extern std::string g_lastVersionRun;

// Version numbers ? updated by scripts/bump_version.ps1
#ifndef V_MAJ
#define V_MAJ 5
#define V_MIN 5
#define V_PAT 249
#endif

#define VERSION_STR APP_STR_Y(V_MAJ) "." APP_STR_Y(V_MIN) "." APP_STR_Y(V_PAT)
#define VERSION_WSTR                                                           \
  APP_WSTR_Y(V_MAJ) L"." APP_WSTR_Y(V_MIN) L"." APP_WSTR_Y(V_PAT)

// Global Profile Management
extern Profile g_currentProfile;
extern std::vector<Profile> g_allProfiles;
extern int g_selectedProfileIdx;
extern std::wstring g_lastLoadedProfileName;

// HUD & Global Shared State
enum SelectionState { NONE, SELECTING_ROI, SELECTING_COLOR };
extern SelectionState g_currentSelection;
extern bool g_isSelectionActive;
extern HBITMAP g_screenSnapshot;
extern bool g_isDiving;
extern bool g_showROIBox;
extern int g_currentTab;
extern std::wstring g_latestName;
extern bool g_isCheckingForUpdates;
extern bool g_hasCheckedForUpdates;
extern bool g_updateAvailable;
extern bool g_isDownloadingUpdate;
extern bool g_downloadComplete;
extern std::string g_updateHistory; // e.g. "v4.20.1 ? v4.20.55"

// Keybinds struct moved to Profile.h (v4.20.37)
void LoadSettings();
void SaveSettings();

extern bool g_showCrosshair;
extern float g_crossThickness;
extern COLORREF g_crossColor;
extern float g_crossOffsetX;
extern float g_crossOffsetY;
extern float g_crossAngle;
extern bool g_crossPulse;

extern COLORREF g_targetColor;
extern COLORREF g_pickedColor;
extern float g_latestVersion;
extern std::atomic<int> g_matchCount;
extern std::atomic<int> g_peakMatchCount;
extern std::atomic<int> g_requiredMatchCount;
extern float g_updateSpinAngle;
extern RECT g_selectionRect;
extern POINT g_startPoint;
extern std::string g_latestVersionOnline;
extern float g_currentAngle;
extern std::atomic<bool> g_isCursorVisible;
extern AngleLogic g_logic;
extern int g_hudX;
extern int g_hudY;
extern bool g_isDraggingHUD;
extern POINT g_dragStartHUD;
extern POINT g_dragStartMouse;
extern HWND g_hHUD;
extern HWND g_hPanel;
extern HWND g_hMsgWnd;

bool RefreshHotkeys(HWND hWnd);
extern std::atomic<bool> g_forceRedraw;
extern std::atomic<bool> g_keybindAssignmentActive;
void NotifyBackendCrosshairChanged();
void NotifyBackendUpdateStatusChanged();

RECT GetMonitorRectByIndex(int index);

#endif // STATE_H
