/**
 * @file    vfs/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "vfs/settings.h"

#include <string>

#include <vector>


#include "core/cvar_table.h"

namespace bd::vfs {
namespace {

bd::SettingWatches g_callbacks;

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptModLog() { modLog_ = bd::cvars::Get().mod_log.Get(); }

void Settings::AdoptModLogKeep() { modLogKeep_ = bd::cvars::Get().mod_log_keep.Get(); }

void Settings::AdoptModLogMaxMB() {
  modLogMaxMB_ = bd::cvars::Get().mod_log_max_mb.Get();
}

void Settings::AdoptDiscPrefetch() {
  discPrefetch_ = bd::cvars::Get().disc_prefetch.Get();
}

void Settings::AdoptCvars() {
  AdoptModLog();
  AdoptModLogKeep();
  AdoptModLogMaxMB();
  AdoptDiscPrefetch();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    bd::CvarWatch(g_callbacks, name,
                  [adopt](std::string_view, std::string_view) {
                    (Settings::Get().*adopt)();
                  });
  };
  reg("bd.mod_log", &Settings::AdoptModLog);
  reg("bd.mod_log_keep", &Settings::AdoptModLogKeep);
  reg("bd.mod_log_max_mb", &Settings::AdoptModLogMaxMB);
  reg("bd.disc_prefetch", &Settings::AdoptDiscPrefetch);
}

} // namespace bd::vfs
