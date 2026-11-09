/**
 * @file    engine/game.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/game.h"

#include <rex/runtime.h>

#include "core/guest_global.h"
#include "core/memory_helpers.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

constexpr u32 kShutdownFieldActive = 0;

} // namespace

Game &Game::Get() {
  static Game instance;
  return instance;
}

const char *ToString(EngineMode mode) {
  switch (mode) {
  case EngineMode::TitleOrMenu:
    return "TitleOrMenu";
  case EngineMode::FieldActive:
    return "FieldActive";
  case EngineMode::FieldTransition:
    return "FieldTransition";
  case EngineMode::Battle:
    return "Battle";
  case EngineMode::Loading:
    return "Loading";
  case EngineMode::Unknown:
  default:
    return "Unknown";
  }
}

bool Game::IsReady() const {
  const rex::Runtime *runtime = rex::GetProcessRuntime();
  return runtime && runtime->IsTitleLaunched();
}

bool Game::FieldGameplayActive() const {
  const engine::GameTask task = GameTask();
  return task && task.ShutdownFlag() == kShutdownFieldActive;
}

bool Game::IsLoading() const { return Loader().AnySlotLoading(); }

bool Game::LoadingScreenUp() const { return Loader().NowLoading(); }

bool Game::MindowsPanelActive() const {
  return bd::mem::global_load(addr::kMindowsActivePanel) != 0;
}

bool Game::MindowsHidden() const {
  return bd::mem::global_load(addr::kMindowsHidden) != 0;
}

void Game::SetMindowsHidden(bool hidden) {
  bd::mem::global_store(addr::kMindowsHidden, hidden ? 1u : 0u);
}

engine::ScriptManTask Game::ScriptManTask() const {
  return engine::ScriptManTask(bd::mem::global_load(addr::kScriptManTask));
}

engine::FieldPlayerEntity Game::FieldPlayerEntity() const {
  return engine::FieldPlayerEntity(
      bd::mem::global_load(addr::kFieldPlayerEntity));
}

engine::GameTask Game::GameTask() const {
  return engine::GameTask(bd::mem::global_load(addr::kGameTask));
}

engine::Loader Game::Loader() const {
  return engine::Loader(bd::mem::global_load(addr::kLoaderTask));
}

engine::SequenceControl Game::SequenceControl() const {
  return engine::SequenceControl(
      bd::mem::global_load(addr::kSequenceControl));
}

engine::ItemSaveData Game::ItemSaveData() const {
  return engine::ItemSaveData(bd::mem::global_load(addr::kItemSaveData));
}

engine::BattleCameraTask Game::BattleCameraTask() const {
  return engine::BattleCameraTask(
      bd::mem::global_load(addr::kBattleCameraCtl));
}

engine::IssEvent Game::IssEvent() const { return engine::IssEvent::LiveAt(0); }

engine::PlayRecord Game::PlayRecord() const {
  return engine::PlayRecord(bd::mem::global_load(addr::kPlayRecord));
}

EngineMode Game::Mode() const {
  if (!IsReady())
    return EngineMode::Unknown;
  if (BattleCameraTask())
    return EngineMode::Battle;
  if (IsLoading() || LoadingScreenUp())
    return EngineMode::Loading;
  if (FieldGameplayActive())
    return ScriptManTask().SceneState() == kSceneStateStageTransition
               ? EngineMode::FieldTransition
               : EngineMode::FieldActive;
  return EngineMode::TitleOrMenu;
}

} // namespace bd::engine
