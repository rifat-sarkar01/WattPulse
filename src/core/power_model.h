#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/sample.h"

namespace wp {

// Calibration values learned from a wall meter reading.
struct Calibration {
  double base_watts = 45.0;  // modelled non-CPU/GPU draw (board, drives, fans)
  bool calibrated = false;
};

// Form-factor defaults for the modelled base draw.
enum class FormFactor { Desktop, Laptop, Mini, Unknown };

struct ModelOutput {
  std::optional<double> total_watts;
  Provenance total_provenance = Provenance::Measured;
  std::string sourceId;          // which tier produced the total
  std::optional<double> cpu_watts;
  std::optional<double> gpu_watts;
  std::optional<double> other_watts;
  std::vector<Sample> samples;   // echo of inputs (for debugging/--probe)
};

// Merges raw sensor samples into the Best-Total (plan §3.3).
class PowerModel {
 public:
  PowerModel() = default;
  PowerModel(Calibration cal, FormFactor form = FormFactor::Desktop)
      : cal_(cal), form_(form) {}

  // Returns the best total + breakdown. Never throws.
  ModelOutput evaluate(const std::vector<Sample>& samples) const;

  const Calibration& calibration() const { return cal_; }
  void set_calibration(const Calibration& cal) { cal_ = cal; }
  FormFactor form_factor() const { return form_; }

  // Default base watts for an uncalibrated form factor.
  static double default_base(FormFactor form);

 private:
  Calibration cal_{};
  FormFactor form_ = FormFactor::Desktop;
};

}  // namespace wp
