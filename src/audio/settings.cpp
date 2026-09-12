/**
 * @file    audio/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "audio/settings.h"

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>

#include "core/settings.h" // kCvarGroup

REXCVAR_DECLARE(double, bd_audio_gain);
REXCVAR_DECLARE(double, bd_audio_center);
REXCVAR_DECLARE(double, bd_audio_surround);
REXCVAR_DECLARE(double, bd_audio_lfe);
REXCVAR_DECLARE(i32, bd_audio_qframes);
REXCVAR_DECLARE(i32, bd_audio_debug);
REXCVAR_DECLARE(i32, bd_audio_peak);
REXCVAR_DECLARE(bool, bd_audio_log);
REXCVAR_DECLARE(double, bd_audio_limit);

// A range alone does not reject NaN: neither NaN < min nor NaN > max is ever
// true, so it passes validation and multiplies every output sample forever.
REXCVAR_DEFINE_DOUBLE(bd_audio_gain, 1.0, kCvarGroup,
                      "Master gain of the audio output.")
    .range(0.0, 4.0)
    .validator([](std::string_view v) {
      f64 d = 0;
      return rex::cvar::ParseDouble(v, d) && std::isfinite(d);
    });
REXCVAR_DEFINE_DOUBLE(bd_audio_center, 1.0, kCvarGroup,
                      "Center channel level on a surround device.")
    .range(0.0, 2.0)
    .validator([](std::string_view v) {
      f64 d = 0;
      return rex::cvar::ParseDouble(v, d) && std::isfinite(d);
    });
REXCVAR_DEFINE_DOUBLE(bd_audio_surround, 1.0, kCvarGroup,
                      "Rear channel level on a surround device.")
    .range(0.0, 2.0)
    .validator([](std::string_view v) {
      f64 d = 0;
      return rex::cvar::ParseDouble(v, d) && std::isfinite(d);
    });
// BD puts a full-band copy of its effects bus in this slot rather than an LFE,
// and the slot is reproduced around 10 dB hot. A receiver that folds it without
// low-passing puts that whole band on the music, so it defaults to off. The
// stereo fold has always discarded it.
REXCVAR_DEFINE_DOUBLE(bd_audio_lfe, 0.0, kCvarGroup,
                      "LFE channel level on a surround device.")
    .range(0.0, 2.0)
    .validator([](std::string_view v) {
      f64 d = 0;
      return rex::cvar::ParseDouble(v, d) && std::isfinite(d);
    });
REXCVAR_DEFINE_INT32(bd_audio_qframes, 16, kCvarGroup,
                     "Audio pacing queue depth in 256-sample frames. Requires "
                     "restart.")
    .range(4, 64)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(bd_audio_debug, 0, kCvarGroup,
                     "Engine cue-monitor overlay: 0 off, 1 all cue slots, 2 "
                     "active cues only. Needs bd_devmode.")
    .range(0, 2);
REXCVAR_DEFINE_INT32(bd_audio_peak, 0, kCvarGroup,
                     "Engine output peak-meter overlay: 0 off, 1 meters, 2 "
                     "meters with dB readout. Needs bd_devmode.")
    .range(0, 2);
REXCVAR_DEFINE_BOOL(bd_audio_log, false, kCvarGroup,
                    "Log cue playback and voice pitch changes.");
REXCVAR_DEFINE_DOUBLE(bd_audio_limit, 8.0, kCvarGroup,
                      "Peak the final mix may reach before the output guard "
                      "mutes it: 0 disables the guard.")
    .range(0.0, 1024.0)
    .validator([](std::string_view v) {
      f64 d = 0;
      return rex::cvar::ParseDouble(v, d) && std::isfinite(d);
    });

namespace bd::audio {
namespace {

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

// Adopt: pull the value the cvar layer holds and run the setting's reaction.
// This is the only place either of those happens. rex::audio::SetOutputGain
// only writes a mutex-guarded process-global the output stage samples on its
// next device callback, so it is safe to call before that stage exists.
//
// The SDK output stage also owns the stereo fold, and reblue deliberately
// leaves it at the default: BD's mixer emits the same signal in the LFE and
// both surround slots, so folding LFE as well would count that bus twice.
void Settings::AdoptGain() {
  gain_ = REXCVAR_GET(bd_audio_gain);
  rex::audio::SetOutputGain(static_cast<f32>(gain_));
}

// One setting in three parts: the output stage takes them as a struct, so a
// change to any one of them republishes all three.
void Settings::AdoptSurroundMix() {
  centerLevel_ = REXCVAR_GET(bd_audio_center);
  surroundLevel_ = REXCVAR_GET(bd_audio_surround);
  lfeLevel_ = REXCVAR_GET(bd_audio_lfe);
  rex::audio::SetSurroundMix({.center = static_cast<f32>(centerLevel_),
                              .surround = static_cast<f32>(surroundLevel_),
                              .lfe = static_cast<f32>(lfeLevel_)});
}

void Settings::AdoptQueueFrames() {
  queueFrames_ = REXCVAR_GET(bd_audio_qframes);
}

void Settings::AdoptCueMonitor() { cueMonitor_ = REXCVAR_GET(bd_audio_debug); }

void Settings::AdoptPeakMeter() { peakMeter_ = REXCVAR_GET(bd_audio_peak); }

void Settings::AdoptLog() { log_ = REXCVAR_GET(bd_audio_log); }

void Settings::AdoptOutputLimit() { outputLimit_ = REXCVAR_GET(bd_audio_limit); }

// Set: hand the value to the cvar layer and let the callback adopt it back.
// Going through SetFlagByName rather than REXCVAR_SET keeps the range check,
// the restart-pending bookkeeping and any callback another subsystem
// registered on this setting. It cannot recurse, because the callback adopts
// and never calls a setter. False means the cvar layer rejected the value and
// nothing changed.
bool Settings::SetGain(f64 v) {
  return rex::cvar::SetFlagByName("bd_audio_gain", FormatCvar(v));
}

bool Settings::SetCenterLevel(f64 v) {
  return rex::cvar::SetFlagByName("bd_audio_center", FormatCvar(v));
}

bool Settings::SetSurroundLevel(f64 v) {
  return rex::cvar::SetFlagByName("bd_audio_surround", FormatCvar(v));
}

bool Settings::SetLFELevel(f64 v) {
  return rex::cvar::SetFlagByName("bd_audio_lfe", FormatCvar(v));
}

bool Settings::SetCueMonitor(i32 v) {
  return rex::cvar::SetFlagByName("bd_audio_debug", FormatCvar(v));
}

bool Settings::SetPeakMeter(i32 v) {
  return rex::cvar::SetFlagByName("bd_audio_peak", FormatCvar(v));
}

bool Settings::SetLog(bool v) {
  return rex::cvar::SetFlagByName("bd_audio_log", FormatCvar(v));
}

bool Settings::SetOutputLimit(f64 v) {
  return rex::cvar::SetFlagByName("bd_audio_limit", FormatCvar(v));
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
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Settings::Get().*adopt)();
        });
  };
  reg("bd_audio_gain", &Settings::AdoptGain);
  reg("bd_audio_center", &Settings::AdoptSurroundMix);
  reg("bd_audio_surround", &Settings::AdoptSurroundMix);
  reg("bd_audio_lfe", &Settings::AdoptSurroundMix);
  reg("bd_audio_qframes", &Settings::AdoptQueueFrames);
  reg("bd_audio_debug", &Settings::AdoptCueMonitor);
  reg("bd_audio_peak", &Settings::AdoptPeakMeter);
  reg("bd_audio_log", &Settings::AdoptLog);
  reg("bd_audio_limit", &Settings::AdoptOutputLimit);
}

} // namespace bd::audio
