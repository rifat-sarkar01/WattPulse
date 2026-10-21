#include "sensors/demo.h"

#include <cmath>

namespace wp {

double DemoSensor::next01() {
  // xorshift32
  std::uint32_t x = state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state_ = x;
  return static_cast<double>(x) / 4294967296.0;
}

void DemoSensor::poll(std::vector<Sample>& out) {
  const double phase = next01();
  const double cpu = 12.0 + 25.0 * (0.5 + 0.5 * std::sin(phase * 12.0)) + next01() * 6.0;
  const double gpu = 5.0 + 15.0 * (0.5 + 0.5 * std::sin(phase * 7.0 + 1.3)) + next01() * 3.0;

  Sample c;
  c.domain = Domain::CpuPackage;
  c.watts = cpu;
  c.provenance = Provenance::Measured;
  c.sourceId = "demo:cpu";
  out.push_back(c);

  Sample g;
  g.domain = Domain::Gpu;
  g.watts = gpu;
  g.provenance = Provenance::Measured;
  g.sourceId = "demo:gpu";
  out.push_back(g);
}

}  // namespace wp
