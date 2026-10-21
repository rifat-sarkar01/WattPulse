#pragma once

#include <cstdint>

#include "core/sample.h"

namespace wp {

// Deterministic synthetic data for --demo / tests. Nothing is saved.
class DemoSensor : public ISensor {
 public:
  explicit DemoSensor(std::uint32_t seed = 1) : state_(seed ? seed : 1) {}

  std::string_view name() const override { return "demo"; }
  bool available() override { return true; }
  void poll(std::vector<Sample>& out) override;

 private:
  double next01();
  std::uint32_t state_;
};

}  // namespace wp
