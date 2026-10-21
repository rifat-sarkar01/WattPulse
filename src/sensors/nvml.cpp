#include "sensors\nvml.h"

#include <windows.h>

#include <vector>

#include "sensors/parse.h"

namespace wp {

namespace {

using NvmlReturn = int;
constexpr NvmlReturn NVML_SUCCESS = 0;

using NvmlInit_t = NvmlReturn (*)();
using NvmlShutdown_t = NvmlReturn (*)();
using NvmlDeviceGetCount_t = NvmlReturn (*)(unsigned*);
using NvmlDeviceGetHandleByIndex_t = NvmlReturn (*)(unsigned, void**);
using NvmlDeviceGetPowerUsage_t = NvmlReturn (*)(void*, unsigned*);  // milliwatts
using NvmlDeviceGetName_t = NvmlReturn (*)(void*, char*, unsigned);

struct NvmlApi {
  NvmlInit_t init = nullptr;
  NvmlShutdown_t shutdown = nullptr;
  NvmlDeviceGetCount_t get_count = nullptr;
  NvmlDeviceGetHandleByIndex_t get_handle = nullptr;
  NvmlDeviceGetPowerUsage_t get_power = nullptr;
  NvmlDeviceGetName_t get_name = nullptr;
  std::vector<void*> handles;
};

NvmlApi g_api;

}  // namespace

NvmlSensor::NvmlSensor() = default;

NvmlSensor::~NvmlSensor() { shutdown(); }

bool NvmlSensor::init() {
  HMODULE mod = LoadLibraryW(L"nvml.dll");
  if (!mod) {
    // shipped with the NVIDIA driver as nvml.dll in System32, or via
    // Program Files\NVIDIA Corporation\NVSMI
    mod = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
  }
  if (!mod) return false;
  module_ = mod;

  g_api.init = reinterpret_cast<NvmlInit_t>(GetProcAddress(mod, "nvmlInit_v2"));
  if (!g_api.init) g_api.init = reinterpret_cast<NvmlInit_t>(GetProcAddress(mod, "nvmlInit"));
  g_api.shutdown = reinterpret_cast<NvmlShutdown_t>(GetProcAddress(mod, "nvmlShutdown"));
  g_api.get_count =
      reinterpret_cast<NvmlDeviceGetCount_t>(GetProcAddress(mod, "nvmlDeviceGetCount_v2"));
  if (!g_api.get_count) {
    g_api.get_count =
        reinterpret_cast<NvmlDeviceGetCount_t>(GetProcAddress(mod, "nvmlDeviceGetCount"));
  }
  g_api.get_handle = reinterpret_cast<NvmlDeviceGetHandleByIndex_t>(
      GetProcAddress(mod, "nvmlDeviceGetHandleByIndex_v2"));
  if (!g_api.get_handle) {
    g_api.get_handle = reinterpret_cast<NvmlDeviceGetHandleByIndex_t>(
        GetProcAddress(mod, "nvmlDeviceGetHandleByIndex"));
  }
  g_api.get_power =
      reinterpret_cast<NvmlDeviceGetPowerUsage_t>(GetProcAddress(mod, "nvmlDeviceGetPowerUsage"));
  g_api.get_name =
      reinterpret_cast<NvmlDeviceGetName_t>(GetProcAddress(mod, "nvmlDeviceGetName_v2"));
  if (!g_api.get_name) {
    g_api.get_name =
        reinterpret_cast<NvmlDeviceGetName_t>(GetProcAddress(mod, "nvmlDeviceGetName"));
  }
  if (!g_api.init || !g_api.shutdown || !g_api.get_count || !g_api.get_handle || !g_api.get_power) {
    shutdown();
    return false;
  }
  if (g_api.init() != NVML_SUCCESS) {
    shutdown();
    return false;
  }
  unsigned count = 0;
  if (g_api.get_count(&count) != NVML_SUCCESS) {
    shutdown();
    return false;
  }
  device_count_ = count;
  for (unsigned i = 0; i < count; ++i) {
    void* dev = nullptr;
    if (g_api.get_handle(i, &dev) == NVML_SUCCESS) {
      g_api.handles.push_back(dev);
    }
  }
  ok_ = !g_api.handles.empty();
  return ok_;
}

void NvmlSensor::shutdown() {
  if (module_ && g_api.shutdown) g_api.shutdown();
  g_api = NvmlApi{};
  if (module_) {
    FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
  }
  ok_ = false;
}

bool NvmlSensor::available() {
  if (!tried_) {
    tried_ = true;
    init();
  }
  return ok_;
}

void NvmlSensor::poll(std::vector<Sample>& out) {
  if (!available()) return;
  for (std::size_t i = 0; i < g_api.handles.size(); ++i) {
    unsigned mw = 0;
    if (g_api.get_power(g_api.handles[i], &mw) != NVML_SUCCESS) continue;
    Sample s;
    s.domain = Domain::Gpu;
    s.watts = milliwatts_to_watts(mw);
    s.provenance = Provenance::Measured;
    s.sourceId = "nvml:" + std::to_string(i);
    out.push_back(std::move(s));
  }
}

}  // namespace wp
