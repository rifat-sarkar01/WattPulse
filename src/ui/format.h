#pragma once

#include <optional>
#include <string>

namespace wp {

// Text formatting for the CLI readout (ported from v1 format.py).
std::string fmt_watts(std::optional<double> w);
std::string fmt_energy(double mwh);

}  // namespace wp
