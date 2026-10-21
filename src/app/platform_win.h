#pragma once

#include <string>

namespace wp {

// Per-user single instance (Local\ namespace). Returns false if another copy runs.
bool acquire_single_instance(const wchar_t* mutex_name);

// Start-with-Windows via HKCU Run key. Returns false on registry failure.
bool set_autostart(bool enable, const std::wstring& exe_path);
bool autostart_enabled();

}  // namespace wp
