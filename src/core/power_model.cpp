#include "core/power_model.h"

#include <cmath>

#include "core/estimator.h"

namespace wp {

double PowerModel::default_base(FormFactor form) {
  switch (form) {
    case FormFactor::Desktop: return 45.0;
    case FormFactor::Laptop: return 18.0;
    case FormFactor::Mini: return 12.0;
    case FormFactor::Unknown: return 30.0;
  }
  return 30.0;
}

ModelOutput PowerModel::evaluate(const std::vector<Sample>& samples) const {
  ModelOutput out;
  out.samples = samples;

  std::optional<double> measured_total;   // wall meter / UPS
  std::optional<double> battery_w;        // battery discharge (whole system)
  std::optional<double> cpu;              // package preferred
  std::optional<double> cores;
  std::optional<double> gpu;
  std::optional<double> psys;             // platform power (PSYS)

  for (const Sample& s : samples) {
    if (!s.watts.has_value() || !std::isfinite(*s.watts) || *s.watts < 0.0) continue;
    switch (s.domain) {
      case Domain::SystemTotal:
        if (s.sourceId.rfind("smartplug", 0) == 0 || s.sourceId.rfind("ups", 0) == 0) {
          measured_total = *s.watts;
          out.sourceId = s.sourceId;
        } else if (s.sourceId.rfind("battery", 0) == 0) {
          battery_w = *s.watts;  // discharge sample (charging/AC arrive as Domain::Battery)
        } else {
          psys = *s.watts;  // RAPL PSYS and other platform totals
        }
        break;
      case Domain::Battery:
        break;  // charge/AC state, not a wall reading
      case Domain::CpuPackage:
        cpu = *s.watts;
        break;
      case Domain::CpuCores:
        cores = *s.watts;
        break;
      case Domain::Gpu:
        gpu = *s.watts;
        break;
      case Domain::Other:
        psys = *s.watts;
        break;
      case Domain::Dram:
        break;
    }
  }

  const double cpu_w = cpu.value_or(cores.value_or(0.0));
  out.cpu_watts = cpu ? cpu : (cores ? cores : std::nullopt);
  out.gpu_watts = gpu;
  const double base = cal_.calibrated ? cal_.base_watts : default_base(form_);

  // Priority: wall meter/UPS > PSYS > battery discharge > estimate > none
  if (measured_total.has_value()) {
    out.total_watts = *measured_total;
    out.total_provenance = Provenance::Measured;
    if (out.sourceId.empty()) out.sourceId = "meter";
  } else if (psys.has_value()) {
    out.total_watts = *psys;
    out.total_provenance = Provenance::Measured;
    out.sourceId = "rapl:psys";
  } else if (battery_w.has_value()) {
    out.total_watts = *battery_w;
    out.total_provenance = Provenance::Measured;
    out.sourceId = "battery";
  } else if (cpu || gpu) {
    const double est = estimate_total(cpu_w, gpu.value_or(0.0), base);
    out.total_watts = est;
    out.total_provenance = Provenance::Estimated;
    out.sourceId = "estimate";
  } else {
    out.total_watts = std::nullopt;
    out.sourceId.clear();
  }

  if (out.total_watts.has_value()) {
    out.other_watts = std::max(0.0, *out.total_watts - cpu_w - gpu.value_or(0.0));
  }
  return out;
}

}  // namespace wp
