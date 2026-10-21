#include <catch_amalgamated.hpp>

#include "core/estimator.h"

TEST_CASE("efficiency is in a sane band") {
  for (double w : {0.0, 50.0, 100.0, 200.0, 400.0, 600.0, 900.0}) {
    const double eta = wp::psu_efficiency(w);
    REQUIRE(eta >= 0.80);
    REQUIRE(eta <= 0.95);
  }
}

TEST_CASE("estimate is monotonic in cpu load") {
  double prev = 0.0;
  for (double cpu = 0.0; cpu <= 120.0; cpu += 10.0) {
    const double est = wp::estimate_total(cpu, 0.0, 40.0);
    REQUIRE(est >= prev);
    prev = est;
  }
}

TEST_CASE("estimate is monotonic in gpu load") {
  double prev = 0.0;
  for (double gpu = 0.0; gpu <= 300.0; gpu += 20.0) {
    const double est = wp::estimate_total(20.0, gpu, 40.0);
    REQUIRE(est >= prev);
    prev = est;
  }
}

TEST_CASE("calibration round-trip recovers the base") {
  const double cpu = 35.0, gpu = 18.0, real_base = 42.0;
  const double wall = wp::estimate_total(cpu, gpu, real_base);
  const double recovered = wp::calibrate_base(wall, cpu, gpu);
  REQUIRE(recovered == Catch::Approx(real_base).margin(0.5));
}

TEST_CASE("negative base is clamped to zero") {
  REQUIRE(wp::calibrate_base(10.0, 50.0, 20.0) == 0.0);
}
