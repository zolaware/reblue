/**
 * @file    core/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/settings.h"

#include "core/cvar_table.h"

#include "core/build_info.h"

#include <charconv>
#include <system_error>

#include <vector>

#include <rex/cvar.h>

namespace bd {
namespace {

std::vector<rex::cvar::CallbackHandle> g_callbacks;

rex::cvar::ICvarRegistry *g_registry = nullptr;

UpdateChannel ChannelOfName(const std::string &name) {
  return name == ToString(UpdateChannel::Nightly) ? UpdateChannel::Nightly
                                                  : UpdateChannel::Stable;
}

} // namespace

rex::cvar::ICvarRegistry &Cvars() { return *g_registry; }

void SetCvars(rex::cvar::ICvarRegistry *registry) { g_registry = registry; }

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
  devmode_ = REXCVAR_GET(bd::cvars::devmode);
  if (devmodeApplier_)
    devmodeApplier_();
}
void Settings::AdoptLanguage() { language_ = REXCVAR_GET(bd::cvars::language); }
void Settings::AdoptI18nKeys() { i18nKeys_ = REXCVAR_GET(bd::cvars::i18n_keys); }
void Settings::AdoptLanguagePath() {
  languagePath_ = REXCVAR_GET(bd::cvars::lang_path);
}
void Settings::AdoptProfiler() { profiler_ = REXCVAR_GET(bd::cvars::profiler); }
void Settings::AdoptShutdownTimeoutMs() {
  shutdownTimeoutMs_ = REXCVAR_GET(bd::cvars::shutdown_timeout_ms);
}
void Settings::AdoptUpdateCheck() {
  updateCheck_ = REXCVAR_GET(bd::cvars::update_check);
}
void Settings::AdoptUpdateBase() {
  updateBase_ = REXCVAR_GET(bd::cvars::update_base);
  ComposeUpdateUrl();
}
void Settings::AdoptUpdateChannel() {
  updateChannel_ = ChannelOfName(REXCVAR_GET(bd::cvars::update_channel));
  ComposeUpdateUrl();
}

void Settings::ComposeUpdateUrl() {
  updateUrl_ = updateBase_.empty()
                   ? std::string()
                   : updateBase_ + "/manifest/" + ToString(updateChannel_) +
                         ".toml";
}

void Settings::AdoptSavesPath() { savesPath_ = REXCVAR_GET(bd::cvars::saves_path); }
void Settings::AdoptCachePath() { cachePath_ = REXCVAR_GET(bd::cvars::cache_path); }

// Each hands the value to the cvar layer and lets the registered change
// callback adopt it back. The registry keeps the range check, the restart
// bookkeeping and any callback another subsystem registered on the same
// setting, and its return says whether the write was accepted. It cannot
// recurse: the callback adopts and never calls a setter.
bool Settings::SetDevmode(bool v) {
  return bd::CvarSet("bd.devmode", FormatCvar(v));
}

void Settings::SetDevmodeApplier(std::function<void()> applier) {
  devmodeApplier_ = std::move(applier);
  if (devmodeApplier_)
    devmodeApplier_();
}

bool Settings::SetLanguage(const std::string &v) {
  return bd::CvarSet("bd.language", v);
}

bool Settings::SetI18nKeys(bool v) {
  return bd::CvarSet("bd.i18n_keys", FormatCvar(v));
}

bool Settings::SetShutdownTimeoutMs(i32 v) {
  return bd::CvarSet("bd.shutdown_timeout_ms", FormatCvar(v));
}

bool Settings::SetUpdateCheck(bool v) {
  return bd::CvarSet("bd.update_check", FormatCvar(v));
}

bool Settings::SetUpdateChannel(bd::UpdateChannel v) {
  return bd::CvarSet("bd.update_channel", ToString(v));
}

void Settings::AdoptCvars() {
  AdoptDevmode();
  AdoptLanguage();
  AdoptI18nKeys();
  AdoptLanguagePath();
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
    bd::CvarWatch(g_callbacks, name,
                  [adopt](std::string_view, std::string_view) {
                    (Settings::Get().*adopt)();
                  });
  };
  reg("bd.devmode", &Settings::AdoptDevmode);
  reg("bd.language", &Settings::AdoptLanguage);
  reg("bd.i18n_keys", &Settings::AdoptI18nKeys);
  reg("bd.lang_path", &Settings::AdoptLanguagePath);
  reg("bd.profiler", &Settings::AdoptProfiler);
  reg("bd.shutdown_timeout_ms", &Settings::AdoptShutdownTimeoutMs);
  reg("bd.update_check", &Settings::AdoptUpdateCheck);
  reg("bd.update_base", &Settings::AdoptUpdateBase);
  reg("bd.update_channel", &Settings::AdoptUpdateChannel);
  reg("bd.saves_path", &Settings::AdoptSavesPath);
  reg("bd.cache_path", &Settings::AdoptCachePath);
}

} // namespace bd
