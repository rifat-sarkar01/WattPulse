# WattPulse sensor matrix

What each machine actually exposes, measured with `wattpulse --probe` and
`typeperf`. Run `--probe` on any new PC and paste the result here.

## rifat-desktop (Ryzen 7 7700 + Radeon iGPU, no battery)

Probed 2026-10-10 on Windows 11.

| Source | Tier | Status | Notes |
|---|---|---|---|
| Battery | 0 | **absent** | Desktop: `Win32_Battery` empty, no `GUID_BATTERY_INTERFACE` devices. |
| Windows Energy Meter (PDH) | 0 | **works** | `typeperf "\Energy Meter(*)\Power"` lists `RAPL_Package0_PKG` + 8× `RAPL_Package0_Core*` + `_Total`. Package counter reports ~35 W idle / ~50+ W under load. `_Total` is 0 (no wall meter). Units read as **watts × 1000** (i.e. milliwatts), so the app scales by `emi_scale=0.001` — **verify against LibreHardwareMonitor before trusting absolute values**. No `PSYS` counter on this chip. |
| NVML (NVIDIA) | 0 | **absent** | No `nvml.dll` (AMD GPU). Sensor loads dynamically and stays unavailable. |
| ADLX (AMD) | 0 | **works** | `amdadlx64.dll` present. GPU power via `IADLXGPUMetrics::GPUPower`, probed for support before use. Live probe: **17–42 W** (iGPU), plausible. |
| RAPL MSR (Tier 1) | 1 | not used | v2 is driver-free by decision; PDH EMI already covers package power. |

**Best Total on this machine:** CPU package (EMI) + GPU (ADLX) → **Estimated**
total (no wall meter, no PSYS, no battery). Calibrate once with a plug-in power
meter: `wattpulse --calibrate <wall-watts>`.

### Reference readings (for accuracy checks)

Probe output from the v2 build, 2026-10-10, five 1 s rounds:

```text
battery  available=no
emi      available=yes
         [cpu] emi:RAPL_Package0_PKG 115.86 W (Measured)   <- round 0 (first-sample spike)
         [cpu] emi:RAPL_Package0_PKG 24.36 W (Measured)    <- round 1
         [cpu] emi:RAPL_Package0_PKG 21.82 W (Measured)    <- round 2, idle
nvml     available=no
adlx     available=yes
         [gpu] adlx:0     42.00 W (Measured)               <- round 0
         [gpu] adlx:0     18.00 W (Measured)               <- round 1, idle
```

| State | EMI PKG (raw) | EMI PKG (scaled) | ADLX GPU | Notes |
|---|---|---|---|---|
| idle (~4 % CPU) | ~35 000 | ~35 W | 17–18 W | typeperf raw vs `--probe` |
| single-core load | ~52 000–57 000 | ~52–57 W | 18–42 W | PowerShell busy job |

Note: `--probe` round 0 can spike (first PDH sample / GPU clock ramp); judge
steady state from rounds 1+. ADLX reports watts directly (no scaling).

Compare side-by-side with LibreHardwareMonitor / HWiNFO before enabling
calibration. If the scaled value is off by ~1000×, adjust `emi_scale` in
`%APPDATA%\WattPulse\settings.ini`.

## How to probe a new machine

```powershell
# WattPulse's own probe (5 rounds, 1 s apart)
wattpulse.exe --probe

# Raw PDH Energy Meter counters (Intel RAPL without a driver)
typeperf -q "Energy Meter"
typeperf "\Energy Meter(*)\Power" -sc 3

# Battery present?
Get-CimInstance Win32_Battery

# GPU vendor
Get-CimInstance Win32_VideoController | Select-Object Name
```

Paste the output into a new section above.

## Gaps and future work

| Source | Status |
|---|---|
| Smart plug (Shelly/Tasmota) | not implemented (Tier 2, plan §3.2) |
| UPS via HID/NUT | not implemented (Tier 2) |
| Intel IGCL (Arc GPU) | not implemented (no Intel GPU on hand) |
| RAPL MSRs via PawnIO | dropped by decision (v2 stays driver-free) |
