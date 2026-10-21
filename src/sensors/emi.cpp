#include "sensors/emi.h"

#include <pdh.h>
#include <pdhmsg.h>

#include <cmath>

#pragma comment(lib, "pdh.lib")

namespace wp {

EmiSensor::EmiSensor(double scale) : scale_(scale) {}

EmiSensor::~EmiSensor() { close_queries(); }

void EmiSensor::close_queries() {
  for (HCOUNTER c : sources_) {
    if (query_) PdhRemoveCounter(c);
  }
  sources_.clear();
  paths_.clear();
  domains_.clear();
  if (query_) {
    PdhCloseQuery(query_);
    query_ = nullptr;
  }
  ok_ = false;
}

bool EmiSensor::open_queries() {
  close_queries();
  if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
    query_ = nullptr;
    return false;
  }

  // Enumerate all Energy Meter Power counters, keep PKG and PSYS instances.
  DWORD buf_size = 0;
  PdhExpandWildCardPathW(nullptr, L"\\Energy Meter(*)\\Power", nullptr, &buf_size,
                         PDH_NOEXPANDCOUNTERS);
  if (buf_size == 0) {
    close_queries();
    return false;
  }
  std::vector<wchar_t> buf(buf_size);
  if (PdhExpandWildCardPathW(nullptr, L"\\Energy Meter(*)\\Power", buf.data(), &buf_size,
                             PDH_NOEXPANDCOUNTERS) != ERROR_SUCCESS) {
    close_queries();
    return false;
  }

  for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
    std::wstring path(p);
    Domain d;
    if (path.find(L"_PKG)\\Power") != std::wstring::npos) {
      d = Domain::CpuPackage;
    } else if (path.find(L"_PSYS)\\Power") != std::wstring::npos) {
      d = Domain::SystemTotal;  // platform power ~= whole system on many CPUs
    } else {
      continue;  // core/DRAM counters: skip (package covers them)
    }
    HCOUNTER counter = nullptr;
    if (PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter) != ERROR_SUCCESS) {
      continue;
    }
    paths_.push_back(path);
    domains_.push_back(d);
    sources_.push_back(counter);
  }

  if (paths_.empty()) {
    close_queries();
    return false;
  }
  // Prime the query so the first real collect has a delta to work with.
  PdhCollectQueryData(query_);
  ok_ = true;
  return true;
}

bool EmiSensor::available() {
  if (!tried_) {
    tried_ = true;
    ok_ = open_queries();
  }
  return ok_;
}

void EmiSensor::poll(std::vector<Sample>& out) {
  if (!available()) return;
  if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
    ok_ = false;  // counter vanished (e.g. driver reload): degrade gracefully
    return;
  }
  for (std::size_t i = 0; i < sources_.size(); ++i) {
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(sources_[i], PDH_FMT_DOUBLE, nullptr, &value) !=
        ERROR_SUCCESS) {
      continue;
    }
    if (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) {
      continue;
    }
    const double raw = value.doubleValue;
    if (!std::isfinite(raw) || raw < 0.0) continue;
    Sample s;
    s.domain = domains_[i];
    s.watts = raw * scale_;
    s.provenance = Provenance::Measured;
    // sourceId: "emi:RAPL_Package0_PKG"
    std::wstring w = paths_[i];
    std::string id = "emi:";
    const auto open = w.find(L'(');
    const auto close = w.find(L')');
    if (open != std::wstring::npos && close != std::wstring::npos && close > open) {
      for (wchar_t c : w.substr(open + 1, close - open - 1)) id.push_back(static_cast<char>(c));
    } else {
      id += "pkg";
    }
    s.sourceId = std::move(id);
    out.push_back(std::move(s));
  }
}

}  // namespace wp
