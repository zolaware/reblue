/**
 * @file    core/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/settings.h"

#include "core/build_info.h"

#include <charconv>
#include <system_error>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, bd_devmode);
REXCVAR_DECLARE(bool, bd_dbgprint);
REXCVAR_DECLARE(std::string, bd_language);
REXCVAR_DECLARE(bool, bd_i18n_keys);
REXCVAR_DECLARE(std::string, bd_lang_path);
REXCVAR_DECLARE(i32, bd_perf_history_seconds);
REXCVAR_DECLARE(bool, bd_perf_csv);
REXCVAR_DECLARE(bool, bd_profiler);
REXCVAR_DECLARE(i32, bd_shutdown_timeout_ms);
REXCVAR_DECLARE(bool, bd_update_check);
REXCVAR_DECLARE(std::string, bd_update_base);
REXCVAR_DECLARE(std::string, bd_update_channel);
REXCVAR_DECLARE(std::string, bd_saves_path);
REXCVAR_DECLARE(std::string, bd_cache_path);

REXCVAR_DEFINE_BOOL(bd_devmode, false, kCvarGroup,
                    "Developer mode: debug menu boot, Mindows overlay (F11) "
                    "and debug keyboard input.");
REXCVAR_DEFINE_BOOL(bd_dbgprint, false, kCvarGroup,
                    "Forward guest DbgPrint output to the host log.");

REXCVAR_DEFINE_STRING(bd_language, "auto", kCvarGroup,
                      "UI text language: auto, us, jp, de, fr, es, it, kr, "
                      "tw, cn, po. Requires restart.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(bd_i18n_keys, false, kCvarGroup,
                    "Show catalog keys instead of translated text, so a string "
                    "on screen names the entry that produced it.");

REXCVAR_DEFINE_STRING(bd_lang_path, "lang", kCvarGroup,
                      "Folder searched for a localization.toml of UI text "
                      "overrides. Relative to the app data folder.");

REXCVAR_DEFINE_INT32(bd_perf_history_seconds, 20, kCvarGroup,
                     "Seconds of per-frame telemetry retained for the F3 "
                     "overlay and CSV capture.")
    .range(5, 120);

REXCVAR_DEFINE_BOOL(bd_perf_csv, false, kCvarGroup,
                    "Write per-frame telemetry to logs/perf/*.csv. Toggling "
                    "starts a new file.");

REXCVAR_DEFINE_BOOL(bd_profiler, false, kCvarGroup,
                    "Start the Tracy profiler at boot so a viewer can attach. "
                    "Read at startup only, and inert in playtest builds, which "
                    "compile the profiler out.");

REXCVAR_DEFINE_INT32(bd_shutdown_timeout_ms, 1500, kCvarGroup,
                     "Budget for the ordered shutdown (guest quiesce + GPU "
                     "drain) before the process is killed outright.")
    .range(100, 30000);

REXCVAR_DEFINE_BOOL(bd_update_check, true, kCvarGroup,
                    "Ask the update endpoint at startup: offer a newer re:Blue "
                    "release, and fetch the content packs it points at.");

REXCVAR_DEFINE_STRING(bd_update_base, REBLUE_UPDATE_BASE, kCvarGroup,
                      "Update endpoint the startup check asks, without a "
                      "trailing slash. Empty disables the check.");

REXCVAR_DEFINE_STRING(bd_update_channel, REBLUE_UPDATE_CHANNEL, kCvarGroup,
                      "Which builds the check offers: stable or nightly. "
                      "Names the manifest read under bd_update_base.");

// Saves live beside the game, NOT under user_data_root: that root is handed to
// the XAM content manager, so nesting saves inside it makes the two contend
// over the same host subtree and breaks overwrites.
REXCVAR_DEFINE_STRING(
    bd_saves_path, "", "reblue",
    "Save-game directory. Empty = <install_root>/saves (beside the game).");
REXCVAR_DEFINE_STRING(
    bd_cache_path, "", "reblue",
    "Transient cache directory (PSO capture). Empty = <exe_dir>/cache.");

namespace bd {
namespace {

UpdateChannel ChannelOfName(const std::string &name) {
  return name == ToString(UpdateChannel::Nightly) ? UpdateChannel::Nightly
                                                  : UpdateChannel::Stable;
}

} // namespace

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

std::string FormatCvar(f64 v) {
  char buf[32];
  auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  return ec == std::errc() ? std::string(buf, end) : std::string("0");
}

const char *ToString(UpdateChannel channel) {
  switch (channel) {
  case UpdateChannel::Nightly:
    return "nightly";
  case UpdateChannel::Stable:
    break;
  }
  return "stable";
}

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptDevmode() {
  devmode_ = REXCVAR_GET(bd_devmode);
  if (devmodeApplier_)
    devmodeApplier_();
}
void Settings::AdoptDbgPrint() { dbgPrint_ = REXCVAR_GET(bd_dbgprint); }
void Settings::AdoptLanguage() { language_ = REXCVAR_GET(bd_language); }
void Settings::AdoptI18nKeys() { i18nKeys_ = REXCVAR_GET(bd_i18n_keys); }
void Settings::AdoptLanguagePath() {
  languagePath_ = REXCVAR_GET(bd_lang_path);
}
void Settings::AdoptPerfHistorySeconds() {
  perfHistorySeconds_ = REXCVAR_GET(bd_perf_history_seconds);
}
void Settings::AdoptPerfCSV() { perfCSV_ = REXCVAR_GET(bd_perf_csv); }
void Settings::AdoptProfiler() { profiler_ = REXCVAR_GET(bd_profiler); }
void Settings::AdoptShutdownTimeoutMs() {
  shutdownTimeoutMs_ = REXCVAR_GET(bd_shutdown_timeout_ms);
}
void Settings::AdoptUpdateCheck() {
  updateCheck_ = REXCVAR_GET(bd_update_check);
}
void Settings::AdoptUpdateBase() {
  updateBase_ = REXCVAR_GET(bd_update_base);
  ComposeUpdateUrl();
}
void Settings::AdoptUpdateChannel() {
  updateChannel_ = ChannelOfName(REXCVAR_GET(bd_update_channel));
  ComposeUpdateUrl();
}

void Settings::ComposeUpdateUrl() {
  updateUrl_ = updateBase_.empty()
                   ? std::string()
                   : updateBase_ + "/manifest/" + ToString(updateChannel_) +
                         ".toml";
}

void Settings::AdoptSavesPath() { savesPath_ = REXCVAR_GET(bd_saves_path); }
void Settings::AdoptCachePath() { cachePath_ = REXCVAR_GET(bd_cache_path); }

// Each hands the value to the cvar layer and lets the registered change
// callback adopt it back. SetFlagByName keeps the range check, the restart
// bookkeeping and any callback another subsystem registered on the same
// setting, and its return says whether the write was accepted. It cannot
// recurse: the callback adopts and never calls a setter.
bool Settings::SetDevmode(bool v) {
  return rex::cvar::SetFlagByName("bd_devmode", FormatCvar(v));
}

void Settings::SetDevmodeApplier(std::function<void()> applier) {
  devmodeApplier_ = std::move(applier);
  if (devmodeApplier_)
    devmodeApplier_();
}

bool Settings::SetDbgPrint(bool v) {
  return rex::cvar::SetFlagByName("bd_dbgprint", FormatCvar(v));
}

bool Settings::SetLanguage(const std::string &v) {
  return rex::cvar::SetFlagByName("bd_language", v);
}

bool Settings::SetI18nKeys(bool v) {
  return rex::cvar::SetFlagByName("bd_i18n_keys", FormatCvar(v));
}

bool Settings::SetPerfHistorySeconds(i32 v) {
  return rex::cvar::SetFlagByName("bd_perf_history_seconds", FormatCvar(v));
}

bool Settings::SetPerfCSV(bool v) {
  return rex::cvar::SetFlagByName("bd_perf_csv", FormatCvar(v));
}

bool Settings::SetShutdownTimeoutMs(i32 v) {
  return rex::cvar::SetFlagByName("bd_shutdown_timeout_ms", FormatCvar(v));
}

bool Settings::SetUpdateCheck(bool v) {
  return rex::cvar::SetFlagByName("bd_update_check", FormatCvar(v));
}

bool Settings::SetUpdateChannel(bd::UpdateChannel v) {
  return rex::cvar::SetFlagByName("bd_update_channel", ToString(v));
}

void Settings::AdoptCvars() {
  AdoptDevmode();
  AdoptDbgPrint();
  AdoptLanguage();
  AdoptI18nKeys();
  AdoptLanguagePath();
  AdoptPerfHistorySeconds();
  AdoptPerfCSV();
  AdoptProfiler();
  AdoptShutdownTimeoutMs();
  AdoptUpdateCheck();
  AdoptUpdateBase();
  AdoptUpdateChannel();
  AdoptSavesPath();
  AdoptCachePath();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Settings::Get().*adopt)();
        });
  };
  reg("bd_devmode", &Settings::AdoptDevmode);
  reg("bd_dbgprint", &Settings::AdoptDbgPrint);
  reg("bd_language", &Settings::AdoptLanguage);
  reg("bd_i18n_keys", &Settings::AdoptI18nKeys);
  reg("bd_lang_path", &Settings::AdoptLanguagePath);
  reg("bd_perf_history_seconds", &Settings::AdoptPerfHistorySeconds);
  reg("bd_perf_csv", &Settings::AdoptPerfCSV);
  reg("bd_profiler", &Settings::AdoptProfiler);
  reg("bd_shutdown_timeout_ms", &Settings::AdoptShutdownTimeoutMs);
  reg("bd_update_check", &Settings::AdoptUpdateCheck);
  reg("bd_update_base", &Settings::AdoptUpdateBase);
  reg("bd_update_channel", &Settings::AdoptUpdateChannel);
  reg("bd_saves_path", &Settings::AdoptSavesPath);
  reg("bd_cache_path", &Settings::AdoptCachePath);
}

} // namespace bd
