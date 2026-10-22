#include <catch_amalgamated.hpp>

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>

#include "storage/store.h"

using wp::FLAG_ESTIMATED;
using wp::FLAG_MEASURED;
using wp::MinuteBucket;
using wp::Store;

namespace {
std::string temp_db() {
  const auto dir = std::filesystem::temp_directory_path();
  const auto name = dir / ("wattpulse_test_" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()) +
                           ".db");
  return name.string();
}
}  // namespace

TEST_CASE("write and read totals") {
  Store store;
  REQUIRE(store.open(":memory:", 35, 1000000.0));
  const std::int64_t now = 1000000;
  std::map<std::int64_t, MinuteBucket> rows;
  MinuteBucket b;
  b.mwh_total = 1000.0;  // 1 Wh
  b.mwh_cpu = 600.0;
  b.mwh_gpu = 200.0;
  b.mwh_other = 200.0;
  b.flags = FLAG_MEASURED;
  rows[now - 30] = b;  // 30 s ago
  REQUIRE(store.write(rows));

  wp::Totals t = store.totals(static_cast<double>(now));
  REQUIRE(t.mwh_last_hour == Catch::Approx(1000.0));
  REQUIRE(t.mwh_last_day == Catch::Approx(1000.0));
  REQUIRE(t.mwh_measured == Catch::Approx(1000.0));
  REQUIRE(t.mwh_estimated == Catch::Approx(0.0));
}

TEST_CASE("series groups rows into evenly spaced buckets") {
  Store store;
  REQUIRE(store.open(":memory:", 35, 1000000.0));
  const std::int64_t now = 1000000;
  std::map<std::int64_t, MinuteBucket> rows;
  MinuteBucket a;
  a.mwh_total = 100.0;
  a.mwh_cpu = 40.0;
  rows[998850] = a;
  MinuteBucket b;
  b.mwh_total = 60.0;
  b.mwh_estimated = 60.0;
  rows[999020] = b;
  MinuteBucket c;
  c.mwh_total = 250.0;
  c.mwh_gpu = 200.0;
  rows[999450] = c;
  REQUIRE(store.write(rows));

  const auto series = store.series(static_cast<double>(now), 1200, 300);
  REQUIRE(series.size() == 4);
  REQUIRE(series[0].ts == 998800);
  REQUIRE(series[1].ts == 999100);
  REQUIRE(series[2].ts == 999400);
  REQUIRE(series[0].mwh_total == Catch::Approx(160.0));
  REQUIRE(series[0].mwh_cpu == Catch::Approx(40.0));
  REQUIRE(series[1].mwh_total == Catch::Approx(0.0));
  REQUIRE(series[2].mwh_total == Catch::Approx(250.0));
  REQUIRE(series[2].mwh_gpu == Catch::Approx(200.0));
}

TEST_CASE("same minute accumulates") {
  Store store;
  REQUIRE(store.open(":memory:", 35, 1000000.0));
  const std::int64_t ts = 999960;
  std::map<std::int64_t, MinuteBucket> a, b;
  MinuteBucket x;
  x.mwh_total = 100.0;
  a[ts] = x;
  MinuteBucket y;
  y.mwh_total = 250.0;
  y.mwh_estimated = 250.0;
  y.flags = FLAG_ESTIMATED;
  b[ts] = y;
  REQUIRE(store.write(a));
  REQUIRE(store.write(b));
  wp::Totals t = store.totals(1000000.0);
  REQUIRE(t.mwh_last_hour == Catch::Approx(350.0));
  REQUIRE(t.mwh_estimated == Catch::Approx(250.0));
}

TEST_CASE("old rows are pruned on open") {
  const std::string path = temp_db();
  {
    Store store;
    REQUIRE(store.open(path, 35, 1000000.0));
    std::map<std::int64_t, MinuteBucket> rows;
    MinuteBucket old_row, new_row;
    old_row.mwh_total = 500.0;
    new_row.mwh_total = 700.0;
    rows[1000000 - 40 * 86400] = old_row;  // 40 days ago
    rows[1000000 - 60] = new_row;
    REQUIRE(store.write(rows));
  }
  {
    Store store;
    REQUIRE(store.open(path, 35, 1000000.0));
    wp::Totals t = store.totals(1000000.0);
    REQUIRE(t.mwh_last_month == Catch::Approx(700.0));
  }
  std::remove(path.c_str());
}

TEST_CASE("open failure returns false, not a crash") {
  Store store;
  // A directory path cannot be opened as a database.
  REQUIRE_FALSE(store.open("no/such/directory/at/all/x.db", 35, 0.0));
  REQUIRE_FALSE(store.is_open());
  const std::map<std::int64_t, MinuteBucket> nothing;
  REQUIRE(store.write(nothing));  // no-op while closed
}

namespace {
void remove_db_files(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + "-wal").c_str());
  std::remove((path + "-shm").c_str());
}

void exec_sql(const std::string& path, const char* sql) {
  sqlite3* db = nullptr;
  REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (err) sqlite3_free(err);
  sqlite3_close(db);
  REQUIRE(rc == SQLITE_OK);
}
}  // namespace

TEST_CASE("v1 database without an estimated column is migrated") {
  const std::string path = temp_db();
  const double now = 1000000.0;
  exec_sql(path,
           "CREATE TABLE energy (ts INTEGER PRIMARY KEY, mwh REAL NOT NULL);"
           "INSERT INTO energy(ts, mwh) VALUES(999880, 500);");

  Store store;
  REQUIRE(store.open(path, 35, now));
  REQUIRE(store.last_error().empty());

  std::map<std::int64_t, MinuteBucket> rows;
  MinuteBucket b;
  b.mwh_total = 250.0;
  b.flags = FLAG_MEASURED;
  rows[999940] = b;
  REQUIRE(store.write(rows));

  const wp::Totals t = store.totals(now);
  REQUIRE(t.mwh_last_hour == Catch::Approx(750.0));
  REQUIRE(t.mwh_estimated == Catch::Approx(0.0));
  store.close();
  remove_db_files(path);
}

TEST_CASE("v1 database with an estimated column preserves old energy") {
  const std::string path = temp_db();
  const double now = 1000000.0;
  exec_sql(path,
           "CREATE TABLE energy (ts INTEGER PRIMARY KEY, mwh REAL NOT NULL,"
           " mwh_estimated REAL NOT NULL DEFAULT 0);"
           "INSERT INTO energy(ts, mwh, mwh_estimated) VALUES(999880, 400, 400);");

  Store store;
  REQUIRE(store.open(path, 35, now));
  REQUIRE(store.last_error().empty());

  const wp::Totals t = store.totals(now);
  REQUIRE(t.mwh_last_hour == Catch::Approx(400.0));
  REQUIRE(t.mwh_estimated == Catch::Approx(400.0));
  REQUIRE(t.mwh_measured == Catch::Approx(0.0));
  store.close();
  remove_db_files(path);
}