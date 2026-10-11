/**
 * @file    audio/settings.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::audio {

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

  f64 Gain() const { return gain_; }
  bool SetGain(f64 v);

  // Level of a 5.1 slot against the front pair, which is the reference. BD
  // emits music in the front pair and copies one effects and ambience bus into
  // the other four slots, so these are what balances effects against music.
  // Only a device the output stage passes six channels through to sees them.
  f64 CenterLevel() const { return centerLevel_; }
  bool SetCenterLevel(f64 v);

  f64 SurroundLevel() const { return surroundLevel_; }
  bool SetSurroundLevel(f64 v);

  f64 LFELevel() const { return lfeLevel_; }
  bool SetLFELevel(f64 v);

  // Audio pacing queue depth in 256-sample frames. Restart-bound.
  i32 QueueFrames() const { return queueFrames_; }

  // Engine cue monitor overlay: 0 off, 1 all cue slots, 2 active cues only.
  i32 CueMonitor() const { return cueMonitor_; }
  bool SetCueMonitor(i32 v);

  // Engine output peak meter overlay: 0 off, 1 meters, 2 meters with dB.
  i32 PeakMeter() const { return peakMeter_; }
  bool SetPeakMeter(i32 v);

  bool Log() const { return log_; }
  bool SetLog(bool v);

  f64 OutputLimit() const { return outputLimit_; }
  bool SetOutputLimit(f64 v);

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the others.
  void AdoptGain();
  void AdoptSurroundMix();
  void AdoptQueueFrames();
  void AdoptCueMonitor();
  void AdoptPeakMeter();
  void AdoptLog();
  void AdoptOutputLimit();

  f64 gain_ = 1.0;
  f64 centerLevel_ = 1.0;
  f64 surroundLevel_ = 1.0;
  f64 lfeLevel_ = 0.0;
  i32 queueFrames_ = 16;
  i32 cueMonitor_ = 0;
  i32 peakMeter_ = 0;
  bool log_ = false;
  f64 outputLimit_ = 8.0;
};

} // namespace bd::audio
