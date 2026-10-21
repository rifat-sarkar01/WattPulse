#include <catch_amalgamated.hpp>

#include "core/power_model.h"

using wp::Calibration;
using wp::Domain;
using wp::ModelOutput;
using wp::PowerModel;
using wp::Provenance;
using wp::Sample;

namespace {
Sample mk(Domain d, double w, const char* id,
          Provenance p = Provenance::Measured) {
  Sample s;
  s.domain = d;
  s.watts = w;
  s.provenance = p;
  s.sourceId = id;
  return s;
}
}  // namespace

TEST_CASE("no sensors -> no total") {
  PowerModel model;
  ModelOutput out = model.evaluate({});
  REQUIRE_FALSE(out.total_watts.has_value());
}

TEST_CASE("cpu only -> estimated total") {
  PowerModel model;
  ModelOutput out = model.evaluate({mk(Domain::CpuPackage, 30.0, "emi:pkg")});
  REQUIRE(out.total_watts.has_value());
  REQUIRE(out.total_provenance == Provenance::Estimated);
  REQUIRE(out.sourceId == "estimate");
  REQUIRE(*out.total_watts > 30.0);  // base + efficiency always push above cpu alone
}

TEST_CASE("cpu + gpu -> estimated, other = total - cpu - gpu") {
  PowerModel model;
  ModelOutput out =
      model.evaluate({mk(Domain::CpuPackage, 40.0, "emi:pkg"), mk(Domain::Gpu, 25.0, "nvml:0")});
  REQUIRE(out.total_provenance == Provenance::Estimated);
  REQUIRE(*out.total_watts > 65.0);
  REQUIRE(*out.cpu_watts == Catch::Approx(40.0));
  REQUIRE(*out.gpu_watts == Catch::Approx(25.0));
  REQUIRE(*out.other_watts == Catch::Approx(*out.total_watts - 65.0));
}

TEST_CASE("battery discharge beats estimate and is measured") {
  PowerModel model;
  ModelOutput out = model.evaluate({mk(Domain::CpuPackage, 20.0, "emi:pkg"),
                                    mk(Domain::SystemTotal, 22.0, "battery")});
  REQUIRE(out.total_provenance == Provenance::Measured);
  REQUIRE(*out.total_watts == Catch::Approx(22.0));
  REQUIRE(out.sourceId == "battery");
}

TEST_CASE("smart plug beats everything") {
  PowerModel model;
  ModelOutput out = model.evaluate(
      {mk(Domain::SystemTotal, 88.0, "battery"), mk(Domain::SystemTotal, 110.0, "smartplug:0")});
  REQUIRE(out.total_provenance == Provenance::Measured);
  REQUIRE(*out.total_watts == Catch::Approx(110.0));
}

TEST_CASE("PSYS is a measured whole-system total") {
  PowerModel model;
  ModelOutput out = model.evaluate({mk(Domain::SystemTotal, 65.0, "emi:RAPL_Package0_PSYS")});
  REQUIRE(out.total_provenance == Provenance::Measured);
  REQUIRE(*out.total_watts == Catch::Approx(65.0));
}

TEST_CASE("calibrated base changes the estimate") {
  Calibration cal;
  cal.base_watts = 10.0;
  cal.calibrated = true;
  PowerModel low(cal);
  cal.base_watts = 80.0;
  PowerModel high(cal);
  const std::vector<Sample> in = {mk(Domain::CpuPackage, 30.0, "emi:pkg")};
  REQUIRE(*low.evaluate(in).total_watts < *high.evaluate(in).total_watts);
}

TEST_CASE("invalid samples are ignored") {
  PowerModel model;
  Sample bad = mk(Domain::CpuPackage, 20.0, "emi:pkg");
  bad.watts = std::nullopt;
  Sample negative = mk(Domain::Gpu, -5.0, "nvml:0");
  ModelOutput out = model.evaluate({bad, negative});
  REQUIRE_FALSE(out.cpu_watts.has_value());
  REQUIRE_FALSE(out.gpu_watts.has_value());
  REQUIRE_FALSE(out.total_watts.has_value());
}

TEST_CASE("cores used when package missing") {
  PowerModel model;
  ModelOutput out = model.evaluate({mk(Domain::CpuCores, 15.0, "emi:core0")});
  REQUIRE(out.cpu_watts.has_value());
  REQUIRE(*out.cpu_watts == Catch::Approx(15.0));
}
