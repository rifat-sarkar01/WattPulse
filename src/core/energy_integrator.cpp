#include "core/energy_integrator.h"

#include <cmath>

namespace wp {

namespace {

double watts_at(const std::map<Domain, double>& m, Domain d) {
  auto it = m.find(d);
  return it == m.end() ? 0.0 : it->second;
}

double integrate(double w1, double w2, double dt) {
  // W * s -> mWh
  return (w1 + w2) * 0.5 * dt / 3600.0 * 1000.0;
}

}  // namespace

EnergyIntegrator::EnergyIntegrator(double max_gap_seconds)
    : max_gap_(max_gap_seconds) {}

void EnergyIntegrator::tick(double mono_t, double wall_t,
                            const std::vector<Sample>& samples) {
  std::map<Domain, double> now;
  std::map<Domain, Provenance> prov;
  for (const Sample& s : samples) {
    if (s.watts.has_value() && std::isfinite(*s.watts) && *s.watts >= 0.0) {
      now[s.domain] = *s.watts;
      prov[s.domain] = s.provenance;
    }
  }

  if (!has_last_) {
    has_last_ = true;
    last_mono_ = mono_t;
    last_watts_ = std::move(now);
    return;
  }

  const double dt = mono_t - last_mono_;
  if (!(dt > 0.0)) {
    last_mono_ = mono_t;
    last_watts_ = std::move(now);
    return;
  }

  if (dt <= max_gap_) {
    const double cpu_w1 =
        last_watts_.count(Domain::CpuPackage) ? watts_at(last_watts_, Domain::CpuPackage)
                                              : watts_at(last_watts_, Domain::CpuCores);
    const double cpu_w2 =
        now.count(Domain::CpuPackage) ? watts_at(now, Domain::CpuPackage)
                                      : watts_at(now, Domain::CpuCores);
    const double gpu_w1 = watts_at(last_watts_, Domain::Gpu);
    const double gpu_w2 = watts_at(now, Domain::Gpu);
    const double tot_w1 = watts_at(last_watts_, Domain::SystemTotal);
    const double tot_w2 = watts_at(now, Domain::SystemTotal);

    const double mwh_cpu = integrate(cpu_w1, cpu_w2, dt);
    const double mwh_gpu = integrate(gpu_w1, gpu_w2, dt);
    double mwh_total = 0.0;
    double mwh_other = 0.0;
    if (tot_w1 > 0.0 || tot_w2 > 0.0) {
      mwh_total = integrate(tot_w1, tot_w2, dt);
      const double other_w1 = std::max(0.0, tot_w1 - cpu_w1 - gpu_w1);
      const double other_w2 = std::max(0.0, tot_w2 - cpu_w2 - gpu_w2);
      mwh_other = integrate(other_w1, other_w2, dt);
    } else {
      // no total: count what we measured as the running total
      mwh_total = mwh_cpu + mwh_gpu;
    }

    if (mwh_total != 0.0 || mwh_cpu != 0.0 || mwh_gpu != 0.0) {
      const std::int64_t minute =
          static_cast<std::int64_t>(std::floor(wall_t / 60.0)) * 60;
      MinuteBucket& bucket = pending_[minute];
      bucket.mwh_total += mwh_total;
      bucket.mwh_cpu += mwh_cpu;
      bucket.mwh_gpu += mwh_gpu;
      bucket.mwh_other += mwh_other;

      bool estimated = false, measured = false;
      auto note = [&](Domain d) {
        auto it = prov.find(d);
        if (it == prov.end()) return;
        if (it->second == Provenance::Estimated) {
          estimated = true;
        } else {
          measured = true;
        }
      };
      if (tot_w1 > 0.0 || tot_w2 > 0.0) {
        note(Domain::SystemTotal);
        if (prov.count(Domain::SystemTotal) &&
            prov[Domain::SystemTotal] == Provenance::Estimated) {
          bucket.mwh_estimated += mwh_total;
        }
      } else {
        note(now.count(Domain::CpuPackage) ? Domain::CpuPackage : Domain::CpuCores);
        note(Domain::Gpu);
        // component-only minutes are treated as measured when the sensors were
        // measured (the Total label comes from the model, not the integrator)
      }
      if (estimated) bucket.flags |= FLAG_ESTIMATED;
      if (measured) bucket.flags |= FLAG_MEASURED;
      if (!estimated && !measured) bucket.flags |= FLAG_MEASURED;
    }
  }
  // else: gap too big (sleep/resume) — do not integrate, just resync

  last_mono_ = mono_t;
  last_watts_ = std::move(now);
}

std::map<std::int64_t, MinuteBucket> EnergyIntegrator::drain() {
  std::map<std::int64_t, MinuteBucket> out;
  out.swap(pending_);
  return out;
}

void EnergyIntegrator::restore(const std::map<std::int64_t, MinuteBucket>& rows) {
  for (const auto& [ts, b] : rows) {
    MinuteBucket& t = pending_[ts];
    t.mwh_total += b.mwh_total;
    t.mwh_cpu += b.mwh_cpu;
    t.mwh_gpu += b.mwh_gpu;
    t.mwh_other += b.mwh_other;
    t.mwh_estimated += b.mwh_estimated;
    t.flags |= b.flags;
  }
}

}  // namespace wp
