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
extern std::atomic<int> g_scannerCpuPct;
extern std::atomic<bool> g_physicalKeys[256];
extern std::atomic<bool> g_running;
extern int g_screenIndex;
extern std::atomic<bool> g_fortniteFocusedCache;
extern std::atomic<ULONGLONG> g_lastLockTime;

extern std::string g_lastVersionRun;

// Version numbers ? updated by scripts/bump_version.ps1
#ifndef V_MAJ
#define V_MAJ 5
#define V_MIN 5
#define V_PAT 197
#endif

#define VERSION_STR APP_STR_Y(V_MAJ) "." APP_STR_Y(V_MIN) "." APP_STR_Y(V_PAT)
#define VERSION_WSTR                                                           \
  APP_WSTR_Y(V_MAJ) L"." APP_WSTR_Y(V_MIN) L"." APP_WSTR_Y(V_PAT)

// Global Profile Management
extern Profile g_currentProfile;
extern std::vector<Profile> g_allProfiles;
extern int g_selectedProfileIdx;
extern std::wstring g_lastLoadedProfileName;

extern HWND g_fortniteWindow;
extern RECT g_fortniteRect;

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
extern std::atomic<bool> g_blockInputActive;

// Pre-spawned BlockInput worker ? eliminates ~5-20ms thread-creation latency
// on FOV transitions. Detector signals g_lockEvent; worker wakes and locks input.
extern HANDLE g_lockEvent;
extern std::atomic<int> g_lockDurationMs;
void StartBlockInputWorker();

// Bumped on every WM_DISPLAYCHANGE so DetectorThread can invalidate its cached
// monitor rect when virtual-desktop layout shifts (e.g. user hot-plugs a 2nd monitor).
extern std::atomic<int> g_displayChangeGen;

// Debug overlay diagnostics for the capture path:
//   g_lastScanUsedDxgi: true if the most recent Scan ran the DXGI path
//   g_lastPickSource:   0 = no pick yet, 1 = DXGI, 2 = BitBlt fallback
extern std::atomic<bool> g_lastScanUsedDxgi;
extern std::atomic<int>  g_lastPickSource;

// Tripwire pre-arm state (v5.5.162). True between the moment the 3-pixel
// tripwire fires speculatively and the moment BlockInput auto-releases.
// Used by debug overlay to show "ARMED" indicator.
extern std::atomic<bool> g_preArmActive;
// Wall-clock ms of the last pre-arm fire (for fading the ARMED indicator).
extern std::atomic<ULONGLONG> g_lastPreArmTime;

void NotifyBackendCrosshairChanged();
void NotifyBackendUpdateStatusChanged();

RECT GetMonitorRectByIndex(int index);

#endif // STATE_H
