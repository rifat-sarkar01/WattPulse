#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "core/energy_integrator.h"

struct sqlite3;

namespace wp {

struct Totals {
  double mwh_last_hour = 0.0;
  double mwh_last_day = 0.0;
  double mwh_last_week = 0.0;
  double mwh_last_month = 0.0;
  double mwh_estimated = 0.0;   // estimated share over the month window
  double mwh_measured = 0.0;
};

// SQLite store: one row per minute holding mWh per domain + flags.
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // ":memory:" or a filesystem path. Returns false on failure (never throws).
  bool open(const std::string& path, int keep_days = 35, double now_unix = 0.0);
  void close();
  bool is_open() const { return db_ != nullptr; }

  bool write(const std::map<std::int64_t, MinuteBucket>& rows);
  Totals totals(double now_unix) const;

  const std::string& path() const { return path_; }
  // Why the last open/write failed. Empty after a successful call.
  const std::string& last_error() const { return last_error_; }

 private:
  bool exec(const char* sql);
  bool ensure_schema();
  bool table_columns(std::set<std::string>& columns) const;
  bool add_missing_columns(const std::set<std::string>& columns);
  bool migrate_v1(const std::set<std::string>& columns);
  bool fail(const std::string& message);

  sqlite3* db_ = nullptr;
  std::string path_;
  std::string last_error_;
  int keep_days_ = 35;
};

}  // namespace wp
