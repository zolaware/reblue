/**
 * @file    engine/game_options.h
 * @brief   The stock game options: the values BD kept in the save block, read
 *          and written as the engine globals the engine actually consults.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// True once the engine address space exists. Every accessor below reads as zero
// before that, and reads the engine's defaults between then and
// bdGameConfigInit.
bool GameOptionsResolved();

class GameOptions {
public:
  static GameOptions &Get();

  // Once at startup, after rex::cvar::LoadConfig has run.
  void Init();

  // Lays the global set over the engine globals, the mixer buses and the
  // renderer's copies.
  void Apply();

  // Overwrites the config range of the save block the engine is about to read.
  // Called from the bdSaveBlockRestoreConfig hook, which then lets the original
  // run so the engine applies its own mirrors.
  void WriteBlock();

  // The load screen has a voice picker of its own, and it writes the engine
  // global after the restore. Called from the same hook once the original has
  // run, so the global set takes that choice instead of pushing the previous
  // one back the next time an option changes.
  void AdoptVoiceType();

  // Persists the global set.
  void Flush();

  i32 MsgSpeed() const;
  bool SetMsgSpeed(i32 v);
  i32 MsgSize() const;
  bool SetMsgSize(i32 v);
  i32 VoiceType() const;
  bool SetVoiceType(i32 v);
  i32 Ruby() const;
  bool SetRuby(i32 v);
  i32 Subtitles() const;
  bool SetSubtitles(i32 v);
  i32 AudioHints() const;
  bool SetAudioHints(i32 v);
  i32 BattleHints() const;
  bool SetBattleHints(i32 v);
  i32 SkipEvents() const;
  bool SetSkipEvents(i32 v);
  i32 Camera() const;
  bool SetCamera(i32 v);
  i32 TargetFirst() const;
  bool SetTargetFirst(i32 v);

  f64 MusicVolume() const;
  bool SetMusicVolume(f64 v);
  // The stock Sound Effects row steps the Default bus and the Voice bus by the
  // same amount, so one setter drives both.
  f64 SeVolume() const;
  bool SetSeVolume(f64 v);
  f64 Brightness() const;
  bool SetBrightness(f64 v);
  f64 ScreenPosX() const;
  bool SetScreenPosX(f64 v);
  f64 ScreenPosY() const;
  bool SetScreenPosY(f64 v);

private:
  GameOptions() = default;
  GameOptions(const GameOptions &) = delete;
  GameOptions &operator=(const GameOptions &) = delete;

  bool dirty_ = false;
};

} // namespace bd::engine
