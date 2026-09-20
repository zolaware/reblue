/**
 * @file    ui/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "ui/settings.h"

#include <string>

#include <rex/cvar.h>

#include "core/settings.h" // kCvarGroup

REXCVAR_DECLARE(i32, bd_cursor_hide_seconds);
REXCVAR_DECLARE(i32, bd_perf_overlay);
REXCVAR_DECLARE(i32, bd_perf_overlay_alpha);

REXCVAR_DEFINE_INT32(bd_cursor_hide_seconds, 5, kCvarGroup,
                     "Seconds of mouse inactivity over the window before the "
                     "cursor hides. 0 = never hide.")
    .range(0, 300);

REXCVAR_DEFINE_INT32(bd_perf_overlay, 0, kCvarGroup,
                     "F3 overlay stage: 0 off, 1 KPI strip, 2 graphs.")
    .range(0, 2);

REXCVAR_DEFINE_INT32(bd_perf_overlay_alpha, 35, kCvarGroup,
                     "Perf overlay background opacity, percent.")
    .range(0, 100);

namespace bd::ui {
namespace {

std::string FormatCvar(i32 v) { return std::to_string(v); }

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

// Adopt: pull the value the cvar layer holds and run the setting's reaction.
// This is the only place either of those happens.
void Settings::AdoptCursorHideSeconds() {
  cursorHideSeconds_ = REXCVAR_GET(bd_cursor_hide_seconds);
  if (cursorApplier_)
    cursorApplier_(cursorHideSeconds_);
}

// The overlay stage has no host state of its own. The applier ReblueApp
// installs once the ImGui drawer exists is the reaction. Before that, or if
// none is installed, adopting only updates the member.
void Settings::AdoptPerfOverlay() {
  perfOverlay_ = REXCVAR_GET(bd_perf_overlay);
  if (overlayApplier_)
    overlayApplier_(perfOverlay_);
}

void Settings::AdoptPerfOverlayAlpha() {
  perfOverlayAlpha_ = REXCVAR_GET(bd_perf_overlay_alpha);
}

// Set: hand the value to the cvar layer and let the callback adopt it back.
// Going through SetFlagByName rather than REXCVAR_SET keeps the range check,
// the restart-pending bookkeeping and any callback another subsystem
// registered on this setting. It cannot recurse, because the callback adopts
// and never calls a setter. False means the cvar layer rejected the value and
// nothing changed.
bool Settings::SetCursorHideSeconds(i32 v) {
  return rex::cvar::SetFlagByName("bd_cursor_hide_seconds", FormatCvar(v));
}

bool Settings::SetPerfOverlay(i32 v) {
  return rex::cvar::SetFlagByName("bd_perf_overlay", FormatCvar(v));
}

bool Settings::SetPerfOverlayAlpha(i32 v) {
  return rex::cvar::SetFlagByName("bd_perf_overlay_alpha", FormatCvar(v));
}

void Settings::SetOverlayApplier(std::function<void(i32)> applier) {
  overlayApplier_ = std::move(applier);
  if (overlayApplier_)
    overlayApplier_(perfOverlay_);
}

void Settings::SetCursorApplier(std::function<void(i32)> applier) {
  cursorApplier_ = std::move(applier);
  if (cursorApplier_)
    cursorApplier_(cursorHideSeconds_);
}

void Settings::AdoptCvars() {
  AdoptCursorHideSeconds();
  AdoptPerfOverlay();
  AdoptPerfOverlayAlpha();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Settings::Get().*adopt)();
        });
  };
  reg("bd_cursor_hide_seconds", &Settings::AdoptCursorHideSeconds);
  reg("bd_perf_overlay", &Settings::AdoptPerfOverlay);
  reg("bd_perf_overlay_alpha", &Settings::AdoptPerfOverlayAlpha);
}

} // namespace bd::ui
