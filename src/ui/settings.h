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

  void SetCursorApplier(std::function<void(i32)> applier);

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the others.
  void AdoptCursorHideSeconds();

  i32 cursorHideSeconds_ = 5;

  std::function<void(i32)> cursorApplier_;
};

} // namespace bd::ui
