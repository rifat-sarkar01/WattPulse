#pragma once

#include <string>

#include "core/power_model.h"

namespace wp {

struct AppSettings {
  double base_watts = 45.0;
  bool calibrated = false;
  bool autostart = false;
  std::string theme = "system";  // system | light | dark (graphite = legacy dark)
  double emi_scale = 0.001;        // EMI PDH counters report milliwatts
  std::string form_factor = "desktop";
};

// %APPDATA%\WattPulse\settings.ini (key=value). Missing file -> defaults.
AppSettings load_settings(const std::string& path);
bool save_settings(const std::string& path, const AppSettings& s);

// %APPDATA%\WattPulse (created if needed). Falls back to "." .
std::string data_dir();

FormFactor parse_form_factor(const std::string& s);
std::string form_factor_name(FormFactor f);

}  // namespace wp
