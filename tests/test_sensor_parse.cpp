#include <catch_amalgamated.hpp>

#include <cstdint>
#include <limits>

#include "sensors/parse.h"

using wp::Domain;
using wp::Provenance;

TEST_CASE("nvml milliwatts are converted to watts") {
  REQUIRE(wp::milliwatts_to_watts(21500) == Catch::Approx(21.5));
  REQUIRE(wp::milliwatts_to_watts(0) == Catch::Approx(0.0));
}

TEST_CASE("emi raw counters are scaled and invalid values rejected") {
  REQUIRE(wp::emi_raw_to_watts(35000.0, 0.001) == Catch::Approx(35.0));
  REQUIRE_FALSE(wp::emi_raw_to_watts(-1.0, 0.001).has_value());
  REQUIRE_FALSE(wp::emi_raw_to_watts(100.0, 0.0).has_value());
  REQUIRE_FALSE(wp::emi_raw_to_watts(std::numeric_limits<double>::quiet_NaN(), 0.001)
                    .has_value());
}

TEST_CASE("emi counter paths map to the right domain") {
  REQUIRE(wp::emi_domain_from_path(L"\\Energy Meter(RAPL_Package0_PKG)\\Power") ==
          Domain::CpuPackage);
  REQUIRE(wp::emi_domain_from_path(L"\\Energy Meter(RAPL_Package0_PSYS)\\Power") ==
          Domain::SystemTotal);
  REQUIRE_FALSE(wp::emi_domain_from_path(L"\\Energy Meter(RAPL_Package0_Core0)\\Power")
                    .has_value());
}

TEST_CASE("battery discharge reports whole-system watts regardless of rate sign") {
  for (std::int64_t rate : {-12000LL, 12000LL}) {
    const auto s = wp::make_battery_sample(0x2, rate, 1);
    REQUIRE(s.has_value());
    REQUIRE(s->domain == Domain::SystemTotal);
    REQUIRE(s->sourceId == "battery");
    REQUIRE(s->provenance == Provenance::Measured);
    REQUIRE(*s->watts == Catch::Approx(12.0));
  }
}

TEST_CASE("battery charging and AC states are not whole-system totals") {
  const auto charging = wp::make_battery_sample(0x4, -8000, 1);
  REQUIRE(charging.has_value());
  REQUIRE(charging->domain == Domain::Battery);
  REQUIRE(charging->sourceId == "battery:charge");
  REQUIRE(*charging->watts == Catch::Approx(8.0));

  const auto ac = wp::make_battery_sample(0x0, 0, 1);
  REQUIRE(ac.has_value());
  REQUIRE(ac->domain == Domain::Battery);
  REQUIRE(ac->sourceId == "battery:ac");
  REQUIRE(*ac->watts == Catch::Approx(0.0));
}

TEST_CASE("battery unknown rate or a failed query yields no sample") {
  REQUIRE_FALSE(wp::make_battery_sample(0x2, 0, 1).has_value());
  REQUIRE_FALSE(wp::make_battery_sample(0x2, -5000, 0).has_value());
}
