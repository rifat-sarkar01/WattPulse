#pragma once

#include <windows.h>
#include <pdh.h>

#include <string>
#include <vector>

#include "core/sample.h"

namespace wp {

// Windows Energy Meter (PDH "Energy Meter" counter set) — Intel RAPL-class
// data without a driver. Reports package (PKG) and PSYS when present.
// Counter values observed as milliwatts; scale via constructor (default 0.001).
class EmiSensor : public ISensor {
 public:
  explicit EmiSensor(double scale = 0.001);
  ~EmiSensor() override;

  std::string_view name() const override { return "emi"; }
  bool available() override;
  void poll(std::vector<Sample>& out) override;

  int counter_count() const { return static_cast<int>(paths_.size()); }

 private:
  bool open_queries();
  void close_queries();

  double scale_;
  bool tried_ = false;
  bool ok_ = false;
  std::vector<std::wstring> paths_;
  std::vector<Domain> domains_;
  std::vector<HCOUNTER> sources_;
  HQUERY query_ = nullptr;
};

}  // namespace wp
