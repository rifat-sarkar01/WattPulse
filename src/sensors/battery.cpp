#include "sensors/battery.h"

#include <setupapi.h>

#include <chrono>
#include <cmath>

namespace wp {

namespace {

constexpr DWORD IOCTL_BATTERY_QUERY_TAG = 0x294040;
constexpr DWORD IOCTL_BATTERY_QUERY_STATUS = 0x29404C;
constexpr LONG UNKNOWN_RATE = LONG(0x80000000);
constexpr ULONG PS_DISCHARGING = 0x2;
constexpr ULONG PS_CHARGING = 0x4;
constexpr double RESCAN_AFTER_ERROR = 5.0;
constexpr double RESCAN_WHEN_EMPTY = 60.0;

GUID GUID_BATTERY_INTERFACE = {0x72631E54, 0x78A4, 0x11D0,
                               {0xBC, 0xF7, 0x00, 0xAA, 0x00, 0xB7, 0xB3, 0x2A}};

struct BATTERY_WAIT_STATUS {
  ULONG BatteryTag = 0;
  ULONG Timeout = 0;
  ULONG PowerState = 0;
  ULONG LowCapacity = 0;
  ULONG HighCapacity = 0;
};

struct BATTERY_STATUS {
  ULONG PowerState = 0;
  ULONG Capacity = 0;
  ULONG Voltage = 0;
  LONG Rate = 0;
};

double monotonic() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

BatterySensor::BatterySensor() { scan(); }
BatterySensor::~BatterySensor() { close_all(); }

void BatterySensor::close_all() {
  for (Device& d : devs_) {
    if (d.handle != INVALID_HANDLE_VALUE) CloseHandle(d.handle);
  }
  devs_.clear();
}

void BatterySensor::scan() {
  close_all();
  dirty_ = false;
  t_scan_ = monotonic();

  HDEVINFO hdev = SetupDiGetClassDevsW(&GUID_BATTERY_INTERFACE, nullptr, nullptr,
                                       DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
  if (hdev == INVALID_HANDLE_VALUE) return;

  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA ifdata;
    ifdata.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    if (!SetupDiEnumDeviceInterfaces(hdev, nullptr, &GUID_BATTERY_INTERFACE, index, &ifdata)) {
      break;
    }
    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(hdev, &ifdata, nullptr, 0, &need, nullptr);
    if (need == 0) continue;
    std::vector<BYTE> buf(need);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(hdev, &ifdata, detail, need, nullptr, nullptr)) {
      continue;
    }
    HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) continue;

    ULONG tag = 0, zero = 0, n = 0;
    if (DeviceIoControl(h, IOCTL_BATTERY_QUERY_TAG, &zero, sizeof(zero), &tag, sizeof(tag), &n,
                        nullptr) &&
        tag != 0) {
      devs_.push_back({h, tag});
    } else {
      CloseHandle(h);
    }
  }
  SetupDiDestroyDeviceInfoList(hdev);
}

bool BatterySensor::available() {
  const double gap = monotonic() - t_scan_;
  if ((dirty_ && gap > RESCAN_AFTER_ERROR) || (devs_.empty() && gap > RESCAN_WHEN_EMPTY)) {
    scan();
  }
  return !devs_.empty();
}

void BatterySensor::poll(std::vector<Sample>& out) {
  SYSTEM_POWER_STATUS sps{};
  int percent = -1;
  if (GetSystemPowerStatus(&sps) && sps.BatteryLifePercent <= 100) {
    percent = sps.BatteryLifePercent;
  }

  if (!available()) return;

  ULONG flags = 0;
  LONG mw = 0;
  bool known = false;
  int answered = 0;
  for (Device& d : devs_) {
    BATTERY_WAIT_STATUS wait;
    wait.BatteryTag = d.tag;
    BATTERY_STATUS st{};
    DWORD n = 0;
    if (!DeviceIoControl(d.handle, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait), &st,
                         sizeof(st), &n, nullptr)) {
      dirty_ = true;
      continue;
    }
    ++answered;
    flags |= st.PowerState;
    if (st.Rate != UNKNOWN_RATE) {
      mw += (st.Rate < 0) ? -st.Rate : st.Rate;
      known = true;
    }
  }
  (void)percent;
  if (answered == 0) return;  // nobody answered: claim nothing

  const bool charging = (flags & PS_CHARGING) != 0;
  const bool discharging = (flags & PS_DISCHARGING) != 0;

  if (discharging && known && mw > 0) {
    Sample s;
    s.domain = Domain::SystemTotal;  // discharge = whole-system draw
    s.watts = static_cast<double>(mw) / 1000.0;
    s.provenance = Provenance::Measured;
    s.sourceId = "battery";
    out.push_back(std::move(s));
  } else if (charging && known && mw > 0) {
    Sample s;
    s.domain = Domain::Battery;
    s.watts = static_cast<double>(mw) / 1000.0;
    s.provenance = Provenance::Measured;
    s.sourceId = "battery:charge";
    out.push_back(std::move(s));
  } else if (!charging && !discharging) {
    Sample s;
    s.domain = Domain::Battery;
    s.watts = 0.0;
    s.provenance = Provenance::Measured;
    s.sourceId = "battery:ac";
    out.push_back(std::move(s));
  }
}

}  // namespace wp
