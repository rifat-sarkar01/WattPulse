#include "core/estimator.h"

#include <algorithm>

namespace wp {

double psu_efficiency(double input_watts) {
  // Piecewise-linear stand-in for an 80 PLUS Gold efficiency curve.
  // Very low and very high loads are less efficient; the middle is ~0.90.
  if (input_watts <= 0.0) return 0.85;
  if (input_watts < 100.0) return 0.82 + 0.08 * (input_watts / 100.0);   // 0.82..0.90
  if (input_watts < 300.0) return 0.90 + 0.02 * ((input_watts - 100.0) / 200.0);  // ..0.92
  if (input_watts < 500.0) return 0.92;                                   // peak band
  return std::max(0.86, 0.92 - 0.04 * ((input_watts - 500.0) / 500.0));
}

double estimate_total(double cpu_watts, double gpu_watts, double base_watts) {
  const double dc_side = std::max(0.0, cpu_watts) + std::max(0.0, gpu_watts) +
                         std::max(0.0, base_watts);
  const double eta = psu_efficiency(dc_side);
  return dc_side / eta;
}

double calibrate_base(double wall_watts, double cpu_watts, double gpu_watts) {
  // Invert: wall = (cpu + gpu + base) / η(wall)  =>  base = wall*η - cpu - gpu
  const double eta = psu_efficiency(wall_watts);
  const double base = wall_watts * eta - std::max(0.0, cpu_watts) - std::max(0.0, gpu_watts);
  return std::max(0.0, base);
}

}  // namespace wp
