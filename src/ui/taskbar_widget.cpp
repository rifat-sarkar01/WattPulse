#include "ui/taskbar_widget.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "ui/format.h"

namespace wp {
namespace {

constexpr wchar_t kWidgetClass[] = L"WattPulseTaskbarWidget";
constexpr wchar_t kWidgetTitle[] = L"WattPulse live power";
constexpr int kWidthDip = 248;
constexpr int kHeightDip = 50;
constexpr UINT kPositionTimer = 1;
constexpr UINT kDwmUseImmersiveDarkMode = 20;
constexpr UINT kDwmWindowCornerPreference = 33;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuNone = 10;
constexpr UINT kMenuHour = 11;
constexpr UINT kMenuDay = 12;
constexpr UINT kMenuWeek = 13;
constexpr UINT kMenuMonth = 14;
constexpr UINT kMenuUnpin = 20;

std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

struct Palette {
  COLORREF bg;
  COLORREF hover;
  COLORREF border;
  COLORREF text;
  COLORREF muted;
  COLORREF accent;
  COLORREF green;
  COLORREF amber;
  COLORREF red;
};

Palette palette(bool dark) {
  if (dark) {
    return {RGB(0x24, 0x25, 0x26), RGB(0x2F, 0x30, 0x31), RGB(0x3E, 0x40, 0x42),
            RGB(0xE4, 0xE6, 0xEB), RGB(0xB0, 0xB3, 0xB8), RGB(0x2D, 0x88, 0xFF),
            RGB(0x45, 0xBD, 0x62), RGB(0xF5, 0xB9, 0x42), RGB(0xF1, 0x6A, 0x7A)};
  }
  return {RGB(0xFF, 0xFF, 0xFF), RGB(0xF5, 0xF6, 0xF8), RGB(0xE4, 0xE6, 0xEB),
          RGB(0x05, 0x05, 0x05), RGB(0x65, 0x67, 0x6B), RGB(0x18, 0x77, 0xF2),
          RGB(0x31, 0xA2, 0x4C), RGB(0xB2, 0x6A, 0x00), RGB(0xE4, 0x1E, 0x3F)};
}

const wchar_t* span_title(WidgetSpan span) {
  switch (span) {
    case WidgetSpan::Hour: return L"1 HOUR";
    case WidgetSpan::Day: return L"24 HOURS";
    case WidgetSpan::Week: return L"7 DAYS";
    case WidgetSpan::Month: return L"30 DAYS";
    case WidgetSpan::None: break;
  }
  return L"";
}

double span_mwh(const Totals& totals, WidgetSpan span) {
  switch (span) {
    case WidgetSpan::Hour: return totals.mwh_last_hour;
    case WidgetSpan::Day: return totals.mwh_last_day;
    case WidgetSpan::Week: return totals.mwh_last_week;
    case WidgetSpan::Month: return totals.mwh_last_month;
    case WidgetSpan::None: break;
  }
  return 0.0;
}

}  // namespace

WidgetSpan parse_widget_span(const std::string& value) {
  if (value == "hour") return WidgetSpan::Hour;
  if (value == "day") return WidgetSpan::Day;
  if (value == "week") return WidgetSpan::Week;
  if (value == "month") return WidgetSpan::Month;
  return WidgetSpan::None;
}

std::string widget_span_name(WidgetSpan span) {
  switch (span) {
    case WidgetSpan::Hour: return "hour";
    case WidgetSpan::Day: return "day";
    case WidgetSpan::Week: return "week";
    case WidgetSpan::Month: return "month";
    case WidgetSpan::None: break;
  }
  return "none";
}

TaskbarWidget::TaskbarWidget() = default;

TaskbarWidget::~TaskbarWidget() { destroy(); }

bool TaskbarWidget::create(HINSTANCE instance, WidgetSpan span) {
  if (hwnd_) return true;
  instance_ = instance;
  span_ = span;
  dpi_ = GetDpiForSystem();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &TaskbarWidget::window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
  wc.lpszClassName = kWidgetClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWidgetClass, kWidgetTitle, WS_POPUP,
                          0, 0, dip(kWidthDip), dip(kHeightDip), nullptr, nullptr, instance, this);
  if (!hwnd_) return false;

  dpi_ = GetDpiForWindow(hwnd_);
  const BOOL dark = data_.dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
  const DWORD round = 2;
  DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &round, sizeof(round));

  position();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd_);
  SetTimer(hwnd_, kPositionTimer, 2000, nullptr);
  return true;
}

void TaskbarWidget::destroy() {
  const HWND hwnd = hwnd_;
  hwnd_ = nullptr;
  if (hwnd) {
    KillTimer(hwnd, kPositionTimer);
    DestroyWindow(hwnd);
  }
  release_fonts();
  placed_ = RECT{};
}

void TaskbarWidget::set_data(const WidgetData& data) {
  const bool dark_changed = data_.dark != data.dark;
  data_ = data;
  if (!hwnd_) return;
  if (dark_changed) {
    const BOOL dark = data_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
  }
  position();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void TaskbarWidget::set_span(WidgetSpan span) {
  if (span_ == span) return;
  span_ = span;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int TaskbarWidget::dip(int value) const {
  return MulDiv(value, static_cast<int>(dpi_), 96);
}

void TaskbarWidget::position() {
  if (!hwnd_) return;
  const int width = dip(kWidthDip);
  const int height = dip(kHeightDip);

  APPBARDATA bar{};
  bar.cbSize = sizeof(bar);
  const bool have_taskbar = SHAppBarMessage(ABM_GETTASKBARPOS, &bar) != 0;
  const RECT taskbar = bar.rc;

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
  GetMonitorInfoW(monitor, &info);

  const int gap = dip(6);
  int x = info.rcWork.right - width - gap;
  int y = info.rcWork.bottom - height - gap;
  if (have_taskbar) {
    switch (bar.uEdge) {
      case ABE_BOTTOM:
        x = taskbar.right - width - gap;
        y = taskbar.top - height - gap;
        break;
      case ABE_TOP:
        x = taskbar.right - width - gap;
        y = taskbar.bottom + gap;
        break;
      case ABE_LEFT:
        x = taskbar.right + gap;
        y = taskbar.bottom - height - gap;
        break;
      case ABE_RIGHT:
        x = taskbar.left - width - gap;
        y = taskbar.bottom - height - gap;
        break;
      default:
        break;
    }
  }
  x = std::clamp(x, static_cast<int>(info.rcMonitor.left),
                 static_cast<int>(info.rcMonitor.right) - width);
  y = std::clamp(y, static_cast<int>(info.rcMonitor.top),
                 static_cast<int>(info.rcMonitor.bottom) - height);

  const RECT target{x, y, x + width, y + height};
  if (EqualRect(&target, &placed_)) return;
  placed_ = target;
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void TaskbarWidget::ensure_fonts() {
  if (value_font_ && font_dpi_ == dpi_) return;
  release_fonts();
  font_dpi_ = dpi_;
  auto make = [&](int size, int weight) {
    return CreateFontW(-MulDiv(size, static_cast<int>(dpi_), 96), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  };
  value_font_ = make(21, FW_SEMIBOLD);
  total_font_ = make(13, FW_SEMIBOLD);
  micro_font_ = make(9, FW_SEMIBOLD);
}

void TaskbarWidget::release_fonts() {
  if (value_font_) DeleteObject(value_font_);
  if (total_font_) DeleteObject(total_font_);
  if (micro_font_) DeleteObject(micro_font_);
  value_font_ = nullptr;
  total_font_ = nullptr;
  micro_font_ = nullptr;
  font_dpi_ = 0;
}

void TaskbarWidget::paint(HDC dc) {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const Palette colors = palette(data_.dark);
  const int width = rc.right;
  const int height = rc.bottom;

  HBRUSH background = CreateSolidBrush(hover_ ? colors.hover : colors.bg);
  FillRect(dc, &rc, background);
  DeleteObject(background);

  HBRUSH border = CreateSolidBrush(colors.border);
  FrameRect(dc, &rc, border);
  DeleteObject(border);

  RECT accent{0, 0, dip(3), height};
  HBRUSH accent_brush = CreateSolidBrush(colors.accent);
  FillRect(dc, &accent, accent_brush);
  DeleteObject(accent_brush);

  ensure_fonts();

  auto text = [&](const std::wstring& value, HFONT font, COLORREF color, RECT rect, UINT flags) {
    if (!font) return;
    const HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), -1, &rect, flags);
    SelectObject(dc, previous);
  };

  const int pad = dip(12);
  const int menu_width = dip(22);
  const int menu_left = width - menu_width;
  const bool has_total = span_ != WidgetSpan::None;
  const int right_width = has_total ? dip(94) : 0;
  const int split = menu_left - right_width - (has_total ? dip(14) : 0);

  const bool live = data_.watts.has_value() && std::isfinite(*data_.watts);
  std::wstring watts = widen(fmt_watts(data_.watts));
  if (live) watts += L" W";
  const RECT value_rect{pad + dip(6), dip(2), split, dip(30)};
  text(watts, value_font_, colors.text, value_rect,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  std::wstring status = L"NO READING";
  COLORREF status_color = colors.red;
  if (live) {
    if (data_.measured) {
      status = L"MEASURED";
      status_color = colors.green;
    } else {
      status = L"ESTIMATED";
      status_color = colors.amber;
    }
  }
  if (data_.demo) status += L" \x00B7 DEMO";
  const RECT status_rect{pad + dip(6), dip(30), split, dip(45)};
  text(status, micro_font_, status_color, status_rect,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  if (has_total) {
    const int divider_x = split + dip(7);
    HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
    const HGDIOBJ previous_pen = SelectObject(dc, pen);
    MoveToEx(dc, divider_x, dip(8), nullptr);
    LineTo(dc, divider_x, height - dip(8));
    SelectObject(dc, previous_pen);
    DeleteObject(pen);

    const RECT label_rect{split + dip(16), dip(3), menu_left - dip(6), dip(19)};
    text(span_title(span_), micro_font_, colors.muted, label_rect,
         DT_LEFT | DT_TOP | DT_SINGLELINE);

    const RECT total_rect{split + dip(16), dip(17), menu_left - dip(6), dip(43)};
    text(widen(fmt_energy(span_mwh(data_.totals, span_))), total_font_, colors.text, total_rect,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  const int cx = menu_left + menu_width / 2;
  const int cy = height / 2;
  HPEN chevron = CreatePen(PS_SOLID, 1, colors.muted);
  const HGDIOBJ previous_pen = SelectObject(dc, chevron);
  MoveToEx(dc, cx - dip(4), cy - dip(2), nullptr);
  LineTo(dc, cx, cy + dip(2));
  LineTo(dc, cx + dip(4), cy - dip(2));
  SelectObject(dc, previous_pen);
  DeleteObject(chevron);
}

void TaskbarWidget::show_menu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open WattPulse dashboard");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (span_ == WidgetSpan::None ? MF_CHECKED : MF_UNCHECKED), kMenuNone,
              L"Live draw only");
  AppendMenuW(menu, MF_STRING | (span_ == WidgetSpan::Hour ? MF_CHECKED : MF_UNCHECKED), kMenuHour,
              L"Also show last hour");
  AppendMenuW(menu, MF_STRING | (span_ == WidgetSpan::Day ? MF_CHECKED : MF_UNCHECKED), kMenuDay,
              L"Also show last 24 hours");
  AppendMenuW(menu, MF_STRING | (span_ == WidgetSpan::Week ? MF_CHECKED : MF_UNCHECKED), kMenuWeek,
              L"Also show last week");
  AppendMenuW(menu, MF_STRING | (span_ == WidgetSpan::Month ? MF_CHECKED : MF_UNCHECKED),
              kMenuMonth, L"Also show last month");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuUnpin, L"Unpin from taskbar");

  POINT cursor{};
  GetCursorPos(&cursor);
  SetForegroundWindow(hwnd_);
  const int command = static_cast<int>(TrackPopupMenu(
      menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0, hwnd_, nullptr));
  DestroyMenu(menu);

  switch (command) {
    case kMenuOpen:
      if (on_open_dashboard) on_open_dashboard();
      return;
    case kMenuUnpin:
      if (on_unpin) on_unpin();
      return;
    default:
      break;
  }

  WidgetSpan chosen = span_;
  switch (command) {
    case kMenuNone: chosen = WidgetSpan::None; break;
    case kMenuHour: chosen = WidgetSpan::Hour; break;
    case kMenuDay: chosen = WidgetSpan::Day; break;
    case kMenuWeek: chosen = WidgetSpan::Week; break;
    case kMenuMonth: chosen = WidgetSpan::Month; break;
    default: return;
  }
  if (chosen == span_) return;
  span_ = chosen;
  InvalidateRect(hwnd_, nullptr, FALSE);
  if (on_span_changed) on_span_changed(span_);
}

LRESULT CALLBACK TaskbarWidget::window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                            LPARAM lparam) {
  TaskbarWidget* self = reinterpret_cast<TaskbarWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<TaskbarWidget*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self) return self->handle_message(hwnd, message, wparam, lparam);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT TaskbarWidget::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HDC memory = CreateCompatibleDC(dc);
      if (memory) {
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        if (bitmap) {
          const HGDIOBJ previous = SelectObject(memory, bitmap);
          paint(memory);
          BitBlt(dc, 0, 0, rc.right, rc.bottom, memory, 0, 0, SRCCOPY);
          SelectObject(memory, previous);
          DeleteObject(bitmap);
        }
        DeleteDC(memory);
      } else {
        paint(dc);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_LBUTTONUP:
      if (GET_X_LPARAM(lparam) >= dip(kWidthDip) - dip(24)) {
        show_menu();
      } else if (on_open_dashboard) {
        on_open_dashboard();
      }
      return 0;
    case WM_RBUTTONUP:
      show_menu();
      return 0;
    case WM_MOUSEMOVE:
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        tracking_mouse_ = true;
      }
      if (!hover_) {
        hover_ = true;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      if (hover_) {
        hover_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    case WM_TIMER:
      if (wparam == kPositionTimer) {
        position();
        return 0;
      }
      break;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
      position();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED:
      dpi_ = HIWORD(wparam);
      release_fonts();
      position();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kPositionTimer);
      hwnd_ = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace wp
