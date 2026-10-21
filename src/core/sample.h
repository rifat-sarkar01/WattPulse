#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wp {

enum class Domain {
  SystemTotal,   // whole-machine draw (wall meter, battery discharge)
  CpuPackage,    // CPU package power (RAPL PKG / EMI)
  CpuCores,      // CPU cores only (RAPL PP0 / EMI core counters)
  Gpu,           // GPU board power (NVML / ADLX)
  Dram,          // DRAM power (RAPL DRAM)
  Battery,       // battery charge/discharge rate
  Other,         // modelled remainder (board, drives, fans)
};

inline const char* domain_name(Domain d) {
  switch (d) {
    case Domain::SystemTotal: return "total";
    case Domain::CpuPackage: return "cpu";
    case Domain::CpuCores: return "cores";
    case Domain::Gpu: return "gpu";
    case Domain::Dram: return "dram";
    case Domain::Battery: return "battery";
    case Domain::Other: return "other";
  }
  return "?";
}

enum class Provenance { Measured, Estimated };

struct Sample {
  Domain domain{};
  std::optional<double> watts;  // nullopt = no data right now
  Provenance provenance = Provenance::Measured;
  std::string sourceId;         // e.g. "nvml:0", "emi:RAPL_Package0_PKG", "battery"
};

// Cheap probe + never-throwing poll. Failure = just produce no samples.
struct ISensor {
  virtual ~ISensor() = default;
  virtual std::string_view name() const = 0;
  virtual bool available() = 0;
  virtual void poll(std::vector<Sample>& out) = 0;
};

}  // namespace wp
