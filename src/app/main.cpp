#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "app/log.h"
#include "app/platform_win.h"
#include "app/settings.h"
#include "core/estimator.h"
#include "core/sampler.h"
#include "sensors/factory.h"
#include "storage/store.h"
#include "ui/dashboard.h"
#include "ui/format.h"

namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI on_console_ctrl(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
    g_stop = true;
    return TRUE;
  }
  return FALSE;
}

void print_usage() {
  std::printf(
      "WattPulse 2.0 - live power draw and energy totals\n"
      "\n"
      "usage: wattpulse [--ui] [--demo]\n"
      "       wattpulse --run [--demo] [--silent]\n"
      "       wattpulse --probe | --calibrate <wall-watts> | --autostart on|off\n"
      "       wattpulse --version | --help\n"
      "\n"
      "  (no arguments)      open the dashboard (default)\n"
      "  --ui                open the dashboard\n"
      "  --run               command-line live readout instead of the dashboard\n"
      "  --demo              synthetic data; demo mode saves nothing\n"
      "  --probe             print sensor availability and 5 raw readings\n"
      "  --silent            no live line (for autostart)\n"
      "  --calibrate <W>     set the modelled base from a wall-meter reading\n"
      "  --autostart on|off  start with Windows (HKCU Run key)\n");
}

const char* provenance_tag(wp::Provenance p) {
  return p == wp::Provenance::Estimated ? "Estimated" : "Measured";
}

void print_totals_line(const wp::Totals& t) {
  const double split = t.mwh_estimated + t.mwh_measured;
  int pct_est = 0;
  if (split > 0.0) pct_est = static_cast<int>(100.0 * t.mwh_estimated / split + 0.5);
  std::printf("  last hour %s | 24 h %s | 7 d %s | 30 d %s | %d%% estimated\n",
              wp::fmt_energy(t.mwh_last_hour).c_str(), wp::fmt_energy(t.mwh_last_day).c_str(),
              wp::fmt_energy(t.mwh_last_week).c_str(), wp::fmt_energy(t.mwh_last_month).c_str(),
              pct_est);
}

int cmd_probe() {
  wp::AppSettings settings = wp::load_settings(wp::data_dir() + "\\settings.ini");
  auto sensors = wp::make_sensors(settings);
  std::printf("probing %zu sensor(s)...\n", sensors.size());
  bool any_available = false;
  for (auto& s : sensors) {
    bool available = false;
    try {
      available = s->available();
    } catch (...) {
      available = false;
    }
    any_available = any_available || available;
    wp::log_info(std::string("probe: sensor ") + std::string(s->name()) +
                 (available ? " available" : " unavailable"));
  }
  if (!any_available) {
    std::printf("no power sensors available; run as a normal user and check your drivers.\n");
  }
  for (int round = 0; round < 5; ++round) {
    std::printf("-- round %d --\n", round);
    for (auto& s : sensors) {
      std::printf("  %-8s available=%s\n", std::string(s->name()).c_str(),
                  s->available() ? "yes" : "no");
      if (!s->available()) continue;
      std::vector<wp::Sample> samples;
      s->poll(samples);
      if (samples.empty()) {
        std::printf("           (no samples)\n");
      }
      for (const wp::Sample& sample : samples) {
        char watts[32];
        if (sample.watts.has_value()) {
          std::snprintf(watts, sizeof(watts), "%.2f W", *sample.watts);
        } else {
          std::snprintf(watts, sizeof(watts), "-");
        }
        std::printf("           [%s] %-10s %s (%s)\n", wp::domain_name(sample.domain),
                    sample.sourceId.c_str(), watts, provenance_tag(sample.provenance));
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}

int cmd_calibrate(double wall_watts) {
  if (wall_watts <= 0.0) {
    std::fprintf(stderr, "wattpulse: --calibrate needs a positive wall-watt reading\n");
    return 1;
  }
  const std::string dir = wp::data_dir();
  const std::string path = dir + "\\settings.ini";
  wp::AppSettings settings = wp::load_settings(path);

  // Sample the components now so the base is solved against live cpu+gpu.
  auto sensors = wp::make_sensors(settings);
  std::vector<wp::Sample> samples;
  for (int i = 0; i < 5; ++i) {
    for (auto& s : sensors) {
      if (!s->available()) continue;
      s->poll(samples);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  double cpu = 0.0, gpu = 0.0;
  for (const wp::Sample& s : samples) {
    if (!s.watts.has_value()) continue;
    if (s.domain == wp::Domain::CpuPackage && *s.watts > cpu) cpu = *s.watts;
    if (s.domain == wp::Domain::Gpu && *s.watts > gpu) gpu = *s.watts;
  }

  const double base = wp::calibrate_base(wall_watts, cpu, gpu);
  settings.base_watts = base;
  settings.calibrated = true;
  if (!wp::save_settings(path, settings)) {
    wp::log_error("could not write settings: " + path);
    std::fprintf(stderr, "wattpulse: could not write %s\n", path.c_str());
    return 1;
  }
  wp::log_info("calibrated base=" + std::to_string(base) + " cpu=" + std::to_string(cpu) +
               " gpu=" + std::to_string(gpu) + " wall=" + std::to_string(wall_watts));
  std::printf("calibrated: base=%.1f W (cpu=%.1f gpu=%.1f, wall=%.1f W) -> %s\n", base, cpu, gpu,
              wall_watts, path.c_str());
  return 0;
}

struct LiveOptions {
  bool demo = false;
  bool silent = false;
};

int cmd_live(const LiveOptions& opts) {
  const std::string dir = wp::data_dir();
  wp::AppSettings settings = wp::load_settings(dir + "\\settings.ini");
  wp::log_info("settings: base=" + std::to_string(settings.base_watts) +
               " calibrated=" + (settings.calibrated ? "true" : "false") +
               " scale=" + std::to_string(settings.emi_scale) +
               " form_factor=" + settings.form_factor);

  wp::Calibration cal;
  cal.base_watts = settings.base_watts;
  cal.calibrated = settings.calibrated;
  wp::PowerModel model(cal, wp::parse_form_factor(settings.form_factor));

  std::vector<std::unique_ptr<wp::ISensor>> sensors =
      opts.demo ? wp::make_demo_sensors() : wp::make_sensors(settings);

  bool any_available = false;
  for (auto& s : sensors) {
    bool available = false;
    try {
      available = s->available();
    } catch (...) {
      available = false;
    }
    any_available = any_available || available;
    wp::log_info(std::string("sensor ") + std::string(s->name()) +
                 (available ? ": available" : ": unavailable"));
  }
  if (!any_available) {
    wp::log_warn("no power sensors are available");
    std::fprintf(stderr,
                 "wattpulse: no power sensors found; live readings will be empty. "
                 "Run `wattpulse --probe` and see docs/sensor-matrix.md.\n");
  }

  wp::Store store;
  const bool persist = !opts.demo;
  bool store_ok = false;
  if (persist) {
    store_ok = store.open(dir + "\\power.db", 35, wp::wall_seconds());
    if (store_ok) {
      wp::log_info("storage: " + store.path());
    } else {
      wp::log_error("storage open failed: " + store.last_error());
      std::fprintf(stderr,
                   "wattpulse: storage unavailable (%s); live readings continue without "
                   "totals.\n",
                   store.last_error().c_str());
    }
  }

  wp::Sampler sampler(std::move(sensors), model);
  sampler.start();

  auto flush_pending = [&]() {
    auto drained = sampler.drain_pending();
    if (drained.empty()) return true;
    if (!store.write(drained)) {
      wp::log_error("storage write failed: " + store.last_error() + "; retrying later");
      sampler.restore_pending(drained);
      return false;
    }
    return true;
  };

  std::uint64_t last_flush = 0;
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    wp::Snapshot snap = sampler.snapshot();

    // flush every 30 s (upsert += in the store, so partial minutes stay correct)
    if (store_ok && snap.ticks - last_flush >= 30) {
      last_flush = snap.ticks;
      flush_pending();
    }

    if (!opts.silent) {
      const wp::ModelOutput& m = snap.model;
      std::string total = wp::fmt_watts(m.total_watts);
      std::string cpu = wp::fmt_watts(m.cpu_watts);
      std::string gpu = wp::fmt_watts(m.gpu_watts);
      std::string other = wp::fmt_watts(m.other_watts);
      std::printf("\r%-42s cpu %-6s gpu %-6s other %-6s   ", total.c_str(), cpu.c_str(),
                  gpu.c_str(), other.c_str());
      if (m.total_watts.has_value()) {
        std::printf("[%s %s] ", provenance_tag(m.total_provenance), m.sourceId.c_str());
      } else {
        std::printf("[no whole-system sensor] ");
      }
      std::fflush(stdout);
    }
  }

  sampler.stop();

  if (persist && store_ok) {
    const bool flushed = flush_pending();
    if (!flushed) {
      std::fprintf(stderr, "wattpulse: last measurements could not be saved; see the log.\n");
    }
    wp::Totals t = store.totals(wp::wall_seconds());
    std::printf("\n");
    print_totals_line(t);
    wp::log_info("totals: hour=" + std::to_string(t.mwh_last_hour) +
                 " day=" + std::to_string(t.mwh_last_day) +
                 " week=" + std::to_string(t.mwh_last_week) +
                 " month=" + std::to_string(t.mwh_last_month) + " mWh");
  } else if (persist) {
    std::printf("\nstorage unavailable: totals were not saved\n");
  } else {
    std::printf("\ndemo mode: nothing was saved\n");
  }
  return 0;
}

struct Options {
  bool demo = false;
  bool probe = false;
  bool silent = false;
  bool run = false;
  bool help = false;
  bool version = false;
  bool calibrate = false;
  bool autostart = false;
  bool autostart_enable = false;
  bool ui = false;
  bool console = false;
  double cal_watts = 0.0;
  std::string unknown;
};

std::string narrow(const wchar_t* text) {
  if (!text || !*text) return {};
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

Options parse_options(const std::vector<std::string>& args) {
  Options opts;
  opts.console = false;
  for (const std::string& argument : args) {
    if (argument == "--demo" || argument == "--ui") continue;
    opts.console = true;
    break;
  }
  opts.ui = !opts.console;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--demo") {
      opts.demo = true;
    } else if (a == "--ui") {
      // The dashboard is already selected when no console action is present.
    } else if (a == "--probe") {
      opts.probe = true;
    } else if (a == "--run") {
      opts.run = true;
    } else if (a == "--silent") {
      opts.silent = true;
    } else if (a == "--help" || a == "-h") {
      opts.help = true;
    } else if (a == "--version") {
      opts.version = true;
    } else if (a == "--calibrate" && i + 1 < args.size()) {
      opts.calibrate = true;
      opts.cal_watts = std::atof(args[++i].c_str());
    } else if (a == "--autostart" && i + 1 < args.size()) {
      opts.autostart = true;
      opts.autostart_enable = (args[++i] == "on");
    } else {
      opts.unknown = a;
      break;
    }
  }

  // --silent only makes sense for the headless live loop.
  if (opts.console && opts.silent && !opts.probe && !opts.help && !opts.version &&
      !opts.calibrate && !opts.autostart) {
    opts.run = true;
  }
  if (opts.run || opts.probe || opts.help || opts.version || opts.calibrate || opts.autostart) {
    opts.ui = false;
  }
  return opts;
}

bool stream_is_redirected(DWORD standard_handle) {
  const HANDLE handle = GetStdHandle(standard_handle);
  if (!handle || handle == INVALID_HANDLE_VALUE) return false;
  const DWORD type = GetFileType(handle);
  return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
}

void attach_console(bool create_if_missing) {
  const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
  if (!attached && create_if_missing) AllocConsole();
  if (!attached && !create_if_missing) return;

  FILE* stream = nullptr;
  if (!stream_is_redirected(STD_OUTPUT_HANDLE)) {
    freopen_s(&stream, "CONOUT$", "w", stdout);
  }
  if (!stream_is_redirected(STD_ERROR_HANDLE)) {
    freopen_s(&stream, "CONOUT$", "w", stderr);
  }
  if (!stream_is_redirected(STD_INPUT_HANDLE)) {
    freopen_s(&stream, "CONIN$", "r", stdin);
  }
}

int run_cli(const Options& opts) {
  if (opts.version) {
    std::printf("WattPulse 2.0.0\n");
    return 0;
  }
  if (opts.help) {
    print_usage();
    return 0;
  }
  if (opts.probe) return cmd_probe();

  if (opts.autostart) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (wp::set_autostart(opts.autostart_enable, exe)) {
      std::printf("autostart %s\n", opts.autostart_enable ? "enabled" : "disabled");
      wp::log_info(std::string("autostart ") +
                   (opts.autostart_enable ? "enabled" : "disabled"));
      wp::AppSettings s = wp::load_settings(wp::data_dir() + "\\settings.ini");
      s.autostart = opts.autostart_enable;
      wp::save_settings(wp::data_dir() + "\\settings.ini", s);
      return 0;
    }
    wp::log_error("could not update the Run key");
    std::fprintf(stderr, "wattpulse: could not update the Run key\n");
    return 1;
  }
  if (opts.calibrate) return cmd_calibrate(opts.cal_watts);

  if (!opts.demo && !wp::acquire_single_instance(L"Local\\WattPulse.SingleInstance")) {
    std::fprintf(stderr, "wattpulse: another instance is already running\n");
    return 0;
  }

  SetConsoleCtrlHandler(on_console_ctrl, TRUE);
  LiveOptions live;
  live.demo = opts.demo;
  live.silent = opts.silent;
  return cmd_live(live);
}

int run_app(HINSTANCE instance, int argc, wchar_t** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
  for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
  const Options opts = parse_options(args);

  if (opts.console) attach_console(!opts.silent);

  const std::string data_dir = wp::data_dir();
  const bool quiet_command = opts.version || opts.help;
  wp::log_init(data_dir + "\\wattpulse.log",
               opts.console && !opts.silent && !quiet_command);
  if (!quiet_command) {
    wp::log_info(std::string("WattPulse 2.0.0 starting; mode=") +
                 (opts.ui ? "dashboard" : "cli") + "; data=" + data_dir);
  }

  if (!opts.unknown.empty()) {
    if (opts.console) {
      std::fprintf(stderr, "wattpulse: unknown argument %s\n", opts.unknown.c_str());
      print_usage();
    } else {
      MessageBoxW(nullptr, L"Unknown command-line option.", L"WattPulse", MB_ICONERROR);
    }
    return 2;
  }
  if (!opts.ui) return run_cli(opts);

  if (!wp::acquire_single_instance(L"Local\\WattPulse.SingleInstance")) {
    if (!wp::focus_dashboard_window()) {
      MessageBoxW(nullptr, L"WattPulse is already running in the background.", L"WattPulse",
                  MB_ICONINFORMATION);
    }
    return 0;
  }

  return wp::run_dashboard(instance, opts.demo);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 1;
  const int result = run_app(instance, argc, argv);
  LocalFree(argv);
  return result;
}
