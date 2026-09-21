/**
 * @file    engine/game.h
 * @brief   Process-wide facade over the engine state objects, plus the coarse
 *          engine mode roll-up.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include "engine/battle_camera_task.h"
#include "engine/battle_task.h"
#include "engine/field_player_entity.h"
#include "engine/game_task.h"
#include "engine/iss_event.h"
#include "engine/item_save_data.h"
#include "engine/language.h"
#include "engine/loader.h"
#include "engine/play_record.h"
#include "engine/script_man_task.h"
#include "engine/sequence_control.h"
#include "engine/sofdec_player.h"

namespace bd::engine {

// Blue Dragon has no single mode integer. This is derived from which task
// singletons are live.
enum class EngineMode : u8 {
  Unknown,
  TitleOrMenu,
  FieldActive,
  FieldTransition,
  Battle,
  Loading,
};

const char *ToString(EngineMode mode);

class Game {
public:
  static Game &Get();

  // Engine memory is mapped. Every accessor below returns defaults until it is.
  bool IsReady() const;

  EngineMode Mode() const;

  // Predicates that read more than one root.
  bool FieldGameplayActive() const; // GameTask && !shutdownFlag
  bool IsLoading() const;           // any Loader slot mid-load
  bool LoadingScreenUp() const;     // the now-loading wheel is up
  bool MindowsPanelActive() const;  // Mindows panel focused, NOT the camp menu

  // The overlay is up but suppressed, so nothing of it is drawn and the
  // keyboard belongs to the game again.
  bool MindowsHidden() const;
  void SetMindowsHidden(bool hidden);
  void ToggleMindows();

  engine::ScriptManTask ScriptManTask() const;
  engine::FieldPlayerEntity FieldPlayerEntity() const;
  engine::GameTask GameTask() const;
  engine::Loader Loader() const;
  engine::SequenceControl SequenceControl() const;
  engine::ItemSaveData ItemSaveData() const;
  engine::BattleCameraTask BattleCameraTask() const;
  engine::PlayRecord PlayRecord() const;

  // Empty until bdBattleSceneUpdate has run this step: the manager has no root
  // global and is only ever passed as 'this'.
  engine::BattleTask BattleTask() const;

  engine::IssEvent IssEvent() const;
  engine::SofdecPlayer SofdecPlayer() const;
  engine::Language Language() const { return {}; }

private:
  Game() = default;
};

} // namespace bd::engine
