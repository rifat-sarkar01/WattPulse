#include "storage/store.h"

#include <sqlite3.h>

namespace wp {

Store::~Store() { close(); }

bool Store::exec(const char* sql) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (err) sqlite3_free(err);
  return rc == SQLITE_OK;
}

bool Store::open(const std::string& path, int keep_days, double now_unix) {
  close();
  path_ = path;
  keep_days_ = keep_days;
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    close();
    return false;
  }
  exec("PRAGMA journal_mode=WAL");
  exec("PRAGMA synchronous=NORMAL");
  const bool ok =
      exec("CREATE TABLE IF NOT EXISTS energy ("
           "ts INTEGER PRIMARY KEY,"
           "mwh_total REAL NOT NULL DEFAULT 0,"
           "mwh_cpu REAL NOT NULL DEFAULT 0,"
           "mwh_gpu REAL NOT NULL DEFAULT 0,"
           "mwh_other REAL NOT NULL DEFAULT 0,"
           "mwh_estimated REAL NOT NULL DEFAULT 0,"
           "flags INTEGER NOT NULL DEFAULT 0)");
  if (!ok) {
    close();
    return false;
  }
  // databases created before the estimated-share column existed
  exec("ALTER TABLE energy ADD COLUMN mwh_estimated REAL NOT NULL DEFAULT 0");
  if (now_unix > 0.0) {
    const std::int64_t cut =
        static_cast<std::int64_t>(now_unix) - static_cast<std::int64_t>(keep_days_) * 86400;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM energy WHERE ts < ?", -1, &stmt, nullptr) ==
        SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, cut);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }
  return true;
}

void Store::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool Store::write(const std::map<std::int64_t, MinuteBucket>& rows) {
  if (!db_ || rows.empty()) return true;
  if (!exec("BEGIN")) return false;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO energy(ts, mwh_total, mwh_cpu, mwh_gpu, mwh_other, mwh_estimated, flags) "
      "VALUES(?,?,?,?,?,?,?) "
      "ON CONFLICT(ts) DO UPDATE SET "
      "mwh_total = mwh_total + excluded.mwh_total,"
      "mwh_cpu = mwh_cpu + excluded.mwh_cpu,"
      "mwh_gpu = mwh_gpu + excluded.mwh_gpu,"
      "mwh_other = mwh_other + excluded.mwh_other,"
      "mwh_estimated = mwh_estimated + excluded.mwh_estimated,"
      "flags = flags | excluded.flags";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    exec("ROLLBACK");
    return false;
  }
  for (const auto& [ts, b] : rows) {
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_double(stmt, 2, b.mwh_total);
    sqlite3_bind_double(stmt, 3, b.mwh_cpu);
    sqlite3_bind_double(stmt, 4, b.mwh_gpu);
    sqlite3_bind_double(stmt, 5, b.mwh_other);
    sqlite3_bind_double(stmt, 6, b.mwh_estimated);
    sqlite3_bind_int(stmt, 7, static_cast<int>(b.flags));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      exec("ROLLBACK");
      return false;
    }
  }
  sqlite3_finalize(stmt);
  return exec("COMMIT");
}

Totals Store::totals(double now_unix) const {
  Totals t;
  if (!db_) return t;
  struct Window {
    const char* label;
    int seconds;
    double* out;
  };
  const std::int64_t now = static_cast<std::int64_t>(now_unix);
  const Window windows[] = {
      {"hour", 3600, &t.mwh_last_hour},
      {"day", 86400, &t.mwh_last_day},
      {"week", 7 * 86400, &t.mwh_last_week},
      {"month", 30 * 86400, &t.mwh_last_month},
  };
  for (const Window& w : windows) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COALESCE(SUM(mwh_total),0) FROM energy WHERE ts >= ?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, now - w.seconds);
      if (sqlite3_step(stmt) == SQLITE_ROW) *w.out = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }
  // estimated/measured split over the month window (column-based, so a minute
  // that mixes both is split correctly)
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT "
                         "COALESCE(SUM(mwh_estimated),0),"
                         "COALESCE(SUM(mwh_total - mwh_estimated),0) "
                         "FROM energy WHERE ts >= ?",
                         -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, now - 30 * 86400);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      t.mwh_estimated = sqlite3_column_double(stmt, 0);
      t.mwh_measured = sqlite3_column_double(stmt, 1);
    }
  }
  sqlite3_finalize(stmt);
  return t;
}

}  // namespace wp
