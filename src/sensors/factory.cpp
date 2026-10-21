#include "sensors/factory.h"

#include "sensors/adlx.h"
#include "sensors/battery.h"
#include "sensors/demo.h"
#include "sensors/emi.h"
#include "sensors/nvml.h"

namespace wp {

std::vector<std::unique_ptr<ISensor>> make_sensors(const AppSettings& settings) {
  std::vector<std::unique_ptr<ISensor>> sensors;
  sensors.push_back(std::make_unique<BatterySensor>());
  sensors.push_back(std::make_unique<EmiSensor>(settings.emi_scale));
  sensors.push_back(std::make_unique<NvmlSensor>());
  sensors.push_back(std::make_unique<AdlxSensor>());
  return sensors;
}

std::vector<std::unique_ptr<ISensor>> make_demo_sensors() {
  std::vector<std::unique_ptr<ISensor>> sensors;
  sensors.push_back(std::make_unique<DemoSensor>());
  return sensors;
}

}  // namespace wp
