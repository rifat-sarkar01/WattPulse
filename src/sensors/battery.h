#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/sample.h"

namespace wp {

// Battery power via IOCTL_BATTERY_QUERY_STATUS (same source as v1).
// Discharge rate is real whole-system draw; mapped to Domain::SystemTotal.
class BatterySensor : public ISensor {
 public:
  BatterySensor();
  ~BatterySensor() override;

  std::string_view name() const override { return "battery"; }
  bool available() override;
  void poll(std::vector<Sample>& out) override;

  int battery_count() const { return static_cast<int>(devs_.size()); }

 private:
  struct Device {
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD tag = 0;
  };

  void scan();
  void close_all();

  std::vector<Device> devs_;
  bool dirty_ = false;
  double t_scan_ = 0.0;
};

}  // namespace wp
