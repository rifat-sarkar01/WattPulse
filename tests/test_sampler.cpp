#include <catch_amalgamated.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

#include "core/power_model.h"
#include "core/sampler.h"

namespace {

class FakeSensor : public wp::ISensor {
 public:
  std::string_view name() const override { return "fake"; }
  bool available() override { return available_; }
  void poll(std::vector<wp::Sample>& out) override {
    if (throw_) throw std::runtime_error("sensor exploded");
    if (!available_) return;
    wp::Sample cpu;
    cpu.domain = wp::Domain::CpuPackage;
    cpu.watts = cpu_watts_;
    cpu.provenance = wp::Provenance::Measured;
    cpu.sourceId = "fake:cpu";
    out.push_back(cpu);

    wp::Sample gpu;
    gpu.domain = wp::Domain::Gpu;
    gpu.watts = gpu_watts_;
    gpu.provenance = wp::Provenance::Measured;
    gpu.sourceId = "fake:gpu";
    out.push_back(gpu);
  }

  void set_available(bool value) { available_ = value; }
  void set_throw(bool value) { throw_ = value; }

 private:
  bool available_ = true;
  bool throw_ = false;
  double cpu_watts_ = 40.0;
  double gpu_watts_ = 20.0;
};

}  // namespace

TEST_CASE("sampler integrates an estimated total and exposes drainable buckets") {
  std::vector<std::unique_ptr<wp::ISensor>> sensors;
  sensors.push_back(std::make_unique<FakeSensor>());
  wp::Calibration cal;
  cal.base_watts = 40.0;
  cal.calibrated = true;
  wp::PowerModel model(cal, wp::FormFactor::Desktop);
  wp::Sampler sampler(std::move(sensors), model, 1.0, 5.0);

  sampler.poll_once(0.0, 60.0);
  auto snap = sampler.snapshot();
  REQUIRE(snap.ticks == 1);
  REQUIRE(snap.any_sensor_ok);
  REQUIRE(snap.sensors.size() == 1);
  REQUIRE(snap.sensors[0].name == "fake");
  REQUIRE(snap.sensors[0].available);
  REQUIRE(snap.sensors[0].producing);
  REQUIRE(snap.model.total_watts.has_value());
  REQUIRE(snap.model.total_provenance == wp::Provenance::Estimated);

  sampler.poll_once(1.0, 61.0);
  snap = sampler.snapshot();
  REQUIRE(snap.pending.size() == 1);
  const auto& bucket = snap.pending.begin()->second;
  REQUIRE(snap.pending.begin()->first == 60);
  REQUIRE(bucket.mwh_total > 0.0);
  REQUIRE(bucket.mwh_estimated == Catch::Approx(bucket.mwh_total));
  REQUIRE(bucket.flags & wp::FLAG_ESTIMATED);

  const auto drained = sampler.drain_pending();
  REQUIRE(drained.size() == 1);
  REQUIRE(sampler.snapshot().pending.empty());

  sampler.restore_pending(drained);
  REQUIRE(sampler.snapshot().pending.size() == 1);
}

TEST_CASE("a failing or unavailable sensor degrades without throwing") {
  std::vector<std::unique_ptr<wp::ISensor>> sensors;
  auto owned = std::make_unique<FakeSensor>();
  FakeSensor* fake = owned.get();
  sensors.push_back(std::move(owned));
  wp::Sampler sampler(std::move(sensors), wp::PowerModel());

  sampler.poll_once(0.0, 60.0);
  REQUIRE(sampler.snapshot().any_sensor_ok);

  fake->set_throw(true);
  REQUIRE_NOTHROW(sampler.poll_once(1.0, 61.0));
  auto snap = sampler.snapshot();
  REQUIRE_FALSE(snap.any_sensor_ok);
  REQUIRE_FALSE(snap.model.total_watts.has_value());
  REQUIRE_FALSE(snap.sensors[0].available);
  REQUIRE_FALSE(snap.sensors[0].producing);

  fake->set_throw(false);
  fake->set_available(false);
  REQUIRE_NOTHROW(sampler.poll_once(2.0, 62.0));
  REQUIRE_FALSE(sampler.snapshot().any_sensor_ok);
  REQUIRE_FALSE(sampler.snapshot().sensors[0].available);
}
