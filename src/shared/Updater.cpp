#include "shared/Updater.h"
#include "shared/State.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

const wchar_t *VERSION_URL =
    L"https://api.github.com/repos/wavedropmaps-org/BetterAngle/releases/latest";
// Beta: /releases list sorted newest-first, includes pre-releases
const wchar_t *RELEASES_LIST_URL =
    L"https://api.github.com/repos/wavedropmaps-org/BetterAngle/releases?per_page=1";
const wchar_t *MIN_STABLE_URL =
    L"https://raw.githubusercontent.com/wavedropmaps-org/BetterAngle/main/MIN_STABLE_VERSION";
const wchar_t *DOWNLOAD_URL = L"https://github.com/wavedropmaps-org/BetterAngle/"
                              L"releases/latest/download/BetterAngle_Setup.exe";

// Fetch a small text body into a std::string (no temp file needed).
static std::string FetchString(const wchar_t *url) {
  HINTERNET hNet = InternetOpenW(L"BetterAngle/" VERSION_WSTR,
                                 INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
  if (!hNet) return {};
  const wchar_t *hdrs = L"Accept: application/vnd.github.v3+json\r\n"
                        L"User-Agent: BetterAngle/" VERSION_WSTR L"\r\n";
  HINTERNET hUrl = InternetOpenUrlW(hNet, url, hdrs, (DWORD)-1,
                                    INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
  std::string out;
  if (hUrl) {
    char buf[8192]; DWORD n;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &n) && n > 0)
      out.append(buf, n);
    InternetCloseHandle(hUrl);
  }
  InternetCloseHandle(hNet);
  return out;
}

// Returns >0 if a>b, 0 if equal, <0 if a<b.  Strips leading 'v' and
// pre-release suffixes like -beta before comparing as major.minor.patch.
static int CompareVersions(const std::string &a, const std::string &b) {
  auto clean = [](std::string s) {
    if (!s.empty() && (s[0]=='v' || s[0]=='V')) s = s.substr(1);
    auto d = s.find('-'); if (d != std::string::npos) s = s.substr(0, d);
    return s;
  };
  std::string sa = clean(a), sb = clean(b);
  int am=0,an=0,ap=0,bm=0,bn=0,bp=0;
  sscanf_s(sa.c_str(), "%d.%d.%d", &am, &an, &ap);
  sscanf_s(sb.c_str(), "%d.%d.%d", &bm, &bn, &bp);
  if (am != bm) return am > bm ? 1 : -1;
  if (an != bn) return an > bn ? 1 : -1;
  if (ap != bp) return ap > bp ? 1 : -1;
  return 0;
}

bool DownloadFile(const std::wstring &url, const std::wstring &dest) {
  HINTERNET hInternet =
      InternetOpenW(L"BetterAngle", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
  if (!hInternet)
    return false;

  // Use a more standard User-Agent to avoid being blocked by GitHub
  std::wstring headers =
      L"Accept: application/vnd.github.v3+json\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36\r\n";
  HINTERNET hUrl =
      InternetOpenUrlW(hInternet, url.c_str(), headers.c_str(), (DWORD)-1,
                       INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);

  if (!hUrl) {
    InternetCloseHandle(hInternet);
    return false;
  }

  std::ofstream ofs(dest, std::ios::binary);
  if (!ofs.is_open()) {
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return false;
  }

  char buffer[8192];
  DWORD bytesRead;
  while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) &&
         bytesRead > 0) {
    ofs.write(buffer, bytesRead);
  }

  ofs.close();
  InternetCloseHandle(hUrl);
  InternetCloseHandle(hInternet);
  return true;
}

static std::wstring g_dynamicDownloadUrl = DOWNLOAD_URL;

static bool IsLikelyWindowsExecutable(const std::wstring &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open())
    return false;

  char mz[2] = {0, 0};
  ifs.read(mz, 2);
  return ifs.gcount() == 2 && mz[0] == 'M' && mz[1] == 'Z';
}

static std::wstring EscapePowerShellSingleQuoted(const std::wstring &value) {
  std::wstring escaped;
  escaped.reserve(value.size());
  for (wchar_t ch : value) {
    if (ch == L'\'')
      escaped += L"''";
    else
      escaped += ch;
  }
  return escaped;
}

// Pull tag_name + browser_download_url for BetterAngle_Setup.exe from a
// GitHub releases JSON body (works for both /latest and /releases?per_page=1).
static bool ParseReleasesJson(const std::string &json,
                               std::string &outTag,
                               std::wstring &outDownloadUrl) {
  size_t tagPos = json.find("\"tag_name\"");
  if (tagPos == std::string::npos) return false;
  size_t col = json.find(":", tagPos + 10);
  if (col == std::string::npos) return false;
  size_t s = json.find("\"", col + 1);
  size_t e = (s != std::string::npos) ? json.find("\"", s + 1) : std::string::npos;
  if (s == std::string::npos || e == std::string::npos) return false;
  outTag = json.substr(s + 1, e - s - 1);

  size_t aPos = json.find("\"browser_download_url\":");
  while (aPos != std::string::npos) {
    size_t uS = json.find("\"", aPos + 23);
    size_t uE = (uS != std::string::npos) ? json.find("\"", uS + 1) : std::string::npos;
    if (uS != std::string::npos && uE != std::string::npos) {
      std::string u = json.substr(uS + 1, uE - uS - 1);
      if (u.find("BetterAngle_Setup.exe") != std::string::npos) {
        outDownloadUrl = std::wstring(u.begin(), u.end());
        break;
      }
    }
    aPos = json.find("\"browser_download_url\":", aPos + 23);
  }
  return true;
}

bool CheckForUpdates() {
  struct Guard {
    ~Guard() { g_isCheckingForUpdates = false; NotifyBackendUpdateStatusChanged(); }
  } guard;

  g_isCheckingForUpdates = true;
  bool beta = g_betaUpdates.load();

  // Fetch release info — beta uses the full list (includes pre-releases),
  // stable uses /latest (pre-releases are invisible to that endpoint).
  const wchar_t *endpoint = beta ? RELEASES_LIST_URL : VERSION_URL;
  std::string json = FetchString(endpoint);
  if (json.empty()) {
    g_hasCheckedForUpdates = true;
    g_updateHistory = "Update check failed. Check your internet connection.";
    return false;
  }

  std::string latestTag;
  std::wstring dlUrl;
  if (!ParseReleasesJson(json, latestTag, dlUrl)) {
    g_hasCheckedForUpdates = true;
    g_updateHistory = "Update check failed — could not parse release info.";
    return false;
  }

  g_latestVersionOnline = latestTag;
  if (!dlUrl.empty()) g_dynamicDownloadUrl = dlUrl;

  std::string currentVer = VERSION_STR;

  // MIN_STABLE_VERSION gate (stable channel only).
  // Only notify if the latest full release has been graduated to stable.
  if (!beta) {
    std::string minStableRaw = FetchString(MIN_STABLE_URL);
    // Trim whitespace
    while (!minStableRaw.empty() &&
           (minStableRaw.back() == '\n' || minStableRaw.back() == '\r' ||
            minStableRaw.back() == ' '))
      minStableRaw.pop_back();

    if (!minStableRaw.empty() && CompareVersions(latestTag, minStableRaw) < 0) {
      // Latest release hasn't been graduated yet — stay quiet.
      g_updateAvailable = false;
      g_updateHistory = "You're up to date (stable channel).";
      g_hasCheckedForUpdates = true;
      return false;
    }
  }

  if (CompareVersions(latestTag, currentVer) > 0) {
    g_updateAvailable = true;
    g_updateHistory = "New version available: " + latestTag +
                      (beta ? " [beta channel]" : "");
  } else {
    g_updateAvailable = false;
    g_updateHistory = std::string("Up to date (v") + currentVer + ")" +
                      (beta ? " [beta channel]" : "");
  }

  g_hasCheckedForUpdates = true;
  return g_updateAvailable;
}

void UpdateApp() {
  if (g_isDownloadingUpdate || g_downloadComplete)
    return;

  g_isDownloadingUpdate = true;
  std::wstring downloadUrl = g_dynamicDownloadUrl; // capture before thread spawn
  std::thread([downloadUrl]() {
    std::wstring dest = GetAppRootPath() + L"BetterAngle_Setup_update.exe";
    if (DownloadFile(downloadUrl, dest) &&
        IsLikelyWindowsExecutable(dest)) {
      g_downloadComplete = true;
    } else {
      DeleteFileW(dest.c_str());
      g_downloadComplete = false;
      g_updateAvailable = true;
      g_updateHistory = "Downloaded update was invalid";
    }
    g_isDownloadingUpdate = false;
  }).detach();
}

void CleanupUpdateJunk() {
  std::wstring root = GetAppRootPath();
  DeleteFileW((root + L"ba_update.bat").c_str());
  DeleteFileW((root + L"ba_update.ps1").c_str());
  DeleteFileW((root + L"BetterAngle_Setup_update.exe").c_str());
  DeleteFileW((root + L"latest_version.txt").c_str());
}

void ApplyUpdateAndRestart() {
  std::wstring root = GetAppRootPath();
  std::wstring installerPath = root + L"BetterAngle_Setup_update.exe";

  auto openReleasePage = []() {
    ShellExecuteW(NULL, L"open",
                  L"https://github.com/wavedropmaps-org/BetterAngle/releases", NULL,
                  NULL, SW_SHOWNORMAL);
  };

  if (GetFileAttributesW(installerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    g_downloadComplete = false;
    g_updateAvailable = true;
    openReleasePage();
    return;
  }

  wchar_t currentExe[MAX_PATH] = {};
  if (GetModuleFileNameW(NULL, currentExe, MAX_PATH) == 0) {
    g_downloadComplete = false;
    g_updateAvailable = true;
    openReleasePage();
    return;
  }

  const std::wstring installerEsc = EscapePowerShellSingleQuoted(installerPath);
  const std::wstring currentExeEsc = EscapePowerShellSingleQuoted(currentExe);
  const std::wstring paramsEsc = EscapePowerShellSingleQuoted(
      L"/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART "
      L"/CLOSEAPPLICATIONS /FORCECLOSEAPPLICATIONS");

  std::wstring psCommand =
      L"$ErrorActionPreference = 'Stop'; "
      L"$installer = '" +
      installerEsc +
      L"'; "
      L"$app = '" +
      currentExeEsc +
      L"'; "
      L"$args = '" +
      paramsEsc +
      L"'; "
      L"Start-Sleep -Seconds 2; "
      L"try { "
      L"$p = Start-Process -FilePath $installer -ArgumentList $args -Verb "
      L"RunAs -PassThru -Wait; "
      L"if ($p.ExitCode -eq 0 -and (Test-Path -LiteralPath $app)) { "
      L"Start-Sleep -Seconds 2; Start-Process -FilePath $app -WorkingDirectory "
      L"(Split-Path -Parent $app) | Out-Null; "
      L"} elseif (Test-Path -LiteralPath $app) { "
      L"Start-Process -FilePath $app -WorkingDirectory (Split-Path -Parent "
      L"$app) | Out-Null; "
      L"} "
      L"} catch { "
      L"if (Test-Path -LiteralPath $app) { Start-Process -FilePath $app "
      L"-WorkingDirectory (Split-Path -Parent $app) | Out-Null } "
      L"} finally { "
      L"Remove-Item -LiteralPath $installer -Force -ErrorAction "
      L"SilentlyContinue "
      L"}";

  std::wstring psArgs =
      L"-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"" +
      psCommand + L"\"";
  HINSTANCE result = ShellExecuteW(NULL, L"open", L"powershell.exe",
                                   psArgs.c_str(), NULL, SW_HIDE);
  if ((INT_PTR)result <= 32) {
    g_downloadComplete = false;
    g_updateAvailable = true;
    openReleasePage();
    return;
  }

  exit(0);
}
