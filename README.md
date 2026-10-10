<img src="assets/wattpulse.png" width="96" alt="WattPulse">

# WattPulse

Live device power draw for Windows, plus energy used over the last hour, day, week and month.
Tiny (standard library only), no background services, pinnable to the taskbar.

## Run

```powershell
python -m venv .venv ; .\.venv\Scripts\activate
pip install -e .
wattpulse                    # windowless launcher
python -m wattpulse --demo   # preview with fake data (nothing is saved)
python -m wattpulse --probe  # print 5 raw sensor readings (diagnostics)
```

## Build a pinnable exe

```powershell
pip install -e ".[build]"
.\packaging\build.ps1      # -> dist\WattPulse.exe, right-click -> Pin to taskbar
```

The script regenerates the brand assets, stamps the exe with the version from
`src/wattpulse/__init__.py`, applies the DPI-aware manifest, and bundles the
`wattpulse.ico` window icon. CI runs the same script and uploads `WattPulse.exe`
as a build artifact.

## How it measures

Power comes from the battery driver (`IOCTL_BATTERY_QUERY_STATUS`), the same source Windows'
own battery meter uses. That means:

- **Laptops/tablets:** works. **Desktops:** no sensor exists; the app says so.
- Energy totals are integrated **only while on battery**. Plugged in, the battery cannot
  report what the system itself draws, so WattPulse shows the charge rate (green) and does not
  add it to totals.
- Totals are rolling windows (last 60 min, 24 h, 7 d, 30 d), stored per minute in
  `%APPDATA%\WattPulse\power.db` and pruned after 35 days. Data from the pre-rename
  `Wattly` folder is migrated on first start.

## Layout

```
src/wattpulse/
  app.py            entry point, CLI flags, wiring, crash reporting
  config.py         constants and paths
  sampler.py        1 Hz background thread, energy integration, chart history
  storage.py        SQLite (minute buckets), rolling totals
  format.py         W / Wh formatting, chart axis
  platform_win.py   DPI, taskbar app-id, single instance, dark title bar
  sensors/          base (Reading/State), windows (IOCTL), demo, null
  ui/               window, chart, theme (design tokens), icon (brand mark)
tests/              pytest (logic + headless UI smoke tests)
tools/              make_icon.py (brand assets), version_info.py (exe resource)
packaging/          PyInstaller build, exe manifest, launcher
assets/             wattpulse.ico / wattpulse.png, generated from ui/icon.py
```

The UI never touches hardware or the database directly: the sampler thread owns the sensor,
and the UI thread only reads snapshots and flushes to SQLite once every 30 s.

## Develop

```powershell
pip install -e ".[dev]"
ruff check .
pytest
```

## Brand

The mark, palette (Ink `#0E1424`, Volt `#F68E17`, Paper `#EEF0E8`) and stroke weights
live in `ui/icon.py`, which redraws the brand sheet's geometry in code. That single
renderer feeds the window icon, the header logo and `assets/wattpulse.ico`, so they
cannot drift apart; regenerate the committed assets with `python tools/make_icon.py`.

## Known limits / ideas

- Verified on Linux with a fake sensor; run `wattpulse --probe` on your laptop first, since
  vendors differ in how they report battery rate.
- Not yet: tray icon, start-with-Windows toggle, CSV export, desktop power estimate.
