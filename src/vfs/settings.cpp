/**
 * @file    vfs/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "vfs/settings.h"

#include <string>

#include <rex/cvar.h>

#include "core/settings.h" // kCvarGroup

REXCVAR_DECLARE(bool, bd_mod_log);
REXCVAR_DECLARE(i32, bd_mod_log_keep);
REXCVAR_DECLARE(i32, bd_mod_log_max_mb);
REXCVAR_DECLARE(bool, bd_disc_prefetch);

REXCVAR_DEFINE_BOOL(bd_mod_log, false, kCvarGroup,
                    "Log guest file accesses to logs/file_access_summary.csv "
                    "and per-session logs under logs/mod_access_logs/.");
REXCVAR_DEFINE_INT32(bd_mod_log_keep, 10, kCvarGroup,
                     "Previous-session mod access logs kept at boot.");
REXCVAR_DEFINE_INT32(bd_mod_log_max_mb, 64, kCvarGroup,
                     "Per-session mod access log size cap in MB. 0 = "
                     "unlimited.");
REXCVAR_DEFINE_BOOL(bd_disc_prefetch, true, kCvarGroup,
                    "Open every loose file under pack, snd_memory and "
                    "snd_memory_jp once at boot on a background thread so the "
                    "OS caches their metadata and the game's first opens do "
                    "not stall on a cold disk.");

namespace bd::vfs {
namespace {

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptModLog() { modLog_ = REXCVAR_GET(bd_mod_log); }

void Settings::AdoptModLogKeep() { modLogKeep_ = REXCVAR_GET(bd_mod_log_keep); }

void Settings::AdoptModLogMaxMB() {
  modLogMaxMB_ = REXCVAR_GET(bd_mod_log_max_mb);
}

void Settings::AdoptDiscPrefetch() {
  discPrefetch_ = REXCVAR_GET(bd_disc_prefetch);
}

bool Settings::SetModLog(bool v) {
  return rex::cvar::SetFlagByName("bd_mod_log", FormatCvar(v));
}

bool Settings::SetDiscPrefetch(bool v) {
  return rex::cvar::SetFlagByName("bd_disc_prefetch", FormatCvar(v));
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
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Settings::Get().*adopt)();
        });
  };
  reg("bd_mod_log", &Settings::AdoptModLog);
  reg("bd_mod_log_keep", &Settings::AdoptModLogKeep);
  reg("bd_mod_log_max_mb", &Settings::AdoptModLogMaxMB);
  reg("bd_disc_prefetch", &Settings::AdoptDiscPrefetch);
}

} // namespace bd::vfs
