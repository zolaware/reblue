/**
 * @file    ui/settings.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include <functional>

#include <rex/types.h>

namespace bd::ui {

class Settings {
public:
  static Settings &Get();

  // Adopts the current cvar values, then registers a change callback per
  // setting so console, config file and launch argument writes reach here.
  // Called once from ReblueApp::OnPostInitLogging, the first consumer hook
  // after rex::cvar::LoadConfig has run.
  void Init();

  // Re-reads every setting from cvar storage. rex::cvar::ResetToDefault and
  // ResetAllToDefaults write the storage without firing a change callback, so
  // anything that calls them calls this after.
  void AdoptCvars();

  // Seconds of mouse inactivity over the window before the cursor hides.
  i32 CursorHideSeconds() const { return cursorHideSeconds_; }
  bool SetCursorHideSeconds(i32 v);

  // F3 overlay stage: 0 off, 1 KPI strip, 2 graphs, 3 interactive.
  i32 PerfOverlay() const { return perfOverlay_; }
  bool SetPerfOverlay(i32 v);

  // Perf overlay background opacity, percent.
  i32 PerfOverlayAlpha() const { return perfOverlayAlpha_; }
  bool SetPerfOverlayAlpha(i32 v);

  // The overlay stage is applied through the ImGui drawer, which does not
  // exist yet when Init runs. ReblueApp installs the applier once it does,
  // and from then on every stage change reaches it, including one typed at
  // the console.
  void SetOverlayApplier(std::function<void(i32)> applier);

  void SetCursorApplier(std::function<void(i32)> applier);

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the others.
  void AdoptCursorHideSeconds();
  void AdoptPerfOverlay();
  void AdoptPerfOverlayAlpha();

  i32 cursorHideSeconds_ = 5;
  i32 perfOverlay_ = 0;
  i32 perfOverlayAlpha_ = 35;

  std::function<void(i32)> overlayApplier_;
  std::function<void(i32)> cursorApplier_;
};

} // namespace bd::ui
