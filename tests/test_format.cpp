#include <catch_amalgamated.hpp>

#include "ui/format.h"

TEST_CASE("watts formatting") {
  REQUIRE(wp::fmt_watts(std::nullopt) == "\xE2\x80\x94");
  REQUIRE(wp::fmt_watts(0.0) == "0.0");
  REQUIRE(wp::fmt_watts(42.34) == "42.3");
  REQUIRE(wp::fmt_watts(99.95) == "100.0");
  REQUIRE(wp::fmt_watts(123.4) == "123");
}

TEST_CASE("energy formatting") {
  REQUIRE(wp::fmt_energy(100.0) == "0.1 Wh");
  REQUIRE(wp::fmt_energy(50000.0) == "50.0 Wh");
  REQUIRE(wp::fmt_energy(150000.0) == "150 Wh");
  REQUIRE(wp::fmt_energy(5400000.0) == "5.40 kWh");
}
