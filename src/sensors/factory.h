#pragma once

#include <memory>
#include <vector>

#include "app/settings.h"
#include "core/sample.h"

namespace wp {

// Build the Tier 0 sensor set for this machine. Never throws; unavailable
// sensors are simply included but fail their available() probe.
std::vector<std::unique_ptr<ISensor>> make_sensors(const AppSettings& settings);

// Just the demo sensor.
std::vector<std::unique_ptr<ISensor>> make_demo_sensors();

}  // namespace wp
