#include "app/settings.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace wp {

namespace {

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

}  // namespace

FormFactor parse_form_factor(const std::string& s) {
  if (s == "laptop") return FormFactor::Laptop;
  if (s == "mini") return FormFactor::Mini;
  if (s == "unknown") return FormFactor::Unknown;
  return FormFactor::Desktop;
}

std::string form_factor_name(FormFactor f) {
  switch (f) {
    case FormFactor::Laptop: return "laptop";
    case FormFactor::Mini: return "mini";
    case FormFactor::Unknown: return "unknown";
    case FormFactor::Desktop: return "desktop";
  }
  return "desktop";
}

std::string data_dir() {
  char buf[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
  std::string base = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string(".");
  std::string dir = base + "\\WattPulse";
  if (GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
    const std::string legacy = base + "\\Wattly";
    if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
      MoveFileExA(legacy.c_str(), dir.c_str(), 0);
    }
  }
  CreateDirectoryA(dir.c_str(), nullptr);
  return dir;
}

AppSettings load_settings(const std::string& path) {
  AppSettings s;
  s.base_watts = PowerModel::default_base(FormFactor::Desktop);
  std::ifstream in(path);
  if (!in) return s;
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string val = trim(line.substr(eq + 1));
    if (key == "base_watts") {
      try { s.base_watts = std::stod(val); } catch (...) {}
    } else if (key == "calibrated") {
      s.calibrated = (val == "1" || val == "true");
    } else if (key == "autostart") {
      s.autostart = (val == "1" || val == "true");
    } else if (key == "theme") {
      s.theme = val;
    } else if (key == "emi_scale") {
      try { s.emi_scale = std::stod(val); } catch (...) {}
    } else if (key == "form_factor") {
      s.form_factor = val;
    }
  }
  return s;
}

bool save_settings(const std::string& path, const AppSettings& s) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << "base_watts=" << s.base_watts << "\n"
      << "calibrated=" << (s.calibrated ? "true" : "false") << "\n"
      << "autostart=" << (s.autostart ? "true" : "false") << "\n"
      << "theme=" << s.theme << "\n"
      << "emi_scale=" << s.emi_scale << "\n"
      << "form_factor=" << s.form_factor << "\n";
  return static_cast<bool>(out);
}

}  // namespace wp
