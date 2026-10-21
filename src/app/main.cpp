#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "app/platform_win.h"
#include "app/settings.h"
#include "core/estimator.h"
#include "core/sampler.h"
#include "sensors/factory.h"
#include "storage/store.h"
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
      "usage: wattpulse [--run] [--demo] [--probe] [--silent]\n"
      "                 [--calibrate <wall-watts>] [--autostart on|off]\n"
      "                 [--version] [--help]\n"
      "\n"
      "  --run               sample at 1 Hz and print a live line (default)\n"
      "  --demo              synthetic data; nothing is saved\n"
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
    std::fprintf(stderr, "wattpulse: could not write %s\n", path.c_str());
    return 1;
  }
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

  wp::Calibration cal;
  cal.base_watts = settings.base_watts;
  cal.calibrated = settings.calibrated;
  wp::PowerModel model(cal, wp::parse_form_factor(settings.form_factor));

  std::vector<std::unique_ptr<wp::ISensor>> sensors =
      opts.demo ? wp::make_demo_sensors() : wp::make_sensors(settings);

  wp::Store store;
  const bool persist = !opts.demo;
  if (persist) {
    store.open(dir + "\\power.db", 35, wp::wall_seconds());
  }

  wp::Sampler sampler(std::move(sensors), model);
  sampler.start();

  std::uint64_t last_flush = 0;
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    wp::Snapshot snap = sampler.snapshot();

    // flush every 30 s (upsert += in the store, so partial minutes stay correct)
    if (persist && snap.ticks - last_flush >= 30) {
      last_flush = snap.ticks;
      auto drained = sampler.drain_pending();
      if (!drained.empty()) store.write(drained);
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

  if (persist) {
    auto drained = sampler.drain_pending();
    store.write(drained);
    wp::Totals t = store.totals(wp::wall_seconds());
    std::printf("\n");
    print_totals_line(t);
  } else {
    std::printf("\ndemo mode: nothing was saved\n");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  bool demo = false, probe = false, silent = false, want_run = false, help = false;
  bool calibrate = false, autostart = false;
  double cal_watts = 0.0;
  bool autostart_enable = false;
  bool version = false;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--demo") == 0) {
      demo = true;
    } else if (std::strcmp(a, "--probe") == 0) {
      probe = true;
    } else if (std::strcmp(a, "--run") == 0) {
      want_run = true;
    } else if (std::strcmp(a, "--silent") == 0) {
      silent = true;
    } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      help = true;
    } else if (std::strcmp(a, "--version") == 0) {
      version = true;
    } else if (std::strcmp(a, "--calibrate") == 0 && i + 1 < argc) {
      calibrate = true;
      cal_watts = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--autostart") == 0 && i + 1 < argc) {
      autostart = true;
      autostart_enable = (std::strcmp(argv[++i], "on") == 0);
    } else {
      std::fprintf(stderr, "wattpulse: unknown argument %s\n", a);
      print_usage();
      return 2;
    }
  }

  if (version) {
    std::printf("WattPulse 2.0.0\n");
    return 0;
  }
  if (help) {
    print_usage();
    return 0;
  }
  if (probe) return cmd_probe();

  if (autostart) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (wp::set_autostart(autostart_enable, exe)) {
      std::printf("autostart %s\n", autostart_enable ? "enabled" : "disabled");
      wp::AppSettings s = wp::load_settings(wp::data_dir() + "\\settings.ini");
      s.autostart = autostart_enable;
      wp::save_settings(wp::data_dir() + "\\settings.ini", s);
      return 0;
    }
    std::fprintf(stderr, "wattpulse: could not update the Run key\n");
    return 1;
  }
  if (calibrate) return cmd_calibrate(cal_watts);

  // --run / default live loop. Single instance so totals are not double-counted.
  if (!demo && !wp::acquire_single_instance(L"Local\\WattPulse.SingleInstance")) {
    std::fprintf(stderr, "wattpulse: another instance is already running\n");
    return 0;
  }

  SetConsoleCtrlHandler(on_console_ctrl, TRUE);
  LiveOptions opts;
  opts.demo = demo;
  opts.silent = silent;
  return cmd_live(opts);
}
