/**
 * @file    vfs/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "vfs/settings.h"

#include <string>

#include <vector>

#include <rex/cvar.h>

#include "core/cvar_table.h"

namespace bd::vfs {
namespace {

std::vector<rex::cvar::CallbackHandle> g_callbacks;

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptModLog() { modLog_ = REXCVAR_GET(bd::cvars::mod_log); }

void Settings::AdoptModLogKeep() { modLogKeep_ = REXCVAR_GET(bd::cvars::mod_log_keep); }

void Settings::AdoptModLogMaxMB() {
  modLogMaxMB_ = REXCVAR_GET(bd::cvars::mod_log_max_mb);
}

void Settings::AdoptDiscPrefetch() {
  discPrefetch_ = REXCVAR_GET(bd::cvars::disc_prefetch);
}

bool Settings::SetModLog(bool v) {
  return bd::CvarSet("bd.mod_log", FormatCvar(v));
}

bool Settings::SetDiscPrefetch(bool v) {
  return bd::CvarSet("bd.disc_prefetch", FormatCvar(v));
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
