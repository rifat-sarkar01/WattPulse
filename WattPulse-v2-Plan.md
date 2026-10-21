# WattPulse v2 — Implementation Plan

**Goal:** a tiny native Windows app that shows live power draw and rolling energy totals on **any** PC (laptop *or* desktop), with a taskbar/tray readout and a calm, neutral UI. Written in C++.

---

## 1. What changes and why

| Feedback | Root cause in v1 | v2 decision |
|---|---|---|
| "No battery sensor found" on your PC | v1 reads **only the battery driver**. Desktops have no battery. | Multi-source measurement: CPU, GPU, battery, platform, optional wall meter, plus a clearly labelled estimate when nothing measures the whole machine (§3). |
| No taskbar option | App is a normal window only. | Tray icon with live watts, optional taskbar overlay widget, floating mini widget (§5). |
| Dark blue not eye-pleasing | Navy-tinted background and saturated blue accent. | Neutral graphite palette, warm amber accent, light theme and "follow system" (§6). |
| C++ would be better | Python + tkinter carries a runtime and ~30–40 MB of RAM. | Native C++20, Win32 + Direct2D, single static exe, no runtime to install (§4). |
| Use HWMonitor-style techniques | — | Use the same *techniques* HWMonitor uses (RAPL energy counters, vendor GPU APIs, EMI), implemented from public specs, **not** by disassembling HWMonitor (§3.4). |

> **Interpretation note:** I read "no taskbar option" as "there is no taskbar/tray readout". If you meant something else, §5 is the only section that changes.

---

## 2. Targets (acceptance criteria for "done")

| Metric | Target |
|---|---|
| Idle CPU | < 0.3 % of one core at 1 Hz sampling |
| Memory (working set) | < 20 MB |
| Executable | < 3 MB, static CRT, no installer required |
| Cold start to first reading | < 500 ms |
| Reliability | 24 h soak: no handle/memory growth, correct after sleep/resume, no crash when a sensor disappears |
| Accuracy | CPU/GPU within ±5 % of LibreHardwareMonitor/HWiNFO; total within ±10–15 % of a wall meter **after calibration** |
| Works with no sensors | Shows an honest empty state, never an error dialog |

---

## 3. Measurement strategy (the core of the project)

### 3.1 Be honest about what software can measure

Software can read **component** power (CPU package, GPU board, battery discharge). It **cannot** read what a desktop pulls from the wall without extra hardware. So v2 reports a **Total** with a visible *provenance chip* so the number is never misleading:

- **Measured** — comes from a sensor or meter.
- **Estimated** — CPU + GPU measured, rest of system modelled.

### 3.2 Source tiers (try in this order, use everything available)

**Tier 0 — no driver, no admin (ship by default)**

| Source | How | Notes |
|---|---|---|
| Battery | `IOCTL_BATTERY_QUERY_STATUS` (already proven in v1) | Laptops only; discharge = real system draw. |
| Windows Energy Meter (EMI) | PDH counters from the `Energy Meter` set (e.g. `\Energy Meter(RAPL_Package0_PKG)\Power`) or the `\\.\EMI` device | Exposes Intel RAPL-class data without a driver on **many** machines, not all. Check yours with `typeperf -q "Energy Meter"`. |
| NVIDIA GPU | `nvml.dll` → `nvmlDeviceGetPowerUsage` (mW) | Load dynamically (it ships with the driver); no SDK linking. |
| AMD GPU | ADLX (preferred) or ADL | GPU/board power metric. Verify exact API names at implementation. |
| Intel GPU (Arc/iGPU) | IGCL (`ctlPowerTelemetryGet`) | Verify availability per driver version. |
| CPU load/frequency | PDH `Processor Information` | Feeds the estimator, not shown as power. |

**Tier 1 — needs a privileged helper (opt-in "Enhanced sensors")**

Read CPU energy counters directly from MSRs, which is what HWMonitor, HWiNFO and LibreHardwareMonitor do:

| Vendor | Registers | Meaning |
|---|---|---|
| Intel | `0x606` MSR_RAPL_POWER_UNIT | energy unit = 1 / 2^ESU joules, ESU = bits 12:8 |
| Intel | `0x611` PKG, `0x639` PP0 (cores), `0x641` PP1 (iGPU), `0x619` DRAM, `0x64D` PSYS (whole platform on many mobile CPUs) | 32-bit cumulative energy counters |
| AMD Zen | `0xC0010299` unit, `0xC001029A` core energy, `0xC001029B` package energy | read per core/package, pin the reading thread to that core |

Power is derived, not read: `P = (E₂ − E₁) × unit / Δt`. Handle 32-bit counter wrap-around, sample no faster than ~2 Hz, use a monotonic clock for Δt.

**Tier 2 — optional external truth for desktops**

| Source | How |
|---|---|
| Smart plug | Local HTTP API (e.g. Shelly `Switch.GetStatus` → `apower`, Tasmota `Status 8`). Real wall power. |
| UPS | USB HID Power Device class or NUT; many report load % (needs rated watts). |
| Digital PSU | Vendor USB HID telemetry (e.g. Corsair i-series). Later, if wanted. |

**Tier 3 — estimator (only when no whole-system source exists)**

```
P_total ≈ ( P_cpu + P_gpu + P_base ) / η
```

- `P_base`: board, RAM, fans, drives. Defaults by form factor, then **calibrated once** (user types a wall-meter reading; app stores an offset/scale).
- `η`: PSU efficiency, ~0.88–0.92 at mid load (80 PLUS Gold-class); a simple curve is enough.
- Always labelled **Estimated**. Never silently mixed with measured data.

### 3.3 "Best Total" selection

Priority: **wall meter / UPS** → **PSYS or battery-derived system power** → **CPU + GPU + calibrated base (Estimated)** → **component-only (no total)**.
The UI always shows which one is active, plus a per-component breakdown (CPU / GPU / Other).

### 3.4 About "reverse engineering HWMonitor" — recommendation

I recommend **not** disassembling HWMonitor, for three reasons:

1. Its EULA likely prohibits reverse engineering and the code is proprietary; you'd risk legal and licensing problems for a project you want on your portfolio. (Check the EULA yourself; I'm not a lawyer.)
2. You don't need to. The technique is **public**: Intel SDM (RAPL), AMD PPR, Windows EMI docs, NVML/ADLX/IGCL docs.
3. Open-source implementations show the working details legally: **LibreHardwareMonitor** (MPL-2.0, C#) is the best reference. Read it to learn the register layouts and edge cases, then write your own C++ against the specs. If you copy code from it, MPL-2.0 file-level terms apply.

Behaviour-level observation of *your own* machine (compare your readings to HWiNFO/LibreHardwareMonitor side by side) is fine and is how you'll validate accuracy.

### 3.5 Driver strategy (the hard part)

| Option | Verdict |
|---|---|
| Ship **WinRing0** | **No.** Known vulnerabilities, flagged by Windows Defender as a vulnerable driver. |
| Write your **own kernel driver** | Needs an EV certificate + Microsoft attestation signing, ongoing maintenance. Too heavy for a solo project. |
| **PawnIO** (signed driver + sandboxed modules, adopted by LibreHardwareMonitor) | **Recommended for Tier 1.** Check its current license and redistribution terms before bundling. |
| **No driver at all** (Tier 0 only) | Fully valid MVP. Many machines already give CPU power via EMI. |

**Security rule that must hold in any design:** the privileged helper exposes a **whitelist** of exact MSR addresses (the ones in §3.2), read-only. Never expose generic MSR/IO-port access. That generic access is precisely what made older tools dangerous.

---

## 4. Architecture (C++)

### 4.1 Stack

- **C++20, MSVC**, CMake, static CRT (`/MT`), LTO.
- **UI:** raw Win32 + **Direct2D/DirectWrite** (small, fast, crisp text). No Qt (too big), no Electron. Dear ImGui is acceptable for a prototype but looks like a dev tool.
- **Storage:** SQLite amalgamation (≈ 1 MB) with WAL, minute buckets as in v1.
- **Settings:** small JSON or INI in `%APPDATA%\WattPulse`.
- **Tests:** Catch2 or GoogleTest. **CI:** GitHub Actions `windows-latest`.

### 4.2 Processes and threads

```
 wattpulse.exe (normal user, no admin)
   ├─ UI thread      Win32 message loop, Direct2D drawing, tray, overlay
   ├─ Sampler thread 1 Hz waitable timer → polls sensors → integrates energy
   └─ Writer         flush minute buckets to SQLite every 30 s

 wattpulse-helper.exe (elevated, optional, started only for Enhanced sensors)
   └─ named pipe, tiny protocol: ReadMsr(addr) for whitelisted addresses only
```

The UI never blocks on hardware. It reads an immutable **snapshot** (swap a `shared_ptr` or use a mutex-guarded copy).

### 4.3 Core interfaces

```cpp
enum class Domain { SystemTotal, CpuPackage, CpuCores, Gpu, Dram, Battery, Other };
enum class Provenance { Measured, Estimated };

struct Sample {
    Domain domain;
    std::optional<double> watts;      // nullopt = no data right now
    Provenance provenance;
    std::string sourceId;             // e.g. "nvml:0", "rapl:pkg0", "battery"
};

struct ISensor {
    virtual ~ISensor() = default;
    virtual std::string_view name() const = 0;
    virtual bool available() = 0;                  // cheap probe, safe to call repeatedly
    virtual void poll(std::vector<Sample>& out) = 0;   // never throws
};
```

Each source (`BatterySensor`, `EmiSensor`, `NvmlSensor`, `AdlxSensor`, `RaplSensor`, `SmartPlugSensor`, …) implements `ISensor`. A `PowerModel` merges samples into the **Best Total** (§3.3) and the breakdown.

### 4.4 Energy accounting

- Integrate **per domain**, trapezoid rule on monotonic Δt.
- Ignore gaps > 5 s (sleep/resume) instead of integrating them.
- Store per minute: `(ts, mwh_total, mwh_cpu, mwh_gpu, mwh_other, provenance_flags)`.
- Windows: last 60 min, 24 h, 7 d, 30 d, rolling. Because v2 measures continuously (not just on battery), totals count **all** time where a total was available; a footnote shows the share that was *Estimated*.
- Prune after 35 days.

### 4.5 Robustness rules

- Every sensor is wrapped: failure → mark unavailable, retry with back-off, never crash.
- No `new`/`delete` in app code; RAII for every handle (`HANDLE`, PDH query, NVML, COM).
- Single-instance mutex (prevents double-counting).
- Per-monitor-v2 DPI manifest; handle `WM_DPICHANGED`, `WM_POWERBROADCAST`, `TaskbarCreated`.

---

## 5. Taskbar options

Windows 11 removed classic taskbar toolbars (deskbands), so "inside the taskbar" is done differently per OS. Offer three, ship the first two.

| Option | How | Notes |
|---|---|---|
| **Tray icon with live watts** (ship) | `Shell_NotifyIconW`; render the number into a small bitmap → `HICON` every second; rich tooltip (total, CPU, GPU, source). Left click toggles window; right-click menu: Show/Compact/Always on top/Theme/Start with Windows/Exit. | Works on Win10 and 11. Win11 hides new tray icons by default: user must pin it once in *Taskbar settings*. Document this. |
| **Floating mini widget** (ship) | Borderless always-on-top draggable window showing watts (+ optional sparkline). Remembers position. | Most robust. This replaces v1's "Compact" mode. |
| **Taskbar overlay readout** (stretch) | TrafficMonitor-style: a small layered, click-through-optional window positioned over free space on the taskbar; follow `Shell_TrayWnd` rect, DPI, auto-hide, multi-monitor. | Fragile: Explorer restarts, fullscreen apps, Win11 layout changes. Do last. |

Also: keep the live watts in the window title (taskbar hover text) like v1, and add "Start with Windows" (per-user `Run` registry key, off by default).

---

## 6. UI redesign

### 6.1 Palette (neutral graphite, no blue tint)

Contrast computed against the stated background (WCAG ratio).

| Token | Dark (default) | Contrast | Light | Contrast |
|---|---|---|---|---|
| `bg` | `#151515` | — | `#F6F6F4` | — |
| `surface` | `#1D1D1F` | — | `#FFFFFF` | — |
| `line` | `#2A2A2D` | — | `#E2E2DE` | — |
| `text` | `#ECECEC` | 15.5 : 1 | `#1A1A1A` | 16.1 : 1 |
| `muted` | `#9A9AA0` | 6.5 : 1 | `#6B6B70` | 4.9 : 1 |
| `accent` (power) | `#F5A524` amber | 8.9 : 1 | `#B25E00` | 4.3 : 1 (use for graphics/large text) |
| `positive` (charging) | `#4CC38A` | 8.2 : 1 | `#1E8E5A` | 3.8 : 1 (graphics only) |
| `warn` (high draw) | `#FF6B5E` | 6.5 : 1 | `#C2392B` | verify when implementing |

Why this works: neutral grey avoids the "navy AI app" look; one warm accent says "energy" and separates the chart from the background without glare; no pure black or pure white (less eye strain). Themes: **Graphite (default) · Light · Follow system** (read `AppsUseLightTheme`). Keep the title bar in sync via `DWMWA_USE_IMMERSIVE_DARK_MODE`. Avoid Mica by default (it tints with the wallpaper, which can reintroduce a colour cast).

### 6.2 Layout

```
┌──────────────────────────────────────────┐
│ WattPulse                    ⌄ Compact … │
│                                          │
│  42.3 W                 [ Estimated ▾ ]  │  ← hero + provenance chip
│  CPU 18.1 · GPU 14.2 · Other 10.0        │  ← breakdown
│                                          │
│  ▁▂▂▃▅▆▆▅▃▃▄▅▆▇▆▅                        │  ← 5-min sparkline, amber
│                                          │
│  ENERGY USED                             │
│  Last hour           41.9 Wh             │
│  Last 24 hours      812 Wh               │
│  Last 7 days        5.4 kWh              │
│  Last 30 days      21.7 kWh              │
│  62 % of totals estimated                │
└──────────────────────────────────────────┘
```

- **Tabular figures** (DirectWrite `tnum`) so digits don't jitter.
- **Empty/partial states** are explained, not alarming: "No whole-system sensor. Showing CPU + GPU, estimating the rest. Calibrate →".
- Fix v1's empty hero (the bare dash line): show a skeleton and the reason.
- Segoe UI Variable, one weight for numbers, restrained spacing, 1 px hairlines, no shadows or gradients.
- Chart: 5-min sparkline default; hover tooltip; optional range switch (5 min / 1 h) later.

---

## 7. Phases

Estimates assume ~10–15 h/week; the MVP is P1–P5.

| # | Phase | Output | Done when | Time |
|---|---|---|---|---|
| **P0** | **Sensor spike** | `docs/sensor-matrix.md`: what *your* machines expose. Run `typeperf -q "Energy Meter"`, compare with LibreHardwareMonitor/HWiNFO, note CPU/GPU vendor. | You know which tiers work on your hardware and whether a driver is needed at all. | 3–5 days |
| **P1** | Skeleton | CMake project, `ISensor`, sampler, energy integrator, SQLite store, CI, unit tests with fake sensors. | `ctest` green on CI; integrator correct for wrap-around and gaps. | 1–1.5 wk |
| **P2** | Tier 0 sensors | Battery (port from v1), EMI/PDH, NVML, ADLX/IGCL as hardware allows. `--probe` CLI prints every source. | Readings match a reference tool within ±5 % on your machine(s). | 1.5 wk |
| **P3** | Power model | Best-Total selection, breakdown, estimator, calibration flow. | Desktop with no battery shows a labelled **Estimated** total; calibration brings it within ±10–15 % of a wall meter (if you have one). | 1 wk |
| **P4** | New UI | Direct2D window, graphite/light themes, hero + breakdown + chart + totals, DPI-correct. | Looks right at 100/125/150/200 % scaling; matches §6. | 2 wk |
| **P5** | Taskbar | Tray icon with live watts, floating mini widget, autostart option, single instance. Overlay widget only if time allows. | Survives Explorer restart, sleep/resume, DPI change. | 1 wk |
| **P6** | Enhanced sensors (optional) | Elevated helper + PawnIO-based whitelisted MSR reads, Intel/AMD RAPL, opt-in toggle with UAC. | Package power agrees with LibreHardwareMonitor; app still fully works if the user declines. | 1.5–2 wk |
| **P7** | Hardening and release | Soak test, resource targets from §2, code signing if available, portable zip, README/screenshots. | All §2 targets met. | 1 wk |

**Order matters:** do P0 first. If EMI/NVML already give CPU and GPU power on your PC, P6 (the risky part) may never be necessary.

---

## 8. Testing and validation

**Automated**
- Integrator: wrap-around, gap > 5 s, zero/negative Δt, non-monotonic input.
- Rolling windows and minute bucketing, DB failure and retry.
- Power model: every priority path, missing-sensor transitions, provenance flags.
- Estimator: monotonic in load, calibration round-trip.

**Manual matrix**
- Windows 10 22H2 and Windows 11.
- Laptop (battery + AC), desktop, Intel CPU, AMD CPU, NVIDIA, AMD and Intel GPUs as available.
- **VM:** no sensors → graceful empty state.
- DPI 100/150/200 %, two monitors, auto-hide taskbar, Explorer restart, sleep/resume.
- 24 h soak: private bytes and handle count stay flat.

**Accuracy**
- Side-by-side with LibreHardwareMonitor/HWiNFO under idle, Cinebench-style CPU load, GPU load.
- Wall meter (plug-in power meter) to calibrate and verify the estimator. Cheap and worth it.

---

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Antivirus flags a driver/helper | Users can't run Tier 1 | Keep Tier 1 optional; bundle only a properly signed driver (PawnIO); sign your exe if possible. |
| Memory Integrity (HVCI) or Secure Boot blocks MSR access | No RAPL on some PCs | Detect and degrade to Tier 0/3 with a clear message. |
| Kernel anti-cheat in games dislikes monitoring drivers | Conflicts | Helper runs only when "Enhanced sensors" is enabled; document it. |
| Sensor coverage differs per CPU/GPU generation | Some PCs only get Estimated | Provenance chip + sensor matrix in docs. |
| RAPL is on-chip telemetry/model, not a wall meter | Users over-trust the number | Say "CPU package power" explicitly; Total always shows its source. |
| Win11 hides tray icons; overlay widget breaks on shell updates | Taskbar feature feels flaky | Ship tray + floating widget first; overlay is stretch. |
| Licensing if you reuse code | Legal mess | Write from specs; if you copy from LibreHardwareMonitor, honour MPL-2.0; don't touch HWMonitor binaries. |
| Scope creep | Never ships | Hard MVP line after P5. |

---

## 10. Repo layout

```
WattPulse/
├─ CMakeLists.txt
├─ src/
│  ├─ app/            WinMain, settings, single-instance
│  ├─ core/           Sample, Sampler, EnergyIntegrator, PowerModel, Estimator
│  ├─ sensors/        battery, emi, nvml, adlx, igcl, rapl_client, smartplug
│  ├─ storage/        sqlite wrapper, minute buckets
│  ├─ ui/             window, chart, theme, tray, overlay, mini widget
│  └─ helper/         wattpulse-helper.exe (elevated, whitelist-only)
├─ third_party/       sqlite3 amalgamation
├─ tests/
├─ docs/              sensor-matrix.md, architecture.md, calibration.md
├─ assets/            icon, screenshots
└─ .github/workflows/ci.yml
```

---

## 11. Decisions I need from you

1. **Hardware:** what CPU and GPU does your main PC have, and do you also test on a laptop? (Decides which Tier 0 sources matter.)
2. **Driver:** are you OK with an optional elevated helper + PawnIO for Tier 1, or should v2 stay driver-free?
3. **Wall meter / smart plug:** do you have one? It's the best way to calibrate the estimator.
4. **Distribution:** portable exe only, or also an installer? Will you sign it?
5. **Licence:** MIT is fine if you write from specs; choose GPL/MPL if you reuse open-source code.

---

## 12. References

- Intel® 64 and IA-32 Architectures Software Developer's Manual, Vol. 3 — RAPL (running average power limit) energy status MSRs
- AMD Processor Programming Reference (PPR) for Family 17h/19h — energy status MSRs
- Windows Driver Kit — Energy Meter Interface (EMI); PDH `Energy Meter` counter set
- NVIDIA NVML documentation — `nvmlDeviceGetPowerUsage`
- AMD ADLX and Intel IGCL SDK documentation — GPU power telemetry
- LibreHardwareMonitor (MPL-2.0): github.com/LibreHardwareMonitor/LibreHardwareMonitor — reference implementation
- PawnIO: github.com/namazso/PawnIO — signed driver used by LibreHardwareMonitor (check licence)
