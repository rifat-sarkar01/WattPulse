#include <catch_amalgamated.hpp>

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
