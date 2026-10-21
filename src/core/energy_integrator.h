#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "core/sample.h"

namespace wp {

// One per-minute bucket of energy, in milliwatt-hours.
struct MinuteBucket {
  double mwh_total = 0.0;
  double mwh_cpu = 0.0;
  double mwh_gpu = 0.0;
  double mwh_other = 0.0;
  double mwh_estimated = 0.0;  // subset of mwh_total that was estimated
  // bit0 = any estimated energy in this minute, bit1 = any measured
  std::uint32_t flags = 0;
};

inline constexpr std::uint32_t FLAG_ESTIMATED = 1u;
inline constexpr std::uint32_t FLAG_MEASURED = 2u;

// Trapezoidal integrator: watts per domain -> per-minute mWh buckets.
// Pure logic, no threads, no clock of its own: the caller passes monotonic
// seconds and wall-clock seconds per tick.
class EnergyIntegrator {
 public:
  explicit EnergyIntegrator(double max_gap_seconds = 5.0);

  // Integrate the interval ending at (mono_t, wall_t) using last tick's
  // watts (trapezoid with the previous sample). Gaps > max_gap are dropped.
  void tick(double mono_t, double wall_t, const std::vector<Sample>& samples);

  // Move out all completed pending buckets (caller writes them to storage).
  std::map<std::int64_t, MinuteBucket> drain();

  // Add already-drained buckets back (used to restore in-memory pending).
  void restore(const std::map<std::int64_t, MinuteBucket>& rows);

  double max_gap() const { return max_gap_; }

 private:
  double max_gap_;
  bool has_last_ = false;
  double last_mono_ = 0.0;
  std::map<Domain, double> last_watts_;
  std::map<std::int64_t, MinuteBucket> pending_;
};

}  // namespace wp
