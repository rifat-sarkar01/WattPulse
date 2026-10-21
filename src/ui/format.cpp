#include "ui/format.h"

#include <cmath>
#include <cstdio>

namespace wp {

std::string fmt_watts(std::optional<double> w) {
  if (!w.has_value() || !std::isfinite(*w)) return "\xE2\x80\x94";  // em dash
  char buf[32];
  if (*w < 100.0) {
    std::snprintf(buf, sizeof(buf), "%.1f", *w);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f", *w);
  }
  return buf;
}

std::string fmt_energy(double mwh) {
  const double wh = mwh / 1000.0;
  char buf[32];
  if (wh >= 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f kWh", wh / 1000.0);
  } else if (wh >= 100.0) {
    std::snprintf(buf, sizeof(buf), "%.0f Wh", wh);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f Wh", wh);
  }
  return buf;
}

}  // namespace wp
