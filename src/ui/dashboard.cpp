#include "ui/dashboard.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/log.h"
#include "app/settings.h"
#include "core/power_model.h"
#include "core/sampler.h"
#include "sensors/factory.h"
#include "storage/store.h"
#include "ui/format.h"

namespace wp {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClass[] = L"WattPulseDashboardWindow";
constexpr wchar_t kWindowTitle[] = L"WattPulse";
constexpr int kLogoResource = 102;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kDwmUseImmersiveDarkMode = 20;
constexpr UINT kDwmWindowCornerPreference = 33;

enum class Span { Hour = 0, Day = 1, Week = 2, Month = 3 };

constexpr Span kSpans[] = {Span::Hour, Span::Day, Span::Week, Span::Month};

int span_seconds(Span span) {
  switch (span) {
    case Span::Hour: return 3600;
    case Span::Day: return 86400;
    case Span::Week: return 7 * 86400;
    case Span::Month: return 30 * 86400;
  }
  return 86400;
}

int span_bucket(Span span) {
  switch (span) {
    case Span::Hour: return 300;
    case Span::Day: return 3600;
    case Span::Week: return 6 * 3600;
    case Span::Month: return 86400;
  }
  return 3600;
}

const wchar_t* span_label(Span span) {
  switch (span) {
    case Span::Hour: return L"Last hour";
    case Span::Day: return L"Last 24 hours";
    case Span::Week: return L"Last week";
    case Span::Month: return L"Last month";
  }
  return L"Last 24 hours";
}

D2D1_COLOR_F rgb(std::uint32_t value, float alpha = 1.0f) {
  return D2D1::ColorF(static_cast<float>((value >> 16) & 0xff) / 255.0f,
                      static_cast<float>((value >> 8) & 0xff) / 255.0f,
                      static_cast<float>(value & 0xff) / 255.0f, alpha);
}

D2D1_COLOR_F with_alpha(D2D1_COLOR_F color, float alpha) {
  color.a = alpha;
  return color;
}

struct Theme {
  D2D1_COLOR_F bg;
  D2D1_COLOR_F card;
  D2D1_COLOR_F alt;
  D2D1_COLOR_F border;
  D2D1_COLOR_F text;
  D2D1_COLOR_F muted;
  D2D1_COLOR_F faint;
  D2D1_COLOR_F accent;
  D2D1_COLOR_F accent_soft;
  D2D1_COLOR_F accent_hover;
  D2D1_COLOR_F green;
  D2D1_COLOR_F green_soft;
  D2D1_COLOR_F amber;
  D2D1_COLOR_F amber_soft;
  D2D1_COLOR_F amber_text;
  D2D1_COLOR_F red;
  D2D1_COLOR_F red_soft;
  D2D1_COLOR_F purple;
  D2D1_COLOR_F grid;
  D2D1_COLOR_F shadow;
  D2D1_COLOR_F menu_hover;
};

Theme light_theme() {
  return {
      rgb(0xF0F2F5), rgb(0xFFFFFF), rgb(0xF5F6F8), rgb(0xE4E6EB),
      rgb(0x050505), rgb(0x65676B), rgb(0x8A8D91), rgb(0x1877F2),
      rgb(0xE7F3FF), rgb(0x166FE5), rgb(0x31A24C), rgb(0xE7F6EA),
      rgb(0xF5A623), rgb(0xFFF4DF), rgb(0xB26A00), rgb(0xE41E3F),
      rgb(0xFFEBEE), rgb(0x8B5CF6), rgb(0xE9EBEE), rgb(0x000000, 0.08f),
      rgb(0xF0F2F5)};
}

Theme dark_theme() {
  return {
      rgb(0x18191A), rgb(0x242526), rgb(0x2F3031), rgb(0x3E4042),
      rgb(0xE4E6EB), rgb(0xB0B3B8), rgb(0x8A8D91), rgb(0x2D88FF),
      rgb(0x1F3A5F), rgb(0x4A9BFF), rgb(0x45BD62), rgb(0x1F3A27),
      rgb(0xF5B942), rgb(0x3D3420), rgb(0xF5C86B), rgb(0xF16A7A),
      rgb(0x43232A), rgb(0xA78BFA), rgb(0x3E4042), rgb(0x000000, 0.35f),
      rgb(0x3A3B3C)};
}

bool point_in(const D2D1_RECT_F& r, float x, float y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

D2D1_RECT_F inset(const D2D1_RECT_F& r, float x, float y) {
  return D2D1::RectF(r.left + x, r.top + y, r.right - x, r.bottom - y);
}

std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

bool system_prefers_dark() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS status =
      RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  return status == ERROR_SUCCESS && value == 0;
}

std::wstring capitalize(std::string value) {
  if (!value.empty()) value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
  return widen(value);
}

std::wstring friendly_source(const std::string& source) {
  if (source.empty()) return L"No source";
  if (source == "estimate") return L"Modelled";
  if (source == "battery") return L"Battery discharge";
  if (source == "rapl:psys") return L"RAPL PSYS";
  if (source.rfind("nvml", 0) == 0) return L"NVIDIA GPU";
  if (source.rfind("adlx", 0) == 0) return L"AMD GPU";
  if (source.rfind("emi", 0) == 0) return L"Energy meter";
  if (source.rfind("demo", 0) == 0) return L"Demo source";
  return widen(source);
}

std::wstring sensor_label(const std::string& name) {
  if (name == "battery") return L"Battery";
  if (name == "emi") return L"Energy meter";
  if (name == "nvml") return L"NVIDIA GPU";
  if (name == "adlx") return L"AMD GPU";
  if (name == "demo") return L"Demo source";
  return capitalize(name);
}

std::wstring component_source(const ModelOutput& model, Domain domain) {
  for (const Sample& sample : model.samples) {
    if (sample.domain == domain && !sample.sourceId.empty()) {
      if (sample.sourceId.rfind("nvml", 0) == 0) return L"NVML";
      if (sample.sourceId.rfind("adlx", 0) == 0) return L"ADLX";
      if (sample.sourceId.rfind("emi", 0) == 0) return L"RAPL";
      if (sample.sourceId.rfind("demo", 0) == 0) return L"Demo";
      return widen(sample.sourceId);
    }
  }
  return L"Modelled";
}

std::wstring time_label(std::int64_t ts, Span span) {
  const std::time_t value = static_cast<std::time_t>(ts);
  std::tm local{};
  localtime_s(&local, &value);
  wchar_t buf[32] = {};
  if (span == Span::Week) {
    std::wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%a", &local);
  } else if (span == Span::Month) {
    std::wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%d %b", &local);
  } else {
    std::wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%H:%M", &local);
  }
  return buf;
}

class Dashboard {
 public:
  Dashboard(HINSTANCE instance, bool demo) : instance_(instance), demo_(demo) {}
  ~Dashboard() { shutdown(); }

  bool create() {
    if (!ensure_factories()) {
      MessageBoxW(nullptr, L"Could not initialise Direct2D.", kWindowTitle, MB_ICONERROR);
      return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Dashboard::window_proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    dpi_ = static_cast<float>(dpi);
    RECT rect = {0, 0, MulDiv(1180, static_cast<int>(dpi), 96),
                 MulDiv(840, static_cast<int>(dpi), 96)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);

    hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, x, y, width,
                            height, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
      MessageBoxW(nullptr, L"Could not create the WattPulse window.", kWindowTitle,
                  MB_ICONERROR);
      return false;
    }

    if (!init_session()) return false;
    apply_window_theme();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
  }

  int message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
  }

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    Dashboard* self = reinterpret_cast<Dashboard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      self = static_cast<Dashboard*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) return self->handle_message(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_ERASEBKGND:
        return 1;
      case WM_SIZE:
        if (target_) {
          target_->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(wparam));
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        discard_target();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
        info->ptMinTrackSize.x = MulDiv(980, static_cast<int>(dpi), 96);
        info->ptMinTrackSize.y = MulDiv(720, static_cast<int>(dpi), 96);
        return 0;
      }
      case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        paint();
        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_TIMER:
        if (wparam == kRefreshTimer) {
          refresh_live();
          return 0;
        }
        break;
      case WM_MOUSEMOVE:
        on_mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
      case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        if (hover_theme_ || hover_span_button_ || hover_total_ >= 0 || hover_option_ >= 0) {
          hover_theme_ = false;
          hover_span_button_ = false;
          hover_total_ = -1;
          hover_option_ = -1;
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      case WM_LBUTTONDOWN:
        on_click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
      case WM_KEYDOWN:
        on_key(static_cast<int>(wparam));
        return 0;
      case WM_SETTINGCHANGE:
        if (follow_system_) {
          const bool dark = system_prefers_dark();
          if (dark != dark_) {
            dark_ = dark;
            apply_window_theme();
            InvalidateRect(hwnd, nullptr, FALSE);
          }
        }
        return 0;
      case WM_CLOSE:
        shutdown();
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        shutdown();
        PostQuitMessage(0);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  bool ensure_factories() {
    if (factory_ && dwrite_) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) {
      return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(
                                       dwrite_.GetAddressOf())))) {
      return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IWICImagingFactory),
                                reinterpret_cast<void**>(wic_.GetAddressOf())))) {
      return false;
    }
    return create_text_formats();
  }

  bool make_format(float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& out) {
    const HRESULT hr =
        dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, size, L"en-us",
                                  out.GetAddressOf());
    if (SUCCEEDED(hr)) out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    return SUCCEEDED(hr);
  }

  bool create_text_formats() {
    return make_format(16.0f, DWRITE_FONT_WEIGHT_BOLD, brand_) &&
           make_format(10.5f, DWRITE_FONT_WEIGHT_NORMAL, subtitle_) &&
           make_format(9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, label_) &&
           make_format(50.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, display_) &&
           make_format(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, display_unit_) &&
           make_format(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, section_) &&
           make_format(23.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, metric_) &&
           make_format(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, metric_small_) &&
           make_format(11.5f, DWRITE_FONT_WEIGHT_NORMAL, body_) &&
           make_format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, body_medium_) &&
           make_format(10.0f, DWRITE_FONT_WEIGHT_NORMAL, small_) &&
           make_format(9.0f, DWRITE_FONT_WEIGHT_NORMAL, micro_) &&
           make_format(9.0f, DWRITE_FONT_WEIGHT_NORMAL, chart_) &&
           make_format(10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, pill_) &&
           make_format(11.5f, DWRITE_FONT_WEIGHT_NORMAL, menu_);
  }

  bool ensure_target() {
    if (target_) return true;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT width = static_cast<UINT>(std::max(0L, rc.right - rc.left));
    const UINT height = static_cast<UINT>(std::max(0L, rc.bottom - rc.top));
    if (width == 0 || height == 0) return false;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), dpi_, dpi_);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);
    if (FAILED(factory_->CreateHwndRenderTarget(props, hwnd_props, target_.GetAddressOf()))) {
      target_.Reset();
      return false;
    }
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),
                                               brush_.GetAddressOf()))) {
      target_.Reset();
      return false;
    }
    load_logo();
    return true;
  }

  void load_logo() {
    logo_.Reset();
    if (!wic_ || !target_) return;
    const HRSRC res = FindResourceW(instance_, MAKEINTRESOURCEW(kLogoResource), RT_RCDATA);
    if (!res) return;
    const DWORD size = SizeofResource(instance_, res);
    const HGLOBAL data = LoadResource(instance_, res);
    const void* bytes = data ? LockResource(data) : nullptr;
    if (!bytes || size == 0) return;

    ComPtr<IWICStream> stream;
    if (FAILED(wic_->CreateStream(stream.GetAddressOf()))) return;
    if (FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(bytes)),
                                            size))) {
      return;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                             decoder.GetAddressOf()))) {
      return;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic_->CreateFormatConverter(converter.GetAddressOf()))) return;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
      return;
    }
    target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, logo_.GetAddressOf());
  }

  void discard_target() {
    logo_.Reset();
    brush_.Reset();
    target_.Reset();
  }

  bool init_session() {
    settings_path_ = data_dir() + "\\settings.ini";
    settings_ = load_settings(settings_path_);
    const std::string theme = settings_.theme;
    follow_system_ = theme.empty() || theme == "system";
    dark_ = follow_system_ ? system_prefers_dark() : (theme != "light");
    if (theme == "graphite") {
      follow_system_ = false;
      dark_ = true;
    }

    Calibration cal;
    cal.base_watts = settings_.base_watts;
    cal.calibrated = settings_.calibrated;
    PowerModel model(cal, parse_form_factor(settings_.form_factor));
    std::vector<std::unique_ptr<ISensor>> sensors =
        demo_ ? make_demo_sensors() : make_sensors(settings_);

    std::vector<std::unique_ptr<ISensor>> available;
    for (auto& sensor : sensors) {
      bool ok = false;
      try {
        ok = sensor->available();
      } catch (...) {
        ok = false;
      }
      log_info(std::string("ui sensor ") + std::string(sensor->name()) +
               (ok ? ": available" : ": unavailable"));
    }

    const double now = wall_seconds();
    const std::string path = data_dir() + "\\power.db";
    if (demo_) {
      storage_note_ = L"Demo (memory)";
      if (!store_.open(":memory:", 35, now)) {
        storage_warning_ = widen(store_.last_error());
      } else {
        store_ok_ = true;
        seed_demo_history();
      }
    } else {
      storage_note_ = L"Local \x00B7 35 days";
      if (!store_.open(path, 35, now)) {
        storage_warning_ = widen(store_.last_error());
        log_error("ui storage open failed: " + store_.last_error());
        if (store_.open(":memory:", 35, now)) {
          store_ok_ = true;
          storage_note_ = L"Unsaved memory";
        }
      } else {
        store_ok_ = true;
        log_info("ui storage: " + store_.path());
      }
    }

    sampler_ = std::make_unique<Sampler>(std::move(sensors), model);
    sampler_->start();
    SetTimer(hwnd_, kRefreshTimer, 1000, nullptr);
    refresh_store();
    refresh_live();
    return true;
  }

  void seed_demo_history() {
    const std::int64_t now = static_cast<std::int64_t>(wall_seconds());
    const std::int64_t start = now - 30 * 86400;
    std::map<std::int64_t, MinuteBucket> rows;
    constexpr double kPi = 3.14159265358979323846;
    for (std::int64_t ts = start; ts < now; ts += 300) {
      const double seconds = static_cast<double>(ts);
      const double day = std::fmod(seconds, 86400.0) / 86400.0 * 2.0 * kPi - kPi / 2.0;
      const double usage = std::max(0.0, 0.55 + 0.28 * std::sin(day) + 0.08 * std::sin(day * 4.0));
      const double cpu = 13.0 + 34.0 * usage + 2.5 * std::sin(seconds / 170.0);
      const double gpu = 5.0 + 19.0 * (0.5 + 0.5 * std::sin(day * 1.7 + 1.2));
      const double other = 21.0 + 5.0 * std::sin(day * 2.3);
      const double total = cpu + gpu + other;
      const double wh_factor = 300.0 / 3600.0;

      MinuteBucket bucket;
      bucket.mwh_total = total * wh_factor * 1000.0;
      bucket.mwh_cpu = cpu * wh_factor * 1000.0;
      bucket.mwh_gpu = gpu * wh_factor * 1000.0;
      bucket.mwh_other = other * wh_factor * 1000.0;
      bucket.mwh_estimated = bucket.mwh_total * 0.22;
      bucket.flags = FLAG_MEASURED | FLAG_ESTIMATED;
      rows[ts] = bucket;
    }
    if (store_.write(rows)) {
      log_info("demo history seeded: " + std::to_string(rows.size()) + " rows");
    }
  }

  void refresh_store() {
    if (!store_ok_) return;
    now_ = wall_seconds();
    store_totals_ = store_.totals(now_);
    series_ = store_.series(now_, span_seconds(selected_span_), span_bucket(selected_span_));
  }

  bool flush_pending() {
    if (!sampler_ || !store_ok_) {
      if (sampler_) sampler_->drain_pending();
      return false;
    }
    auto rows = sampler_->drain_pending();
    if (rows.empty()) return true;
    if (!store_.write(rows)) {
      storage_warning_ = widen(store_.last_error());
      log_error("ui storage write failed: " + store_.last_error());
      sampler_->restore_pending(rows);
      return false;
    }
    storage_warning_.clear();
    return true;
  }

  void refresh_live() {
    if (!sampler_) return;
    snapshot_ = sampler_->snapshot();
    now_ = wall_seconds();
    if (snapshot_.ticks - last_flush_tick_ >= 30) {
      last_flush_tick_ = snapshot_.ticks;
      if (flush_pending()) refresh_store();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  Totals totals_with_pending() const {
    Totals totals = store_totals_;
    const std::int64_t now = static_cast<std::int64_t>(now_);
    for (const auto& [ts, bucket] : snapshot_.pending) {
      const std::int64_t age = now - ts;
      if (age <= 3600) totals.mwh_last_hour += bucket.mwh_total;
      if (age <= 86400) totals.mwh_last_day += bucket.mwh_total;
      if (age <= 7 * 86400) totals.mwh_last_week += bucket.mwh_total;
      if (age <= 30 * 86400) {
        totals.mwh_last_month += bucket.mwh_total;
        totals.mwh_estimated += bucket.mwh_estimated;
        totals.mwh_measured += bucket.mwh_total - bucket.mwh_estimated;
      }
    }
    return totals;
  }

  std::vector<SeriesPoint> chart_points() const {
    std::vector<SeriesPoint> points = series_;
    const int bucket = span_bucket(selected_span_);
    for (const auto& [ts, minute] : snapshot_.pending) {
      for (SeriesPoint& point : points) {
        if (ts >= point.ts && ts < point.ts + bucket) {
          point.mwh_total += minute.mwh_total;
          point.mwh_cpu += minute.mwh_cpu;
          point.mwh_gpu += minute.mwh_gpu;
          point.mwh_other += minute.mwh_other;
          point.mwh_estimated += minute.mwh_estimated;
          break;
        }
      }
    }
    return points;
  }

  void shutdown() {
    if (shut_down_) return;
    shut_down_ = true;
    if (hwnd_) KillTimer(hwnd_, kRefreshTimer);
    if (sampler_) {
      sampler_->stop();
      flush_pending();
    }
    if (store_ok_ && store_.is_open()) {
      const Totals totals = store_.totals(wall_seconds());
      log_info("ui totals: hour=" + std::to_string(totals.mwh_last_hour) +
               " day=" + std::to_string(totals.mwh_last_day) +
               " week=" + std::to_string(totals.mwh_last_week) +
               " month=" + std::to_string(totals.mwh_last_month) + " mWh");
      store_.close();
    }
  }

  void apply_window_theme() {
    if (!hwnd_) return;
    const BOOL dark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const DWORD round = 2;
    DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &round, sizeof(round));
  }

  void set_span(Span span) {
    selected_span_ = span;
    span_open_ = false;
    hover_option_ = -1;
    refresh_store();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  float to_dip_x(LPARAM value) const {
    return static_cast<float>(GET_X_LPARAM(value)) * 96.0f / dpi_;
  }

  float to_dip_y(LPARAM value) const {
    return static_cast<float>(GET_Y_LPARAM(value)) * 96.0f / dpi_;
  }

  void on_mouse_move(int px, int py) {
    const float x = static_cast<float>(px) * 96.0f / dpi_;
    const float y = static_cast<float>(py) * 96.0f / dpi_;
    if (!tracking_mouse_) {
      TRACKMOUSEEVENT track{};
      track.cbSize = sizeof(track);
      track.dwFlags = TME_LEAVE;
      track.hwndTrack = hwnd_;
      TrackMouseEvent(&track);
      tracking_mouse_ = true;
    }

    bool theme_hover = point_in(theme_button_, x, y);
    bool span_hover = point_in(span_button_, x, y);
    int total_hover = -1;
    for (int i = 0; i < 4; ++i) {
      if (point_in(total_cards_[i], x, y)) total_hover = i;
    }
    int option_hover = -1;
    if (span_open_) {
      for (int i = 0; i < 4; ++i) {
        if (point_in(span_options_[i], x, y)) option_hover = i;
      }
    }
    if (theme_hover != hover_theme_ || span_hover != hover_span_button_ ||
        total_hover != hover_total_ || option_hover != hover_option_) {
      hover_theme_ = theme_hover;
      hover_span_button_ = span_hover;
      hover_total_ = total_hover;
      hover_option_ = option_hover;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void on_click(int px, int py) {
    const float x = static_cast<float>(px) * 96.0f / dpi_;
    const float y = static_cast<float>(py) * 96.0f / dpi_;
    if (span_open_) {
      for (int i = 0; i < 4; ++i) {
        if (point_in(span_options_[i], x, y)) {
          set_span(kSpans[i]);
          return;
        }
      }
      if (!point_in(span_button_, x, y) && !point_in(span_menu_, x, y)) {
        span_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    }
    if (point_in(theme_button_, x, y)) {
      dark_ = !dark_;
      follow_system_ = false;
      settings_.theme = dark_ ? "dark" : "light";
      if (!save_settings(settings_path_, settings_)) {
        log_warn("could not save theme to " + settings_path_);
      }
      apply_window_theme();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (point_in(span_button_, x, y)) {
      span_open_ = true;
      hover_option_ = -1;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    for (int i = 0; i < 4; ++i) {
      if (point_in(total_cards_[i], x, y)) {
        set_span(kSpans[i]);
        return;
      }
    }
  }

  void on_key(int key) {
    if (key == VK_ESCAPE) {
      if (span_open_) {
        span_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    }
    if (!span_open_) return;
    if (key == VK_DOWN) {
      hover_option_ = (hover_option_ + 1) % 4;
      InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (key == VK_UP) {
      hover_option_ = hover_option_ <= 0 ? 3 : hover_option_ - 1;
      InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (key == VK_RETURN) {
      const int index =
          hover_option_ >= 0 ? hover_option_ : static_cast<int>(selected_span_);
      set_span(kSpans[std::clamp(index, 0, 3)]);
    }
  }

  void paint() {
    if (!ensure_target()) return;
    const Theme theme = dark_ ? dark_theme() : light_theme();
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(theme.bg);
    const D2D1_SIZE_F size = target_->GetSize();
    draw(theme, size.width, size.height);
    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discard_target();
  }

  void set_color(D2D1_COLOR_F color) { brush_->SetColor(color); }

  void fill_round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) {
    set_color(color);
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
  }

  void stroke_round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color, float width = 1.0f) {
    set_color(color);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get(), width);
  }

  void fill_rect(D2D1_RECT_F rect, D2D1_COLOR_F color) {
    set_color(color);
    target_->FillRectangle(rect, brush_.Get());
  }

  void draw_line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width = 1.0f) {
    set_color(color);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), width);
  }

  void draw_circle(float x, float y, float radius, D2D1_COLOR_F color) {
    set_color(color);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush_.Get());
  }

  void draw_card(D2D1_RECT_F rect, const Theme& theme, bool selected = false,
                 bool hovered = false) {
    D2D1_RECT_F shadow = rect;
    shadow.top += 2.0f;
    shadow.bottom += 2.0f;
    fill_round(shadow, 14.0f, theme.shadow);
    fill_round(rect, 14.0f, selected ? theme.accent_soft : (hovered ? theme.alt : theme.card));
    stroke_round(rect, 14.0f, selected ? theme.accent : theme.border, selected ? 1.4f : 1.0f);
  }

  void draw_text(std::wstring_view text, const ComPtr<IDWriteTextFormat>& format,
                 D2D1_RECT_F rect, D2D1_COLOR_F color,
                 DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                 bool wrap = false, bool ellipsis = true) {
    if (text.empty() || rect.right <= rect.left || rect.bottom <= rect.top) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                         format.Get(), rect.right - rect.left,
                                         rect.bottom - rect.top, layout.GetAddressOf()))) {
      return;
    }
    layout->SetTextAlignment(align);
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (ellipsis && !wrap) {
      ComPtr<IDWriteInlineObject> sign;
      if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format.Get(), sign.GetAddressOf()))) {
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trimming, sign.Get());
      }
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float y = rect.top;
    if (valign == DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
      y += ((rect.bottom - rect.top) - metrics.height) / 2.0f;
    } else if (valign == DWRITE_PARAGRAPH_ALIGNMENT_FAR) {
      y += (rect.bottom - rect.top) - metrics.height;
    }
    set_color(color);
    target_->DrawTextLayout(D2D1::Point2F(rect.left, y), layout.Get(), brush_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }

  float text_width(std::wstring_view text, const ComPtr<IDWriteTextFormat>& format) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                         format.Get(), 4096.0f, 100.0f,
                                         layout.GetAddressOf()))) {
      return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
  }

  void draw_header(const Theme& theme, float x, float y, float right) {
    const float logo = 36.0f;
    const D2D1_RECT_F logo_rect = D2D1::RectF(x, y, x + logo, y + logo);
    if (logo_) {
      target_->DrawBitmap(logo_.Get(), logo_rect, 1.0f,
                          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
      fill_round(logo_rect, 10.0f, theme.accent);
      draw_text(L"W", brand_, logo_rect, rgb(0xFFFFFF), DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false);
    }
    draw_text(L"WattPulse", brand_, D2D1::RectF(x + logo + 12.0f, y - 1.0f, right, y + 21.0f),
              theme.text);
    draw_text(demo_ ? L"Demo mode \x00B7 nothing is saved" : L"Live system power",
              subtitle_, D2D1::RectF(x + logo + 12.0f, y + 20.0f, right, y + 38.0f),
              theme.muted);

    const float button_y = y + 2.0f;
    const float theme_width = 142.0f;
    theme_button_ = D2D1::RectF(right - theme_width, button_y, right, button_y + 32.0f);
    fill_round(theme_button_, 16.0f,
               hover_theme_ ? theme.accent_soft : theme.card);
    stroke_round(theme_button_, 16.0f, theme.border);
    const std::wstring theme_text = dark_ ? L"Switch to light" : L"Switch to dark";
    draw_text(theme_text, pill_, inset(theme_button_, 12.0f, 0.0f), theme.text,
              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false);

    const float status_width = 148.0f;
    const D2D1_RECT_F status = D2D1::RectF(theme_button_.left - status_width - 10.0f, button_y,
                                           theme_button_.left - 10.0f, button_y + 32.0f);
    fill_round(status, 16.0f, theme.card);
    stroke_round(status, 16.0f, theme.border);
    int online = 0;
    for (const SensorStatus& sensor : snapshot_.sensors) {
      if (sensor.producing) ++online;
    }
    const bool has_data = online > 0;
    const D2D1_COLOR_F dot = has_data ? theme.green : theme.amber;
    draw_circle(status.left + 15.0f, (status.top + status.bottom) / 2.0f, 4.0f, dot);
    const std::wstring status_text =
        has_data ? std::to_wstring(online) + L" of " + std::to_wstring(snapshot_.sensors.size()) +
                       L" sensors live"
                 : L"Waiting for sensors";
    draw_text(status_text, small_, D2D1::RectF(status.left + 25.0f, status.top, status.right - 10.0f,
                                               status.bottom),
              has_data ? theme.text : theme.muted, DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }

  void draw_hero(const Theme& theme, D2D1_RECT_F rect) {
    draw_card(rect, theme);
    const float pad = 20.0f;
    const float available = rect.right - rect.left - pad * 2.0f;
    const float left_width = std::clamp(available * 0.42f, 330.0f, 500.0f);
    const float split = rect.left + pad + left_width;
    draw_line(split, rect.top + 20.0f, split, rect.bottom - 20.0f, theme.border);

    const ModelOutput& model = snapshot_.model;
    draw_text(L"CURRENT SYSTEM DRAW", label_,
              D2D1::RectF(rect.left + pad, rect.top + 17.0f, split - 20.0f, rect.top + 31.0f),
              theme.muted);
    const std::wstring value = widen(fmt_watts(model.total_watts));
    const float value_width = text_width(value, display_);
    draw_text(value, display_,
              D2D1::RectF(rect.left + pad, rect.top + 31.0f, split - 46.0f, rect.top + 92.0f),
              model.total_watts.has_value() ? theme.text : theme.faint);
    if (model.total_watts.has_value()) {
      draw_text(L"W", display_unit_,
                D2D1::RectF(rect.left + pad + value_width + 7.0f, rect.top + 58.0f, split,
                            rect.top + 82.0f),
                theme.muted);
    }

    D2D1_COLOR_F pill_bg = theme.red_soft;
    D2D1_COLOR_F pill_fg = theme.red;
    std::wstring provenance = L"NO READING";
    if (model.total_watts.has_value()) {
      if (model.total_provenance == Provenance::Measured) {
        pill_bg = theme.green_soft;
        pill_fg = theme.green;
        provenance = L"MEASURED";
      } else {
        pill_bg = theme.amber_soft;
        pill_fg = theme.amber_text;
        provenance = L"ESTIMATED";
      }
    }
    const float pill_width = text_width(provenance, pill_) + 22.0f;
    const D2D1_RECT_F pill =
        D2D1::RectF(rect.left + pad, rect.top + 102.0f, rect.left + pad + pill_width,
                    rect.top + 124.0f);
    fill_round(pill, 11.0f, pill_bg);
    draw_text(provenance, pill_, pill, pill_fg, DWRITE_TEXT_ALIGNMENT_CENTER,
              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false);
    draw_text(friendly_source(model.sourceId), small_,
              D2D1::RectF(pill.right + 9.0f, rect.top + 102.0f, split - 16.0f,
                          rect.top + 124.0f),
              theme.muted, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float right_left = split + 24.0f;
    const float right_width = rect.right - pad - right_left;
    const float col_gap = 18.0f;
    const float col_width = (right_width - col_gap * 2.0f) / 3.0f;
    struct Component {
      const wchar_t* name;
      std::optional<double> watts;
      D2D1_COLOR_F color;
      std::wstring source;
    };
    const Component components[] = {
        {L"CPU PACKAGE", model.cpu_watts, theme.accent,
         component_source(model, Domain::CpuPackage)},
        {L"GPU", model.gpu_watts, theme.purple, component_source(model, Domain::Gpu)},
        {L"OTHER", model.other_watts, theme.amber, L"Remainder"},
    };
    const double total = model.total_watts.value_or(0.0);
    for (int i = 0; i < 3; ++i) {
      const float cx = right_left + static_cast<float>(i) * (col_width + col_gap);
      const D2D1_RECT_F column = D2D1::RectF(cx, rect.top + 18.0f, cx + col_width,
                                             rect.bottom - 16.0f);
      draw_circle(column.left + 4.0f, column.top + 5.0f, 3.5f, components[i].color);
      draw_text(components[i].name, label_,
                D2D1::RectF(column.left + 13.0f, column.top - 1.0f, column.right,
                            column.top + 13.0f),
                theme.muted);
      draw_text(widen(fmt_watts(components[i].watts)), metric_,
                D2D1::RectF(column.left, column.top + 25.0f, column.right,
                            column.top + 54.0f),
                components[i].watts.has_value() ? theme.text : theme.faint);
      draw_text(components[i].source, micro_,
                D2D1::RectF(column.left, column.top + 55.0f, column.right,
                            column.top + 69.0f),
                theme.faint);
      const float bar_width = std::max(0.0f, col_width - 4.0f);
      const float filled =
          total > 0.0 && components[i].watts.has_value()
              ? bar_width * static_cast<float>(std::clamp(*components[i].watts / total, 0.0, 1.0))
              : 0.0f;
      fill_round(D2D1::RectF(column.left, column.top + 78.0f, column.left + bar_width,
                             column.top + 83.0f),
                 2.5f, theme.alt);
      if (filled > 0.0f) {
        fill_round(D2D1::RectF(column.left, column.top + 78.0f, column.left + filled,
                               column.top + 83.0f),
                   2.5f, components[i].color);
      }
    }
  }

  void draw_totals(const Theme& theme, D2D1_RECT_F rect, float gap) {
    const Totals totals = totals_with_pending();
    const double values[] = {totals.mwh_last_hour, totals.mwh_last_day,
                             totals.mwh_last_week, totals.mwh_last_month};
    const wchar_t* labels[] = {L"LAST HOUR", L"LAST 24 HOURS", L"LAST 7 DAYS",
                               L"LAST 30 DAYS"};
    const float width = (rect.right - rect.left - gap * 3.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
      const float left = rect.left + static_cast<float>(i) * (width + gap);
      const D2D1_RECT_F card =
          D2D1::RectF(left, rect.top, i == 3 ? rect.right : left + width, rect.bottom);
      total_cards_[i] = card;
      const bool selected = selected_span_ == kSpans[i];
      draw_card(card, theme, selected, hover_total_ == i);
      const float pad = 16.0f;
      draw_text(labels[i], label_,
                D2D1::RectF(card.left + pad, card.top + 13.0f, card.right - pad,
                            card.top + 27.0f),
                selected ? theme.accent : theme.muted);
      draw_text(widen(fmt_energy(values[i])), metric_,
                D2D1::RectF(card.left + pad, card.top + 30.0f, card.right - pad,
                            card.bottom - 10.0f),
                theme.text);
      if (selected) {
        draw_circle(card.right - pad, card.top + 20.0f, 3.5f, theme.accent);
      }
    }
  }

  void draw_chart(const Theme& theme, D2D1_RECT_F rect) {
    draw_card(rect, theme);
    const float pad = 18.0f;
    draw_text(L"Power history", section_,
              D2D1::RectF(rect.left + pad, rect.top + 11.0f, rect.right - 210.0f,
                          rect.top + 30.0f),
              theme.text);

    const float legend_y = rect.top + 16.0f;
    float legend_x = rect.left + pad + 104.0f;
    struct Legend {
      const wchar_t* label;
      D2D1_COLOR_F color;
    };
    const Legend legends[] = {{L"Total", theme.accent}, {L"CPU", theme.green},
                              {L"GPU", theme.purple}};
    for (const Legend& legend : legends) {
      draw_circle(legend_x, legend_y + 5.0f, 3.0f, legend.color);
      draw_text(legend.label, micro_,
                D2D1::RectF(legend_x + 8.0f, legend_y - 2.0f, legend_x + 54.0f,
                            legend_y + 12.0f),
                theme.muted);
      legend_x += 56.0f;
    }
    const std::wstring subtitle = L"Average watts \x00B7 " + std::wstring(span_label(selected_span_));
    draw_text(subtitle, micro_,
              D2D1::RectF(rect.left + pad, rect.top + 31.0f, rect.right - 210.0f,
                          rect.top + 45.0f),
              theme.faint);

    span_button_ =
        D2D1::RectF(rect.right - pad - 162.0f, rect.top + 13.0f, rect.right - pad,
                    rect.top + 45.0f);
    fill_round(span_button_, 9.0f, hover_span_button_ ? theme.accent_soft : theme.alt);
    stroke_round(span_button_, 9.0f, hover_span_button_ ? theme.accent : theme.border);
    draw_text(span_label(selected_span_), pill_,
              D2D1::RectF(span_button_.left + 12.0f, span_button_.top, span_button_.right - 28.0f,
                          span_button_.bottom),
              theme.text, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const float cx = span_button_.right - 16.0f;
    const float cy = (span_button_.top + span_button_.bottom) / 2.0f;
    draw_line(cx - 4.0f, cy - 2.0f, cx, cy + 2.0f, theme.muted, 1.5f);
    draw_line(cx, cy + 2.0f, cx + 4.0f, cy - 2.0f, theme.muted, 1.5f);

    const D2D1_RECT_F body =
        D2D1::RectF(rect.left + pad, rect.top + 52.0f, rect.right - pad, rect.bottom - 14.0f);
    const D2D1_RECT_F plot =
        D2D1::RectF(body.left + 42.0f, body.top + 6.0f, body.right, body.bottom - 18.0f);
    if (plot.right <= plot.left || plot.bottom <= plot.top) return;

    const std::vector<SeriesPoint> points = chart_points();
    const double bucket_seconds = static_cast<double>(span_bucket(selected_span_));
    double max_watts = 0.0;
    bool has_data = false;
    for (const SeriesPoint& point : points) {
      const double total = point.mwh_total * 3.6 / bucket_seconds;
      const double cpu = point.mwh_cpu * 3.6 / bucket_seconds;
      const double gpu = point.mwh_gpu * 3.6 / bucket_seconds;
      if (total > 0.01 || cpu > 0.01 || gpu > 0.01) has_data = true;
      max_watts = std::max({max_watts, total, cpu, gpu});
    }
    double step = 25.0;
    if (max_watts <= 20.0) {
      step = 5.0;
    } else if (max_watts <= 50.0) {
      step = 10.0;
    } else if (max_watts <= 100.0) {
      step = 25.0;
    } else if (max_watts <= 250.0) {
      step = 50.0;
    } else {
      step = 100.0;
    }
    const double y_max = has_data ? std::ceil(max_watts / step) * step : 100.0;
    const float plot_h = plot.bottom - plot.top;
    for (int i = 0; i <= 4; ++i) {
      const float y = plot.bottom - plot_h * static_cast<float>(i) / 4.0f;
      draw_line(plot.left, y, plot.right, y, theme.grid, 1.0f);
      const double label_value = y_max * static_cast<double>(i) / 4.0;
      wchar_t label[24] = {};
      std::swprintf(label, sizeof(label) / sizeof(label[0]), L"%.0f", label_value);
      draw_text(label, chart_,
                D2D1::RectF(body.left, y - 7.0f, plot.left - 8.0f, y + 7.0f), theme.faint,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    const std::size_t count = points.size();
    auto point_x = [&](std::size_t index) {
      if (count <= 1) return plot.left;
      return plot.left + (plot.right - plot.left) * static_cast<float>(index) /
                              static_cast<float>(count - 1);
    };
    auto point_y = [&](double watts) {
      const double clamped = std::clamp(watts, 0.0, y_max);
      return plot.bottom - static_cast<float>(clamped / y_max) * plot_h;
    };

    target_->PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (has_data && count > 1) {
      ComPtr<ID2D1PathGeometry> area;
      ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(factory_->CreatePathGeometry(area.GetAddressOf())) &&
          SUCCEEDED(area->Open(sink.GetAddressOf()))) {
        const double first_w = points[0].mwh_total * 3.6 / bucket_seconds;
        sink->BeginFigure(D2D1::Point2F(point_x(0), point_y(first_w)), D2D1_FIGURE_BEGIN_FILLED);
        for (std::size_t i = 1; i < count; ++i) {
          const double watts = points[i].mwh_total * 3.6 / bucket_seconds;
          sink->AddLine(D2D1::Point2F(point_x(i), point_y(watts)));
        }
        sink->AddLine(D2D1::Point2F(point_x(count - 1), plot.bottom));
        sink->AddLine(D2D1::Point2F(point_x(0), plot.bottom));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        set_color(with_alpha(theme.accent, 0.14f));
        target_->FillGeometry(area.Get(), brush_.Get());
      }

      auto draw_series = [&](auto value, D2D1_COLOR_F color, float width) {
        for (std::size_t i = 1; i < count; ++i) {
          const double a = value(points[i - 1]);
          const double b = value(points[i]);
          draw_line(point_x(i - 1), point_y(a), point_x(i), point_y(b), color, width);
        }
      };
      draw_series([&](const SeriesPoint& p) { return p.mwh_cpu * 3.6 / bucket_seconds; },
                  theme.green, 1.5f);
      draw_series([&](const SeriesPoint& p) { return p.mwh_gpu * 3.6 / bucket_seconds; },
                  theme.purple, 1.5f);
      draw_series([&](const SeriesPoint& p) { return p.mwh_total * 3.6 / bucket_seconds; },
                  theme.accent, 2.4f);
      const double last = points.back().mwh_total * 3.6 / bucket_seconds;
      draw_circle(point_x(count - 1), point_y(last), 3.5f, theme.card);
      draw_circle(point_x(count - 1), point_y(last), 2.2f, theme.accent);
    } else {
      draw_text(L"Collecting data \x2014 the first minute appears here.", body_,
                D2D1::RectF(plot.left, plot.top, plot.right, plot.bottom), theme.faint,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    target_->PopAxisAlignedClip();

    for (int i = 0; i < 4; ++i) {
      const std::int64_t ts =
          static_cast<std::int64_t>(now_) - span_seconds(selected_span_) +
          static_cast<std::int64_t>(span_seconds(selected_span_)) * i / 3;
      const std::wstring label = time_label(ts, selected_span_);
      const float x = plot.left + (plot.right - plot.left) * static_cast<float>(i) / 3.0f;
      DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_CENTER;
      float left = x - 38.0f;
      float right = x + 38.0f;
      if (i == 0) {
        align = DWRITE_TEXT_ALIGNMENT_LEADING;
        left = x;
        right = x + 76.0f;
      } else if (i == 3) {
        align = DWRITE_TEXT_ALIGNMENT_TRAILING;
        left = x - 76.0f;
        right = x;
      }
      draw_text(label, chart_, D2D1::RectF(left, plot.bottom + 4.0f, right, body.bottom),
                theme.faint, align, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
  }

  void draw_sensors(const Theme& theme, D2D1_RECT_F rect) {
    draw_card(rect, theme);
    const float pad = 18.0f;
    draw_text(L"SENSORS", label_,
              D2D1::RectF(rect.left + pad, rect.top + 12.0f, rect.right - pad,
                          rect.top + 26.0f),
              theme.muted);
    int online = 0;
    for (const SensorStatus& sensor : snapshot_.sensors) {
      if (sensor.producing) ++online;
    }
    const std::wstring summary =
        std::to_wstring(online) + L" of " + std::to_wstring(snapshot_.sensors.size()) + L" live";
    draw_text(summary, small_,
              D2D1::RectF(rect.left + pad, rect.top + 11.0f, rect.right - pad,
                          rect.top + 27.0f),
              theme.faint, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (snapshot_.sensors.empty()) {
      draw_text(L"No sensors are registered.", body_,
                D2D1::RectF(rect.left + pad, rect.top + 38.0f, rect.right - pad,
                            rect.bottom - 12.0f),
                theme.faint, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      return;
    }

    const float row_top = rect.top + 34.0f;
    const float row_height = 18.0f;
    const int max_rows = std::max(1, static_cast<int>((rect.bottom - row_top - 6.0f) / row_height));
    const int total = static_cast<int>(snapshot_.sensors.size());
    const int shown = std::min(total, max_rows);
    for (int i = 0; i < shown; ++i) {
      const float y = row_top + static_cast<float>(i) * row_height;
      const bool more_row = total > shown && i == shown - 1;
      if (more_row) {
        draw_text(L"+" + std::to_wstring(total - shown + 1) + L" more sources", small_,
                  D2D1::RectF(rect.left + pad + 12.0f, y, rect.right - pad, y + row_height),
                  theme.faint, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        break;
      }
      const SensorStatus& sensor = snapshot_.sensors[static_cast<std::size_t>(i)];
      D2D1_COLOR_F dot = theme.faint;
      std::wstring state = L"Unavailable";
      D2D1_COLOR_F state_color = theme.faint;
      if (sensor.producing) {
        dot = theme.green;
        state = L"Live";
        state_color = theme.green;
      } else if (sensor.available) {
        dot = theme.amber;
        state = L"Idle";
        state_color = theme.amber_text;
      }
      draw_circle(rect.left + pad + 4.0f, y + row_height / 2.0f, 3.2f, dot);
      draw_text(sensor_label(sensor.name), body_,
                D2D1::RectF(rect.left + pad + 14.0f, y, rect.right - pad, y + row_height),
                sensor.available ? theme.text : theme.muted);
      draw_text(state, small_,
                D2D1::RectF(rect.left + pad, y, rect.right - pad, y + row_height), state_color,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
  }

  void draw_system(const Theme& theme, D2D1_RECT_F rect) {
    draw_card(rect, theme);
    const float pad = 18.0f;
    draw_text(L"SYSTEM & DATA", label_,
              D2D1::RectF(rect.left + pad, rect.top + 12.0f, rect.right - pad,
                          rect.top + 26.0f),
              theme.muted);

    const Totals totals = totals_with_pending();
    const double split = totals.mwh_estimated + totals.mwh_measured;
    std::wstring estimate = L"\x2014";
    if (split > 0.0) {
      estimate = std::to_wstring(static_cast<int>(100.0 * totals.mwh_estimated / split + 0.5)) +
                 L"% (30 d)";
    }
    const std::wstring source = friendly_source(snapshot_.model.sourceId);
    const std::wstring base = widen(fmt_watts(settings_.base_watts)) + L" W";
    const std::wstring storage = store_ok_ ? storage_note_ : L"Unavailable";

    const float column_width = (rect.right - rect.left - pad * 2.0f - 16.0f) / 2.0f;
    const float label_width = 72.0f;
    struct Item {
      const wchar_t* label;
      std::wstring value;
    };
    const Item items[] = {
        {L"Model", capitalize(settings_.form_factor)},
        {L"Base draw", base + (settings_.calibrated ? L" \x00B7 calibrated" : L" \x00B7 default")},
        {L"Total source", source},
        {L"Estimate", estimate},
        {L"Storage", storage},
        {L"Refresh", L"1 second"},
    };
    for (int i = 0; i < 6; ++i) {
      const int column = i / 3;
      const int row = i % 3;
      const float x = rect.left + pad + static_cast<float>(column) * (column_width + 16.0f);
      const float y = rect.top + 38.0f + static_cast<float>(row) * 23.0f;
      draw_text(items[i].label, small_,
                D2D1::RectF(x, y, x + label_width, y + 20.0f), theme.faint,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      draw_text(items[i].value, body_,
                D2D1::RectF(x + label_width, y, x + column_width, y + 20.0f), theme.text,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (!storage_warning_.empty()) {
      draw_text(L"Storage: " + storage_warning_, micro_,
                D2D1::RectF(rect.left + pad, rect.bottom - 18.0f, rect.right - pad,
                            rect.bottom - 6.0f),
                theme.red);
    }
  }

  void draw_dropdown(const Theme& theme) {
    if (!span_open_) {
      span_menu_ = D2D1::RectF();
      for (auto& rect : span_options_) rect = D2D1::RectF();
      return;
    }
    const float item_height = 31.0f;
    span_menu_ = D2D1::RectF(span_button_.left, span_button_.bottom + 6.0f, span_button_.right,
                             span_button_.bottom + 6.0f + item_height * 4.0f + 8.0f);
    D2D1_RECT_F shadow = span_menu_;
    shadow.top += 4.0f;
    shadow.bottom += 4.0f;
    fill_round(shadow, 11.0f, theme.shadow);
    fill_round(span_menu_, 11.0f, theme.card);
    stroke_round(span_menu_, 11.0f, theme.border);
    for (int i = 0; i < 4; ++i) {
      const float top = span_menu_.top + 4.0f + static_cast<float>(i) * item_height;
      span_options_[i] = D2D1::RectF(span_menu_.left + 4.0f, top, span_menu_.right - 4.0f,
                                     top + item_height);
      if (i == hover_option_) {
        fill_round(span_options_[i], 7.0f, theme.menu_hover);
      }
      const bool selected = selected_span_ == kSpans[i];
      if (selected) draw_circle(span_options_[i].left + 12.0f, top + item_height / 2.0f, 3.2f,
                                theme.accent);
      draw_text(span_label(kSpans[i]), menu_,
                D2D1::RectF(span_options_[i].left + 24.0f, top,
                            span_options_[i].right - 10.0f, top + item_height),
                selected ? theme.accent : theme.text, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
  }

  void draw(const Theme& theme, float width, float height) {
    const float pad = width < 1100.0f ? 20.0f : 24.0f;
    const float right = width - pad;
    const float header_y = pad;
    const float content_top = header_y + 42.0f;
    const float hero_height = height < 780.0f ? 142.0f : 150.0f;
    const float totals_height = 84.0f;
    const float bottom_height = 118.0f;
    const float gap = 12.0f;
    const D2D1_RECT_F hero = D2D1::RectF(pad, content_top + 10.0f, right,
                                         content_top + 10.0f + hero_height);
    const D2D1_RECT_F totals =
        D2D1::RectF(pad, hero.bottom + gap, right, hero.bottom + gap + totals_height);
    const D2D1_RECT_F bottom =
        D2D1::RectF(pad, height - pad - bottom_height, right, height - pad);
    const D2D1_RECT_F chart =
        D2D1::RectF(pad, totals.bottom + gap, right, bottom.top - gap);
    const float sensors_width = (right - pad) * 0.58f;
    const D2D1_RECT_F sensors =
        D2D1::RectF(pad, bottom.top, pad + sensors_width, bottom.bottom);
    const D2D1_RECT_F system =
        D2D1::RectF(sensors.right + gap, bottom.top, right, bottom.bottom);

    draw_header(theme, pad, header_y, right);
    draw_hero(theme, hero);
    draw_totals(theme, totals, gap);
    draw_chart(theme, chart);
    draw_sensors(theme, sensors);
    draw_system(theme, system);
    draw_dropdown(theme);
  }

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  bool demo_ = false;
  float dpi_ = 96.0f;
  bool dark_ = false;
  bool follow_system_ = false;
  bool shut_down_ = false;
  bool tracking_mouse_ = false;
  Span selected_span_ = Span::Day;
  bool span_open_ = false;

  AppSettings settings_;
  std::string settings_path_;
  Store store_;
  bool store_ok_ = false;
  std::wstring storage_note_;
  std::wstring storage_warning_;
  Totals store_totals_;
  std::vector<SeriesPoint> series_;
  std::unique_ptr<Sampler> sampler_;
  Snapshot snapshot_;
  double now_ = 0.0;
  std::uint64_t last_flush_tick_ = 0;

  ComPtr<ID2D1Factory> factory_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IWICImagingFactory> wic_;
  ComPtr<ID2D1HwndRenderTarget> target_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  ComPtr<ID2D1Bitmap> logo_;

  ComPtr<IDWriteTextFormat> brand_;
  ComPtr<IDWriteTextFormat> subtitle_;
  ComPtr<IDWriteTextFormat> label_;
  ComPtr<IDWriteTextFormat> display_;
  ComPtr<IDWriteTextFormat> display_unit_;
  ComPtr<IDWriteTextFormat> section_;
  ComPtr<IDWriteTextFormat> metric_;
  ComPtr<IDWriteTextFormat> metric_small_;
  ComPtr<IDWriteTextFormat> body_;
  ComPtr<IDWriteTextFormat> body_medium_;
  ComPtr<IDWriteTextFormat> small_;
  ComPtr<IDWriteTextFormat> micro_;
  ComPtr<IDWriteTextFormat> chart_;
  ComPtr<IDWriteTextFormat> pill_;
  ComPtr<IDWriteTextFormat> menu_;

  D2D1_RECT_F theme_button_{};
  D2D1_RECT_F span_button_{};
  D2D1_RECT_F span_menu_{};
  D2D1_RECT_F span_options_[4]{};
  D2D1_RECT_F total_cards_[4]{};

  bool hover_theme_ = false;
  bool hover_span_button_ = false;
  int hover_total_ = -1;
  int hover_option_ = -1;
};

}  // namespace

int run_dashboard(HINSTANCE instance, bool demo) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int result = 1;
  {
    Dashboard dashboard(instance, demo);
    if (dashboard.create()) result = dashboard.message_loop();
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return result;
}

bool focus_dashboard_window() {
  HWND hwnd = FindWindowW(kWindowClass, nullptr);
  if (!hwnd) return false;
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
  return true;
}

}  // namespace wp
