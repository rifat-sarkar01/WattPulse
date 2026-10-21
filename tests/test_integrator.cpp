#include <catch_amalgamated.hpp>

#include "core/energy_integrator.h"

using wp::Domain;
using wp::EnergyIntegrator;
using wp::FLAG_ESTIMATED;
using wp::FLAG_MEASURED;
using wp::MinuteBucket;
using wp::Provenance;
using wp::Sample;

namespace {
Sample cpu(double w, Provenance p = Provenance::Measured) {
  Sample s;
  s.domain = Domain::CpuPackage;
  s.watts = w;
  s.provenance = p;
  s.sourceId = "test:cpu";
  return s;
}
Sample gpu(double w, Provenance p = Provenance::Measured) {
  Sample s;
  s.domain = Domain::Gpu;
  s.watts = w;
  s.provenance = p;
  s.sourceId = "test:gpu";
  return s;
}
Sample total(double w, Provenance p = Provenance::Measured) {
  Sample s;
  s.domain = Domain::SystemTotal;
  s.watts = w;
  s.provenance = p;
  s.sourceId = "test:total";
  return s;
}
}  // namespace

TEST_CASE("trapezoid integration of a constant wattage") {
  EnergyIntegrator integ(5.0);
  // 60 seconds at 100 W = 100 Wh = 100000 mWh, all in one minute bucket
  const double wall0 = 60.0 * 1000.0;  // aligned to a minute boundary
  for (int i = 0; i <= 60; ++i) {
    integ.tick(static_cast<double>(i), wall0 + static_cast<double>(i), {cpu(100.0)});
  }
  auto buckets = integ.drain();
  double mwh = 0.0;
  for (const auto& [ts, b] : buckets) mwh += b.mwh_total;
  // 100 W * 60 s = 6000 W·s = 1666.67 mWh
  REQUIRE(mwh == Catch::Approx(100.0 * 60.0 / 3600.0 * 1000.0));
  REQUIRE(buckets.begin()->second.flags & FLAG_MEASURED);
  REQUIRE_FALSE(buckets.begin()->second.flags & FLAG_ESTIMATED);
}

TEST_CASE("gap larger than max is not integrated") {
  EnergyIntegrator integ(5.0);
  integ.tick(0.0, 60.0, {cpu(100.0)});
  integ.tick(100.0, 160.0, {cpu(100.0)});  // 100 s gap: sleep/resume
  auto buckets = integ.drain();
  double mwh = 0.0;
  for (const auto& [ts, b] : buckets) mwh += b.mwh_total;
  REQUIRE(mwh == 0.0);
}

TEST_CASE("negative or zero dt resyncs without integrating") {
  EnergyIntegrator integ(5.0);
  integ.tick(10.0, 600.0, {cpu(50.0)});
  integ.tick(10.0, 600.0, {cpu(50.0)});  // dt == 0
  integ.tick(5.0, 595.0, {cpu(50.0)});   // non-monotonic
  auto buckets = integ.drain();
  double mwh = 0.0;
  for (const auto& [ts, b] : buckets) mwh += b.mwh_total;
  REQUIRE(mwh == 0.0);
}

TEST_CASE("nullopt watts are treated as zero contribution") {
  EnergyIntegrator integ(5.0);
  Sample s = cpu(10.0);
  integ.tick(0.0, 60.0, {s});
  s.watts = std::nullopt;
  integ.tick(1.0, 61.0, {s});
  auto buckets = integ.drain();
  double mwh = 0.0;
  for (const auto& [ts, b] : buckets) mwh += b.mwh_total;
  // one interval of (10 + 0)/2 W for 1 s
  REQUIRE(mwh == Catch::Approx(5.0 / 3600.0 * 1000.0));
}

TEST_CASE("estimated flag propagates") {
  EnergyIntegrator integ(5.0);
  integ.tick(0.0, 60.0, {cpu(20.0, Provenance::Estimated), gpu(10.0, Provenance::Estimated),
                          total(50.0, Provenance::Estimated)});
  integ.tick(1.0, 61.0, {cpu(20.0, Provenance::Estimated), gpu(10.0, Provenance::Estimated),
                         total(50.0, Provenance::Estimated)});
  auto buckets = integ.drain();
  REQUIRE_FALSE(buckets.empty());
  const auto& b = buckets.begin()->second;
  REQUIRE(b.flags & FLAG_ESTIMATED);
  REQUIRE(b.mwh_cpu > 0.0);
  REQUIRE(b.mwh_gpu > 0.0);
  REQUIRE(b.mwh_other > 0.0);
  REQUIRE(b.mwh_total == Catch::Approx(b.mwh_cpu + b.mwh_gpu + b.mwh_other));
}

TEST_CASE("without a total, cpu+gpu form the running total") {
  EnergyIntegrator integ(10.0);  // dt of 6 s must fit inside the gap cap
  integ.tick(0.0, 60.0, {cpu(30.0), gpu(20.0)});
  integ.tick(6.0, 66.0, {cpu(30.0), gpu(20.0)});
  auto buckets = integ.drain();
  double total = 0.0, cpu_mwh = 0.0, gpu_mwh = 0.0;
  for (const auto& [ts, b] : buckets) {
    total += b.mwh_total;
    cpu_mwh += b.mwh_cpu;
    gpu_mwh += b.mwh_gpu;
  }
  REQUIRE(cpu_mwh == Catch::Approx(30.0 * 6.0 / 3600.0 * 1000.0));
  REQUIRE(gpu_mwh == Catch::Approx(20.0 * 6.0 / 3600.0 * 1000.0));
  REQUIRE(total == Catch::Approx(cpu_mwh + gpu_mwh));
}

TEST_CASE("cpu package preferred over cores") {
  EnergyIntegrator integ(5.0);
  Sample pkg = cpu(40.0);
  Sample cores;
  cores.domain = Domain::CpuCores;
  cores.watts = 10.0;
  cores.provenance = Provenance::Measured;
  cores.sourceId = "test:cores";
  integ.tick(0.0, 60.0, {pkg, cores});
  integ.tick(2.0, 62.0, {pkg, cores});
  auto buckets = integ.drain();
  double cpu_mwh = 0.0;
  for (const auto& [ts, b] : buckets) cpu_mwh += b.mwh_cpu;
  REQUIRE(cpu_mwh == Catch::Approx(40.0 * 2.0 / 3600.0 * 1000.0));
}

TEST_CASE("minute buckets split across a minute boundary") {
  EnergyIntegrator integ(5.0);
  // wall 119.5 -> 120.5 (seconds) crosses the 120 s minute boundary;
  // the whole interval lands in the ending minute (key = unix seconds)
  integ.tick(0.0, 119.5, {cpu(10.0)});
  integ.tick(1.0, 120.5, {cpu(10.0)});
  auto buckets = integ.drain();
  REQUIRE(buckets.size() == 1);
  REQUIRE(buckets.begin()->first == 120);
}
