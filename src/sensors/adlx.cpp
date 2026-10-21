#include "sensors/adlx.h"

#include <windows.h>

// Vendored ADLX SDK headers (C++ interfaces in namespace adlx).
#include <ADLX.h>
#include <IPerformanceMonitoring.h>

namespace wp {

namespace {

using ADLXQueryFullVersion_Fn = ADLX_RESULT(ADLX_CDECL_CALL*)(adlx_uint64*);
using ADLXQueryVersion_Fn = ADLX_RESULT(ADLX_CDECL_CALL*)(const char**);
using ADLXInitialize2_Fn =
    ADLX_RESULT(ADLX_CDECL_CALL*)(adlx_uint64, adlx::IADLXSystem**, adlx::IADLMapping**);
using ADLXInitialize_Fn = ADLX_RESULT(ADLX_CDECL_CALL*)(adlx_uint64, adlx::IADLXSystem**);
using ADLXTerminate_Fn = ADLX_RESULT(ADLX_CDECL_CALL*)();

}  // namespace

AdlxSensor::AdlxSensor() = default;

AdlxSensor::~AdlxSensor() { shutdown(); }

bool AdlxSensor::init() {
  HMODULE mod = LoadLibraryW(ADLX_DLL_NAMEW);
  if (!mod) return false;
  module_ = mod;

  auto query_full = reinterpret_cast<ADLXQueryFullVersion_Fn>(
      GetProcAddress(mod, ADLX_QUERY_FULL_VERSION_FUNCTION_NAME));
  auto query_version = reinterpret_cast<ADLXQueryVersion_Fn>(
      GetProcAddress(mod, ADLX_QUERY_VERSION_FUNCTION_NAME));
  auto init2 = reinterpret_cast<ADLXInitialize2_Fn>(
      GetProcAddress(mod, ADLX_INIT2_FUNCTION_NAME));
  auto init1 = reinterpret_cast<ADLXInitialize_Fn>(
      GetProcAddress(mod, ADLX_INIT_FUNCTION_NAME));
  auto terminate = reinterpret_cast<ADLXTerminate_Fn>(
      GetProcAddress(mod, ADLX_TERMINATE_FUNCTION_NAME));
  if (!query_full || !query_version || !terminate || (!init2 && !init1)) {
    shutdown();
    return false;
  }

  adlx::IADLXSystem* system = nullptr;
  if (init2) {
    adlx::IADLMapping* mapping = nullptr;
    const ADLX_RESULT res = init2(ADLX_FULL_VERSION, &system, &mapping);
    // IADLMapping is a singleton: no Release. (see ISystem.h)
    if (ADLX_FAILED(res) || !system) {
      shutdown();
      return false;
    }
  } else {
    const ADLX_RESULT res = init1(ADLX_FULL_VERSION, &system);
    if (ADLX_FAILED(res) || !system) {
      shutdown();
      return false;
    }
  }
  system_ = system;

  adlx::IADLXPerformanceMonitoringServices* perf = nullptr;
  if (ADLX_FAILED(system->GetPerformanceMonitoringServices(&perf)) || !perf) {
    shutdown();
    return false;
  }
  perf_services_ = perf;

  adlx::IADLXGPUList* gpus = nullptr;
  if (ADLX_FAILED(system->GetGPUs(&gpus)) || !gpus) {
    shutdown();
    return false;
  }
  const adlx_uint n = gpus->Size();

  // Probe once: can we actually read power for any GPU?
  bool any_power = false;
  for (adlx_uint i = 0; i < n && !any_power; ++i) {
    adlx::IADLXGPU* gpu = nullptr;
    if (ADLX_FAILED(gpus->At(i, &gpu)) || !gpu) continue;
    adlx::IADLXGPUMetricsSupport* sup = nullptr;
    if (ADLX_SUCCEEDED(perf->GetSupportedGPUMetrics(gpu, &sup)) && sup) {
      adlx_bool supported = false;
      sup->IsSupportedGPUPower(&supported);
      sup->Release();
      if (supported) {
        adlx::IADLXGPUMetrics* metrics = nullptr;
        if (ADLX_SUCCEEDED(perf->GetCurrentGPUMetrics(gpu, &metrics)) && metrics) {
          adlx_double watts = 0.0;
          if (ADLX_SUCCEEDED(metrics->GPUPower(&watts))) any_power = true;
          metrics->Release();
        }
      }
    }
    gpu->Release();
  }
  gpu_count_ = n;
  gpus->Release();

  ok_ = any_power;
  if (!ok_) shutdown();
  return ok_;
}

void AdlxSensor::shutdown() {
  if (perf_services_) {
    static_cast<adlx::IADLXPerformanceMonitoringServices*>(perf_services_)->Release();
    perf_services_ = nullptr;
  }
  // IADLXSystem is a singleton: never Release()'d (see ISystem.h).
  system_ = nullptr;
  if (module_) {
    auto terminate = reinterpret_cast<ADLXTerminate_Fn>(
        GetProcAddress(static_cast<HMODULE>(module_), ADLX_TERMINATE_FUNCTION_NAME));
    if (terminate) terminate();
    FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
  }
  gpu_count_ = 0;
  ok_ = false;
}

bool AdlxSensor::available() {
  if (!tried_) {
    tried_ = true;
    init();
  }
  return ok_;
}

void AdlxSensor::poll(std::vector<Sample>& out) {
  if (!available()) return;
  auto* system = static_cast<adlx::IADLXSystem*>(system_);
  auto* perf = static_cast<adlx::IADLXPerformanceMonitoringServices*>(perf_services_);

  adlx::IADLXGPUList* gpus = nullptr;
  if (ADLX_FAILED(system->GetGPUs(&gpus)) || !gpus) return;
  const adlx_uint n = gpus->Size();
  for (adlx_uint i = 0; i < n; ++i) {
    adlx::IADLXGPU* gpu = nullptr;
    if (ADLX_FAILED(gpus->At(i, &gpu)) || !gpu) continue;
    adlx::IADLXGPUMetrics* metrics = nullptr;
    if (ADLX_SUCCEEDED(perf->GetCurrentGPUMetrics(gpu, &metrics)) && metrics) {
      adlx_double watts = 0.0;
      if (ADLX_SUCCEEDED(metrics->GPUPower(&watts)) && watts >= 0.0) {
        Sample s;
        s.domain = Domain::Gpu;
        s.watts = watts;
        s.provenance = Provenance::Measured;
        s.sourceId = "adlx:" + std::to_string(i);
        out.push_back(std::move(s));
      }
      metrics->Release();
    }
    gpu->Release();
  }
  gpus->Release();
}

}  // namespace wp
