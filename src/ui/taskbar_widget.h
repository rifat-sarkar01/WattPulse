#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

#include "storage/store.h"

namespace wp {

enum class WidgetSpan { None, Hour, Day, Week, Month };

WidgetSpan parse_widget_span(const std::string& value);
std::string widget_span_name(WidgetSpan span);

struct WidgetData {
  std::optional<double> watts;
  bool measured = false;
  Totals totals;
  bool dark = true;
  bool demo = false;
};

// Compact always-on-top meter docked next to the taskbar. It is not a shell
// deskband (Windows 11 does not support them), it follows the taskbar edge.
class TaskbarWidget {
 public:
  TaskbarWidget();
  ~TaskbarWidget();

  TaskbarWidget(const TaskbarWidget&) = delete;
  TaskbarWidget& operator=(const TaskbarWidget&) = delete;

  bool create(HINSTANCE instance, WidgetSpan span);
  void destroy();

  void set_data(const WidgetData& data);
  void set_span(WidgetSpan span);
  WidgetSpan span() const { return span_; }

  std::function<void(WidgetSpan)> on_span_changed;
  std::function<void()> on_open_dashboard;
  std::function<void()> on_unpin;

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  void position();
  void paint(HDC dc);
  void ensure_fonts();
  void release_fonts();
  void show_menu();
  int dip(int value) const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  WidgetSpan span_ = WidgetSpan::None;
  WidgetData data_;
  bool hover_ = false;
  bool tracking_mouse_ = false;
  RECT placed_{};

  HFONT value_font_ = nullptr;
  HFONT total_font_ = nullptr;
  HFONT micro_font_ = nullptr;
  UINT font_dpi_ = 0;
};

}  // namespace wp
