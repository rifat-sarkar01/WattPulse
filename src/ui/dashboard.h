#pragma once

#include <windows.h>

namespace wp {

// Opens the Direct2D dashboard. Blocks until the window closes.
int run_dashboard(HINSTANCE instance, bool demo);

// Brings an already-running dashboard to the foreground.
bool focus_dashboard_window();

}  // namespace wp
