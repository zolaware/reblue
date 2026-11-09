/**
 * @file    audio/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "audio/settings.h"

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

#include <vector>

#include <rex/apu.h>
#include <rex/cvar.h>

#include "core/cvar_table.h"

namespace bd::audio {
namespace {

std::vector<rex::cvar::CallbackHandle> g_callbacks;

// std::to_string on a double is sprintf("%f"): six decimals and
// locale-sensitive. to_chars is neither.
std::string FormatCvar(f64 v) {
  char buf[32];
  auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  return ec == std::errc() ? std::string(buf, end) : std::string("0");
}

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::Attach(rex::apu::IAPUBackend *backend) {
  backend_ = backend;
  AdoptGain();
  AdoptSurroundMix();
}

// Adopt pulls the value the cvar layer holds and runs the setting's reaction.
// Attach replays both reactions once the apu module's backend exists.
//
// The SDK output stage also owns the stereo fold, and reblue deliberately
// leaves it at the default: BD's mixer emits the same signal in the LFE and
// both surround slots, so folding LFE as well would count that bus twice.
void Settings::AdoptGain() {
  gain_ = REXCVAR_GET(bd::cvars::audio_gain);
  if (backend_)
    backend_->SetOutputGain(static_cast<f32>(gain_));
}

// One setting in three parts: the output stage takes them as a struct, so a
// change to any one of them republishes all three.
void Settings::AdoptSurroundMix() {
  centerLevel_ = REXCVAR_GET(bd::cvars::audio_center);
  surroundLevel_ = REXCVAR_GET(bd::cvars::audio_surround);
  lfeLevel_ = REXCVAR_GET(bd::cvars::audio_lfe);
  if (backend_)
    backend_->SetSurroundMix({.center = static_cast<f32>(centerLevel_),
                              .surround = static_cast<f32>(surroundLevel_),
                              .lfe = static_cast<f32>(lfeLevel_)});
}

void Settings::AdoptQueueFrames() {
  queueFrames_ = REXCVAR_GET(bd::cvars::audio_qframes);
}

void Settings::AdoptCueMonitor() { cueMonitor_ = REXCVAR_GET(bd::cvars::audio_debug); }

void Settings::AdoptPeakMeter() { peakMeter_ = REXCVAR_GET(bd::cvars::audio_peak); }

void Settings::AdoptLog() { log_ = REXCVAR_GET(bd::cvars::audio_log); }

void Settings::AdoptOutputLimit() { outputLimit_ = REXCVAR_GET(bd::cvars::audio_limit); }

// Set: hand the value to the cvar layer and let the callback adopt it back.
// Going through the registry rather than the cell keeps the range check,
// the restart-pending bookkeeping and any callback another subsystem
// registered on this setting. It cannot recurse, because the callback adopts
// and never calls a setter. False means the cvar layer rejected the value and
// nothing changed.
bool Settings::SetGain(f64 v) {
  return bd::CvarSet("bd.audio_gain", FormatCvar(v));
}

bool Settings::SetCenterLevel(f64 v) {
  return bd::CvarSet("bd.audio_center", FormatCvar(v));
}

bool Settings::SetSurroundLevel(f64 v) {
  return bd::CvarSet("bd.audio_surround", FormatCvar(v));
}

bool Settings::SetLFELevel(f64 v) {
  return bd::CvarSet("bd.audio_lfe", FormatCvar(v));
}

bool Settings::SetCueMonitor(i32 v) {
  return bd::CvarSet("bd.audio_debug", FormatCvar(v));
}

bool Settings::SetPeakMeter(i32 v) {
  return bd::CvarSet("bd.audio_peak", FormatCvar(v));
}

bool Settings::SetLog(bool v) {
  return bd::CvarSet("bd.audio_log", FormatCvar(v));
}

bool Settings::SetOutputLimit(f64 v) {
  return bd::CvarSet("bd.audio_limit", FormatCvar(v));
}

void Settings::AdoptCvars() {
  AdoptGain();
  AdoptSurroundMix();
  AdoptQueueFrames();
  AdoptCueMonitor();
  AdoptPeakMeter();
  AdoptLog();
  AdoptOutputLimit();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    bd::CvarWatch(g_callbacks, name,
                  [adopt](std::string_view, std::string_view) {
                    (Settings::Get().*adopt)();
                  });
  };
  reg("bd.audio_gain", &Settings::AdoptGain);
  reg("bd.audio_center", &Settings::AdoptSurroundMix);
  reg("bd.audio_surround", &Settings::AdoptSurroundMix);
  reg("bd.audio_lfe", &Settings::AdoptSurroundMix);
  reg("bd.audio_qframes", &Settings::AdoptQueueFrames);
  reg("bd.audio_debug", &Settings::AdoptCueMonitor);
  reg("bd.audio_peak", &Settings::AdoptPeakMeter);
  reg("bd.audio_log", &Settings::AdoptLog);
  reg("bd.audio_limit", &Settings::AdoptOutputLimit);
}

} // namespace bd::audio
