/**
 * @file    vfs/settings.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::vfs {

class Settings {
public:
  static Settings &Get();

  // Adopts the current cvar values, then registers a change callback per
  // setting so console, config file and launch argument writes reach here.
  // Called once from ReblueApp::OnInit, the first consumer hook
  // after rex::Init has applied the config file.
  void Init();

  // Re-reads every setting from the registry.
  void AdoptCvars();

  bool ModLog() const { return modLog_; }

  bool DiscPrefetch() const { return discPrefetch_; }

  // Both are read when the log opens at boot.
  i32 ModLogKeep() const { return modLogKeep_; }
  i32 ModLogMaxMB() const { return modLogMaxMB_; }

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  void AdoptModLog();
  void AdoptModLogKeep();
  void AdoptModLogMaxMB();
  void AdoptDiscPrefetch();

  bool modLog_ = false;
  i32 modLogKeep_ = 10;
  i32 modLogMaxMB_ = 64;
  bool discPrefetch_ = true;
};

} // namespace bd::vfs
