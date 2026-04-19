#pragma once

#include <string_view>

namespace reblue::ui {

// Blocking modal error dialog. Returns when the user dismisses. No presenter
// or window required - safe to call before SetupPresentation(). No-op on
// non-Windows platforms (logs and returns).
void ShowFatalError(std::string_view title, std::string_view body);

}  // namespace reblue::ui
