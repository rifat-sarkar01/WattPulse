#include "core/sampler.h"

#include <chrono>
#include <exception>
#include <string>

#include "app/log.h"

namespace wp {

double monotonic_seconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

double wall_seconds() {
  using namespace std::chrono;
  return duration<double>(system_clock::now().time_since_epoch()).count();
}

Sampler::Sampler(std::vector<std::unique_ptr<ISensor>> sensors, PowerModel model,
                 double interval_seconds, double max_gap_seconds)
    : sensors_(std::move(sensors)),
      model_(std::move(model)),
      interval_(interval_seconds),
      integrator_(max_gap_seconds) {
  sensor_names_.reserve(sensors_.size());
  for (const auto& sensor : sensors_) {
    sensor_names_.emplace_back(sensor->name());
  }
  sensor_available_.assign(sensors_.size(), false);
  sensor_ok_.assign(sensors_.size(), false);
}

Sampler::~Sampler() { stop(); }

void Sampler::start() {
  if (thread_.joinable()) return;
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void Sampler::stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void Sampler::run() {
  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  while (!stop_) {
    poll_once(monotonic_seconds(), wall_seconds());
    next += std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(interval_));
    // sleep in small slices so stop() is responsive, but keep a fixed cadence
    while (!stop_) {
      const auto now = clock::now();
      if (now >= next) break;
      const auto remaining = next - now;
      const auto slice = remaining < std::chrono::milliseconds(100)
                             ? remaining
                             : std::chrono::duration_cast<clock::duration>(
                                   std::chrono::milliseconds(100));
      std::this_thread::sleep_for(slice);
    }
    // after a long stall (sleep/resume), restart the cadence instead of bursting
    if (clock::now() - next > std::chrono::seconds(5)) next = clock::now();
  }
}

void Sampler::poll_once(double mono_t, double wall_t) {
  std::vector<Sample> samples;
  samples.reserve(16);
  bool ok = false;
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    ISensor* s = sensors_[i].get();
    const bool was_ok = sensor_ok_[i];
    const std::string& name = sensor_names_[i];
    std::string error;
    bool available = false;
    bool this_ok = false;
    try {
      available = s->available();
      if (available) {
        const std::size_t before = samples.size();
        s->poll(samples);
        this_ok = samples.size() > before;
      }
    } catch (const std::exception& e) {
      error = e.what();
      available = false;
      this_ok = false;
    } catch (...) {
      available = false;
      this_ok = false;
    }
    sensor_available_[i] = available;
    sensor_ok_[i] = this_ok;
    ok = ok || this_ok;
    if (was_ok && !this_ok) {
      log_warn("sensor " + name + " stopped returning data" +
               (error.empty() ? "" : ": " + error));
    } else if (!was_ok && this_ok) {
      log_info("sensor " + name + " is producing data");
    }
  }

  // Apply the model: attach estimated total when no measured one exists.
  ModelOutput model = model_.evaluate(samples);
  if (!model.total_watts.has_value()) {
    model.samples = samples;
  } else if (model.total_provenance == Provenance::Estimated) {
    // Re-tag the total so the integrator stores it with the right flag.
    Sample total;
    total.domain = Domain::SystemTotal;
    total.watts = model.total_watts;
    total.provenance = Provenance::Estimated;
    total.sourceId = "estimate";
    samples.push_back(total);
  }

  integrator_.tick(mono_t, wall_t, samples);

  std::lock_guard<std::mutex> lock(mutex_);
  last_model_ = std::move(model);
  any_ok_ = ok;
  ++ticks_;
  // fold freshly completed buckets into the visible pending set
  for (auto& [ts, b] : integrator_.drain()) {
    MinuteBucket& t = pending_[ts];
    t.mwh_total += b.mwh_total;
    t.mwh_cpu += b.mwh_cpu;
    t.mwh_gpu += b.mwh_gpu;
    t.mwh_other += b.mwh_other;
    t.mwh_estimated += b.mwh_estimated;
    t.flags |= b.flags;
  }
}

Snapshot Sampler::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot s;
  s.model = last_model_;
  s.pending = pending_;
  s.sensors.reserve(sensor_names_.size());
  for (std::size_t i = 0; i < sensor_names_.size(); ++i) {
    s.sensors.push_back(
        {sensor_names_[i], sensor_available_[i], sensor_ok_[i]});
  }
  s.ticks = ticks_;
  s.any_sensor_ok = any_ok_;
  return s;
}

std::map<std::int64_t, MinuteBucket> Sampler::drain_pending() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::int64_t, MinuteBucket> out;
  out.swap(pending_);
  return out;
}

void Sampler::restore_pending(const std::map<std::int64_t, MinuteBucket>& rows) {
  std::lock_guard<std::mutex> lock(mutex_);
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

void Sampler::set_model(const PowerModel& model) {
  std::lock_guard<std::mutex> lock(mutex_);
  model_ = model;
}

std::vector<std::string> Sampler::sensor_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  out.reserve(sensor_names_.size());
  for (std::size_t i = 0; i < sensor_names_.size(); ++i) {
    const char* state = sensor_ok_[i] ? ": ok"
                                       : (sensor_available_[i] ? ": idle" : ": unavailable");
    out.push_back(sensor_names_[i] + state);
  }
  return out;
}

}  // namespace wp
