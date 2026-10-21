#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "core/sample.h"

namespace wp {

// NVML reports milliwatts.
inline double milliwatts_to_watts(std::uint64_t milliwatts) {
  return static_cast<double>(milliwatts) / 1000.0;
}

// Windows Energy Meter counters are seen as milliwatts on current drivers.
inline std::optional<double> emi_raw_to_watts(double raw, double scale) {
  if (!std::isfinite(raw) || raw < 0.0) return std::nullopt;
  if (!std::isfinite(scale) || scale <= 0.0) return std::nullopt;
  return raw * scale;
}

inline std::optional<Domain> emi_domain_from_path(std::wstring_view path) {
  if (path.find(L"_PKG)\\Power") != std::wstring_view::npos) return Domain::CpuPackage;
  if (path.find(L"_PSYS)\\Power") != std::wstring_view::npos) return Domain::SystemTotal;
  return std::nullopt;
}

// Battery STATUS flags: bit 1 = discharging, bit 2 = charging.
inline std::optional<Sample> make_battery_sample(std::uint32_t power_state, std::int64_t rate_mw,
                                                 int answered) {
  constexpr std::uint32_t PS_DISCHARGING = 0x2;
  constexpr std::uint32_t PS_CHARGING = 0x4;
  if (answered <= 0) return std::nullopt;

  const bool charging = (power_state & PS_CHARGING) != 0;
  const bool discharging = (power_state & PS_DISCHARGING) != 0;
  if ((charging || discharging) && rate_mw != 0) {
    Sample s;
    s.watts = static_cast<double>(std::llabs(rate_mw)) / 1000.0;
    s.provenance = Provenance::Measured;
    if (discharging) {
      s.domain = Domain::SystemTotal;
      s.sourceId = "battery";
    } else {
      s.domain = Domain::Battery;
      s.sourceId = "battery:charge";
    }
    return s;
  }
  if (!charging && !discharging) {
    Sample s;
    s.domain = Domain::Battery;
    s.watts = 0.0;
    s.provenance = Provenance::Measured;
    s.sourceId = "battery:ac";
    return s;
  }
  return std::nullopt;
}

}  // namespace wp
