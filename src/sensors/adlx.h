#pragma once

#include <string>
#include <vector>

#include "core/sample.h"

namespace wp {

// AMD GPU power via ADLX (amdadlx64.dll, shipped with the driver).
// Header-only vendored SDK; the DLL is loaded dynamically.
class AdlxSensor : public ISensor {
 public:
  AdlxSensor();
  ~AdlxSensor() override;

  std::string_view name() const override { return "adlx"; }
  bool available() override;
  void poll(std::vector<Sample>& out) override;

  int gpu_count() const { return static_cast<int>(gpu_count_); }

 private:
  bool init();
  void shutdown();

  void* module_ = nullptr;
  void* system_ = nullptr;       // adlx::IADLXSystem*
  void* perf_services_ = nullptr;  // adlx::IADLXPerformanceMonitoringServices*
  unsigned gpu_count_ = 0;
  bool ok_ = false;
  bool tried_ = false;
};

}  // namespace wp
