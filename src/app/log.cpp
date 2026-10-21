#include "app/log.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

namespace wp {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
bool g_console = true;

std::string timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
  return buf;
}

void write_line(const char* level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string line = timestamp() + " [" + level + "] " + message + "\n";
  if (g_file) {
    g_file << line;
    g_file.flush();
  }
  if (g_console) {
    std::fprintf(stderr, "wattpulse: %s\n", message.c_str());
    std::fflush(stderr);
  }
}

}  // namespace

void log_init(const std::string& path, bool console) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_console = console;
  if (g_file.is_open()) g_file.close();
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (!ec && size > 2 * 1024 * 1024) {
    const std::string rotated = path + ".1";
    std::filesystem::remove(rotated, ec);
    std::filesystem::rename(path, rotated, ec);
  }
  g_file.open(path, std::ios::app);
}

void log_info(const std::string& message) { write_line("info", message); }
void log_warn(const std::string& message) { write_line("warn", message); }
void log_error(const std::string& message) { write_line("error", message); }

}  // namespace wp
