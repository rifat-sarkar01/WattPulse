#pragma once

namespace wp {

// PSU efficiency model (80 PLUS Gold-class-ish curve, mid-load ~0.90).
double psu_efficiency(double input_watts);

// P_total ≈ (P_cpu + P_gpu + P_base) / η
double estimate_total(double cpu_watts, double gpu_watts, double base_watts);

// Solve for the base watts that make the estimate match a wall-meter reading.
// `already_known` is the modelled portion (cpu+gpu)/η at the moment of reading.
double calibrate_base(double wall_watts, double cpu_watts, double gpu_watts);

}  // namespace wp
