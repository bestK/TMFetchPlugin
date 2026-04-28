#pragma once
#include "Config.h"
#include <Windows.h>

namespace ui {

// Show the modal main options dialog.
// Returns true if user clicked OK and cfg was modified, false if Cancel.
bool ShowOptions(HWND parent, HINSTANCE hInst, PluginConfig& cfg);

} // namespace ui
