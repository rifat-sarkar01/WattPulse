#pragma once

#include <string>
#include <vector>

#include "core/sample.h"

namespace wp {

// NVIDIA GPU power via nvml.dll (loaded dynamically; absent on AMD/Intel boxes).
class NvmlSensor : public ISensor {
 public:
  NvmlSensor();
  ~NvmlSensor() override;

  std::string_view name() const override { return "nvml"; }
  bool available() override;
  void poll(std::vector<Sample>& out) override;

 private:
  bool init();
  void shutdown();

  void* module_ = nullptr;      // HMODULE
  void* runtime_ = nullptr;     // opaque nvmlDevice_t list holder
  unsigned device_count_ = 0;
  bool ok_ = false;
  bool tried_ = false;
};

}  // namespace wp
