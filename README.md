<img src="assets/wattpulse.png" width="96" alt="WattPulse">

# WattPulse

Live device power draw for Windows, plus energy used over the last hour, day,
week and month. Native C++20, single static exe, no runtime to install.

**v2** is a rewrite: multi-source measurement (works on desktops, not just
laptops), an honest Measured/Estimated provenance label, calibration against a
wall meter, and a native Direct2D dashboard with a CLI fallback plus a compact
live meter that docks next to the taskbar.

## Build

Requires Visual Studio 2022 or newer (MSVC) and CMake 3.21+.

```powershell
# Use the generator installed with your Visual Studio; newer releases also
# provide "Visual Studio 18 2026".
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The exe lands at `build\Release\wattpulse.exe`. It is statically linked
(`/MT`), so it runs on any x64 Windows 10/11 machine without a runtime.

## Run

```powershell
wattpulse.exe                   # open the dashboard (default)
wattpulse.exe --demo            # dashboard preview with synthetic history
wattpulse.exe --probe           # what sensors does this machine expose?
wattpulse.exe --run             # live 1 Hz CLI readout + totals on exit
wattpulse.exe --run --silent    # for autostart: no console output
wattpulse.exe --calibrate 120   # set the modelled base from a wall meter (W)
wattpulse.exe --autostart on    # start with Windows (off by default)
```

## Dashboard

The default window is a Direct2D/DirectWrite dashboard with a light and dark
theme. It shows the current system draw and its CPU/GPU/other split, the rolling
energy totals for the last hour, 24 hours, week and month, a power-history chart,
sensor health, calibration details and storage status.

The chart and one of the total cards can be switched between **last hour, last
24 hours, last week and last month** using the dropdown. The theme toggle is in
the top-right corner and is remembered in `settings.ini`. The dashboard samples
at 1 Hz and writes completed minute buckets to the same `power.db` as the CLI.

**Pin to taskbar** docks a compact always-on-top meter above the taskbar. It
shows the live system draw and, optionally, one rolling total. Right-click it (or
click the chevron) to choose *Live draw only*, last hour, last 24 hours, last
week or last month, or to unpin. Click it to bring the dashboard back. The pinned state and chosen total
are remembered. (Windows 11 has no supported public deskband API, so the meter is
a borderless window that follows the taskbar edge rather than a shell band.)

## How it measures

v1 read only the battery driver, so it could not run on a desktop. v2 tries
every driverless source in order and labels the result:

| Tier | Source | Needs |
|---|---|---|
| 0 | Battery discharge (`IOCTL_BATTERY_QUERY_STATUS`) | a battery |
| 0 | Windows Energy Meter / PDH (Intel RAPL-class counters) | many machines, no driver |
| 0 | NVIDIA GPU (`nvml.dll`, loaded dynamically) | NVIDIA driver |
| 0 | AMD GPU (ADLX, `amdadlx64.dll`, loaded dynamically) | AMD driver |
| 3 | Estimator: `(CPU + GPU + base) / PSU-efficiency` | always available as fallback |

The **Total** is chosen by priority: wall meter / UPS > PSYS or battery
discharge > CPU + GPU + calibrated base (**Estimated**). The readout always
shows which one is active. See `docs/sensor-matrix.md` for what your hardware
exposes and how to calibrate.

Energy totals are integrated per minute (trapezoid rule, gaps > 5 s from
sleep/resume are not counted) into `%APPDATA%\WattPulse\power.db`, kept for
35 days as rolling windows. Databases written by v1 (`ts, mwh`) are migrated in
place on first start; if migration fails, the original table is left untouched.

Runtime diagnostics (sensor availability, storage errors, totals) are written
to `%APPDATA%\WattPulse\wattpulse.log` and echoed to the console.

## Settings

`%APPDATA%\WattPulse\settings.ini`:

```ini
base_watts=45        # modelled board/RAM/drives draw
calibrated=false     # true once --calibrate has been run
autostart=false
theme=system         # system | light | dark (saved by the dashboard toggle)
emi_scale=0.001      # EMI PDH counter scale; adjust if readings are 1000x off
form_factor=desktop  # desktop | laptop | mini | unknown
taskbar_pinned=false # true after "Pin to taskbar" (restores the meter on launch)
taskbar_span=none    # none | hour | day | week | month (extra total in the meter)
```

## Layout

```
src/
  app/         entry point, settings, single instance, autostart
  core/        Sample, ISensor, Sampler, EnergyIntegrator, PowerModel, Estimator
  sensors/     battery, emi (PDH), nvml, adlx, demo, factory
  storage/     SQLite minute buckets, rolling totals, chart series
  ui/          Direct2D dashboard + shared text formatting
tests/         Catch2 unit tests (integrator, model, estimator, store, format)
third_party/   sqlite3 amalgamation, Catch2 amalgamated, ADLX headers (AMD)
docs/          sensor-matrix.md
```

The sampler thread owns the sensors and the integrator; everything else reads
immutable snapshots. Every sensor is failure-isolated: if a source disappears
(driver unload, eGPU unplug), it degrades to unavailable and the model falls
back — never a crash.

## Develop

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

CI (GitHub Actions, `windows-latest`) builds, tests and uploads the exe on
every push and PR.

## Licence

MIT — see [LICENSE](LICENSE). The ADLX headers under `third_party/adlx/` are
AMD's SDK headers, distributed under the ADLX SDK License Agreement; they are
used here to read GPU power only, and `amdadlx64.dll` is loaded from the
user's own AMD driver at runtime (never redistributed).
