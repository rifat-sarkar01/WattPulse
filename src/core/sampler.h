#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/energy_integrator.h"
#include "core/power_model.h"
#include "core/sample.h"

namespace wp {

// Immutable snapshot the UI reads.
struct Snapshot {
  ModelOutput model;
  std::map<std::int64_t, MinuteBucket> pending;  // not-yet-flushed minutes
  std::uint64_t ticks = 0;
  bool any_sensor_ok = false;
};

// Owns the sensors and the 1 Hz loop. Thread-safe snapshot access.
class Sampler {
 public:
  Sampler(std::vector<std::unique_ptr<ISensor>> sensors, PowerModel model,
          double interval_seconds = 1.0, double max_gap_seconds = 5.0);
  ~Sampler();

  Sampler(const Sampler&) = delete;
  Sampler& operator=(const Sampler&) = delete;

  void start();
  void stop();

  Snapshot snapshot() const;

  // Drain pending minute buckets for the writer (they stay in the snapshot
  // until the next drain-and-restore cycle, so UI totals never lose them).
  std::map<std::int64_t, MinuteBucket> drain_pending();
  void restore_pending(const std::map<std::int64_t, MinuteBucket>& rows);

  // Single synchronous poll (used by --probe and tests).
  void poll_once(double mono_t, double wall_t);

  void set_model(const PowerModel& model);

  std::vector<std::string> sensor_status() const;

 private:
  void run();

  std::vector<std::unique_ptr<ISensor>> sensors_;
  PowerModel model_;
  double interval_;
  EnergyIntegrator integrator_;

  mutable std::mutex mutex_;
  ModelOutput last_model_;
  std::map<std::int64_t, MinuteBucket> pending_;
  std::uint64_t ticks_ = 0;
  bool any_ok_ = false;
  std::vector<bool> sensor_ok_;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

double monotonic_seconds();
double wall_seconds();

}  // namespace wp
