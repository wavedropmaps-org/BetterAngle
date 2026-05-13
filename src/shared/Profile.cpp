#include "shared/Profile.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <vector>
#include <windows.h>

bool Profile::Load(const std::wstring &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;

  // Simple manual JSON parser to avoid external dependencies
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  // Extract name
  size_t namePos = content.find("\"name\": \"");
  if (namePos != std::string::npos) {
    size_t end = content.find("\"", namePos + 9);
    std::string n = content.substr(namePos + 9, end - (namePos + 9));
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &n[0], (int)n.size(), NULL, 0);
    name.assign(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &n[0], (int)n.size(), &name[0], size_needed);
  }

  // Extract scales
  auto extractDouble = [&](const std::string &key) -> double {
    size_t pos = content.find("\"" + key + "\": ");
    if (pos == std::string::npos)
      return 0.003;
    size_t end = content.find_first_of(",}", pos + 3 + key.length());
    std::string valStr =
        content.substr(pos + 3 + key.length(), end - (pos + 3 + key.length()));
    for (char &c : valStr)
      if (c == ',')
        c = '.'; // Fix existing comma-polluted files
    std::istringstream iss(valStr);
    iss.imbue(std::locale("C"));
    double d = 0.003;
    iss >> d;
    return d;
  };

  auto extractString = [&](const std::string &key) -> std::string {
    size_t pos = content.find("\"" + key + "\": \"");
    if (pos == std::string::npos)
      return "";
    size_t end = content.find("\"", pos + 4 + key.length());
    return content.substr(pos + 4 + key.length(),
                          end - (pos + 4 + key.length()));
  };

  sensitivityX = extractDouble("sensitivityX");
  if (sensitivityX <= 0.0)
    sensitivityX = 0.05;

  sensitivityY = extractDouble("sensitivityY");
  if (sensitivityY <= 0.0)
    sensitivityY = 0.05;

  fov = (float)extractDouble("fov");
  resolutionWidth = (int)extractDouble("resolutionWidth");
  resolutionHeight = (int)extractDouble("resolutionHeight");
  renderScale = (float)extractDouble("renderScale");

  roi_x = (int)extractDouble("roi_x");
  roi_y = (int)extractDouble("roi_y");
  roi_w = (int)extractDouble("roi_w");
  roi_h = (int)extractDouble("roi_h");
  target_color = (COLORREF)extractDouble("target_color");
  tolerance = (int)extractDouble("tolerance");
  if (tolerance <= 0)
    tolerance = 2;

  if (content.find("\"diveGlideMatch\"") != std::string::npos) {
    diveGlideMatch = (float)extractDouble("diveGlideMatch");
  } else {
    diveGlideMatch = 9.0f;
  }

  if (content.find("\"screenIndex\"") != std::string::npos) {
    screenIndex = (int)extractDouble("screenIndex");
  } else {
    screenIndex = 0;
  }

  // Load Keybinds
  keybinds.toggleMod = (UINT)extractDouble("kb_toggleMod");
  keybinds.toggleKey = (UINT)extractDouble("kb_toggleKey");
  keybinds.roiMod = (UINT)extractDouble("kb_roiMod");
  keybinds.roiKey = (UINT)extractDouble("kb_roiKey");
  keybinds.crossMod = (UINT)extractDouble("kb_crossMod");
  keybinds.crossKey = (UINT)extractDouble("kb_crossKey");
  keybinds.zeroMod = (UINT)extractDouble("kb_zeroMod");
  keybinds.zeroKey = (UINT)extractDouble("kb_zeroKey");

  // Fallback defaults for new files or legacy ones
  if (keybinds.toggleKey == 0) {
    keybinds.toggleMod = MOD_CONTROL;
    keybinds.toggleKey = 'U';
  }
  if (keybinds.roiKey == 0) {
    keybinds.roiMod = MOD_CONTROL;
    keybinds.roiKey = 'R';
  }
  if (keybinds.crossKey == 0) {
    keybinds.crossMod = 0;
    keybinds.crossKey = VK_F10;
  }
  if (keybinds.zeroKey == 0) {
    keybinds.zeroMod = MOD_CONTROL;
    keybinds.zeroKey = 'G';
  }

  // Load Crosshair (with defaults for legacy files)
  crossThickness = (float)extractDouble("crossThickness");
  if (crossThickness < 1.0f)
    crossThickness = 1.0f;

  crossColor = (COLORREF)extractDouble("crossColor");
  if (crossColor == 0)
    crossColor = RGB(255, 0, 0); // Default Red
  crossOffsetX = (float)extractDouble("crossOffsetX");
  crossOffsetY = (float)extractDouble("crossOffsetY");
  crossAngle = (float)extractDouble("crossAngle");
  bool pulseVal = extractDouble("crossPulse") > 0.5;
  crossPulse = pulseVal;
  showCrosshair = extractDouble("showCrosshair") > 0.5;
  if (content.find("\"showCrosshair\"") == std::string::npos)
    showCrosshair = true;

  // Load Presets Array (Manual Parser)
  crosshairPresets.clear();
  size_t arrPos = content.find("\"crosshairPresets\": [");
  if (arrPos != std::string::npos) {
    size_t endArr = content.find("]", arrPos);
    std::string arrContent = content.substr(arrPos, endArr - arrPos);
    size_t objPos = 0;
    while ((objPos = arrContent.find("{", objPos)) != std::string::npos) {
      size_t objEnd = arrContent.find("}", objPos);
      if (objEnd == std::string::npos)
        break;
      std::string obj = arrContent.substr(objPos, objEnd - objPos);

      CrosshairPreset cp;
      // Parse name
      size_t nP = obj.find("\"name\": \"");
      if (nP != std::string::npos) {
        size_t nE = obj.find("\"", nP + 9);
        std::string nStr = obj.substr(nP + 9, nE - (nP + 9));
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &nStr[0], (int)nStr.size(), NULL, 0);
        cp.name.assign(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &nStr[0], (int)nStr.size(), &cp.name[0], size_needed);
      }
      // Parse coords
      auto exD = [&](std::string k) -> float {
        size_t p = obj.find("\"" + k + "\": ");
        if (p == std::string::npos)
          return 0.0f;
        return (float)std::atof(obj.substr(p + k.length() + 3).c_str());
      };
      cp.offsetX = exD("x");
      cp.offsetY = exD("y");
      cp.angle = exD("a");
      cp.thickness = exD("t");
      if (cp.thickness < 1.0f)
        cp.thickness = 1.0f;
      cp.color = (COLORREF)exD("c");
      if (cp.color == 0)
        cp.color = RGB(255, 0, 0);
      cp.pulse = exD("p") > 0.5f;
      crosshairPresets.push_back(cp);
      objPos = objEnd + 1;
    }
  }

  // Ensure default if empty
  if (crosshairPresets.empty()) {
    CrosshairPreset def = {L"🎯 Screen Center", 0.0f, 0.0f, 0.0f, 1.0f,
                           RGB(255, 0, 0),      false};
    crosshairPresets.push_back(def);
  }

  return true;
}

bool Profile::Save(const std::wstring &path) {
  std::wstring tempPath = path + L".tmp";

  // Ensure file is not hidden before writing to avoid permission issues
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

  // Use narrow ofstream for consistent UTF-8 behavior
  std::ofstream f(tempPath, std::ios::trunc);
  if (!f.is_open())
    return false;

  // Use a stringstream with C locale for consistent decimal points
  std::stringstream ss;
  ss.imbue(std::locale("C"));

  auto toUtf8 = [](const std::wstring &wstr) -> std::string {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
  };

  ss << "{\n";
  ss << "  \"name\": \"" << toUtf8(name) << "\",\n";
  ss << "  \"sensitivityX\": " << sensitivityX << ",\n";
  ss << "  \"sensitivityY\": " << sensitivityY << ",\n";
  ss << "  \"fov\": " << fov << ",\n";
  ss << "  \"resolutionWidth\": " << resolutionWidth << ",\n";
  ss << "  \"resolutionHeight\": " << resolutionHeight << ",\n";
  ss << "  \"renderScale\": " << renderScale << ",\n";
  ss << "  \"roi_x\": " << roi_x << ",\n";
  ss << "  \"roi_y\": " << roi_y << ",\n";
  ss << "  \"roi_w\": " << roi_w << ",\n";
  ss << "  \"roi_h\": " << roi_h << ",\n";
  ss << "  \"target_color\": " << (unsigned long)target_color << ",\n";
  ss << "  \"tolerance\": " << tolerance << ",\n";
  ss << "  \"diveGlideMatch\": " << diveGlideMatch << ",\n";
  ss << "  \"screenIndex\": " << screenIndex << ",\n";
  ss << "  \"kb_toggleMod\": " << keybinds.toggleMod << ",\n";
  ss << "  \"kb_toggleKey\": " << keybinds.toggleKey << ",\n";
  ss << "  \"kb_roiMod\": " << keybinds.roiMod << ",\n";
  ss << "  \"kb_roiKey\": " << keybinds.roiKey << ",\n";
  ss << "  \"kb_crossMod\": " << keybinds.crossMod << ",\n";
  ss << "  \"kb_crossKey\": " << keybinds.crossKey << ",\n";
  ss << "  \"kb_zeroMod\": " << keybinds.zeroMod << ",\n";
  ss << "  \"kb_zeroKey\": " << keybinds.zeroKey << ",\n";

  ss << "  \"crossThickness\": " << std::fixed << std::setprecision(6) << crossThickness << ",\n";
  ss << "  \"showCrosshair\": " << (showCrosshair ? 1 : 0) << ",\n";
  ss << "  \"crossColor\": " << (unsigned long)crossColor << ",\n";
  ss << "  \"crossOffsetX\": " << crossOffsetX << ",\n";
  ss << "  \"crossOffsetY\": " << crossOffsetY << ",\n";
  ss << "  \"crossAngle\": " << crossAngle << ",\n";
  ss << "  \"crossPulse\": " << (crossPulse ? 1 : 0) << ",\n";

  ss << "  \"crosshairPresets\": [\n";
  for (size_t i = 0; i < crosshairPresets.size(); i++) {
    const auto &cp = crosshairPresets[i];
    ss << "    {\"name\": \"" << toUtf8(cp.name) << "\", \"x\": " << cp.offsetX
       << ", \"y\": " << cp.offsetY << ", \"a\": " << cp.angle
       << ", \"t\": " << cp.thickness << ", \"c\": "
       << (unsigned long)cp.color << ", \"p\": " << (cp.pulse ? 1 : 0)
       << "}";
    if (i < crosshairPresets.size() - 1)
      ss << ",";
    ss << "\n";
  }
  ss << "  ]\n";
  ss << "}";

  f << ss.str();
  f.close();

  // Atomic swap
  DeleteFileW(path.c_str());
  MoveFileW(tempPath.c_str(), path.c_str());

  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
  return true;
}

std::vector<Profile> GetProfiles(const std::wstring &directory) {
  std::vector<Profile> profiles;
  WIN32_FIND_DATAW findData;
  std::wstring searchPath = directory + L"/*.json";
  HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        Profile p;
        if (p.Load(directory + findData.cFileName)) {
          profiles.push_back(p);
        }
      }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
  }

  return profiles;
}
