#pragma once

#include <string>

namespace wp {

// Append-only diagnostics log. Safe to call from any thread. Until log_init()
// runs, messages go to the console only.
void log_init(const std::string& path, bool console = true);
void log_info(const std::string& message);
void log_warn(const std::string& message);
void log_error(const std::string& message);

}  // namespace wp
