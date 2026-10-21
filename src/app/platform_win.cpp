#include "app/platform_win.h"

#include <windows.h>

namespace wp {

bool acquire_single_instance(const wchar_t* mutex_name) {
  HANDLE h = CreateMutexW(nullptr, TRUE, mutex_name);
  if (!h) return true;  // cannot create -> do not block the app
  const bool first = (GetLastError() != ERROR_ALREADY_EXISTS);
  if (!first) CloseHandle(h);
  // first-run handle intentionally leaked for process lifetime
  return first;
}

namespace {
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"WattPulse";
}  // namespace

bool set_autostart(bool enable, const std::wstring& exe_path) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  bool ok;
  if (enable) {
    std::wstring cmd = L"\"" + exe_path + L"\" --run --silent";
    ok = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(cmd.c_str()),
                        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  } else {
    ok = RegDeleteValueW(key, kRunValue) == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND;
  }
  RegCloseKey(key);
  return ok;
}

bool autostart_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  const bool present = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) ==
                       ERROR_SUCCESS;
  RegCloseKey(key);
  return present;
}

}  // namespace wp
