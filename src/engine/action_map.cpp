/**
 * @file    engine/action_map.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/action_map.h"

#include "core/memory_helpers.h"
#include "engine/config.h"
#include "engine/game_options.h"
#include "engine/input/button_map.h"

namespace bd::engine {

namespace addr {
// bdGetCurrentMapPath's own arithmetic: the per-type rows, sixty bytes apart,
// and the row it substitutes when the player is on type A and the alternate
// map flag is set.
inline constexpr u32 kTypeRows = 0x8205D798;
inline constexpr u32 kDefaultRow = 0x8205D75C;
} // namespace addr

namespace {

constexpr u32 kRowStride = 60;

// Beyond the last row the disc ships. A value outside this would index past the
// table into whatever follows it.
constexpr u32 kControlTypeCount = 4;

} // namespace

const char *ToString(GameAction action) {
  switch (action) {
  case GameAction::Confirm:
    return "confirm";
  case GameAction::Cancel:
    return "cancel";
  case GameAction::CampMenu:
    return "camp_menu";
  case GameAction::FieldMenu:
    return "field_menu";
  case GameAction::Examine:
    return "examine";
  case GameAction::Encounter:
    return "encounter";
  case GameAction::WorldMap:
    return "world_map";
  case GameAction::FieldSkill1:
    return "field_skill_1";
  case GameAction::FieldSkill2:
    return "field_skill_2";
  case GameAction::CenterCamera:
    return "center_camera";
  case GameAction::DpadUp:
    return "dpad_up";
  case GameAction::DpadDown:
    return "dpad_down";
  case GameAction::DpadLeft:
    return "dpad_left";
  case GameAction::DpadRight:
    return "dpad_right";
  case GameAction::RightStickAsDirection:
    return "right_stick_direction";
  }
  return "?";
}

ActionMap &ActionMap::Get() {
  static ActionMap s;
  return s;
}

u32 ActionMap::Row() const {
  if (const u32 derived = ButtonMap::Get().GeneralRowVA())
    return derived;
  const auto type = static_cast<u32>(GameOptions::Get().CtlNormalType());
  if (type >= kControlTypeCount)
    return 0;
  if (type == 0 && Config::Get().AltMap() != 0)
    return addr::kDefaultRow;
  return addr::kTypeRows + type * kRowStride;
}

int ActionMap::Button(GameAction action) const {
  const int slot = static_cast<int>(action);
  if (slot < 0 || slot >= kGameActionCount ||
      action == GameAction::RightStickAsDirection)
    return -1;
  const u32 row = Row();
  if (!row)
    return -1;
  return static_cast<int>(mem::try_load<u32>(row + u32(slot) * sizeof(u32)));
}

std::optional<GameAction> ActionMap::ActionFor(int padButton) const {
  if (padButton < 0)
    return std::nullopt;
  for (int slot = 0; slot < kGameActionCount; ++slot) {
    const auto action = static_cast<GameAction>(slot);
    if (action == GameAction::RightStickAsDirection)
      continue;
    if (Button(action) == padButton)
      return action;
  }
  return std::nullopt;
}

} // namespace bd::engine
