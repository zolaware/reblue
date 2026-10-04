/**
 * @file    engine/d2anime/anime_input.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_input.h"
#include "core/memory_helpers.h"
#include "engine/input/button_map.h"
#include "reblue_init.h"
#include <rex/types.h>

REX_IMPORT(__imp__bdInputCheckButton, InputCheckButton, u32(u32, u32, u32));
// The trailing argument is the caller's priority, which every in-game reader
// passes as zero.
REX_IMPORT(__imp__bdInputIsPressed, InputIsPressed, u32(u32, u32, u32, u32));
REX_IMPORT(__imp__bdInputGetAnalogValue, InputGetAnalogValue,
           f64(u32, u32, u32, u32));

namespace bd::engine {

namespace addr {
inline constexpr u32 kInputManager = 0x82DC9844;
} // namespace addr

bool CheckButton(Button btn) {
  u32 inputMgr = bd::mem::load<u32>(addr::kInputManager);
  return inputMgr && InputCheckButton(inputMgr, 0, static_cast<u32>(btn)) != 0;
}

bool ButtonHeld(Button btn) {
  u32 inputMgr = bd::mem::load<u32>(addr::kInputManager);
  return inputMgr &&
         InputIsPressed(inputMgr, 0, static_cast<u32>(btn), 0) != 0;
}

Button ActionButton(Action action) {
  const int btn = ButtonMap::Get().Id(action);
  if (btn >= 0)
    return static_cast<Button>(btn);
  return action == Action::Cancel ? Button::B : Button::A;
}

bool CheckAction(Action action) {
  const int btn = ButtonMap::Get().Id(action);
  return btn >= 0 && CheckButton(static_cast<Button>(btn));
}

bool ActionHeld(Action action) {
  const int btn = ButtonMap::Get().Id(action);
  return btn >= 0 && ButtonHeld(static_cast<Button>(btn));
}

float StickValue(StickAxis axis) {
  u32 inputMgr = bd::mem::load<u32>(addr::kInputManager);
  if (!inputMgr)
    return 0.0f;
  return static_cast<float>(
      InputGetAnalogValue(inputMgr, 0, static_cast<u32>(axis), 0));
}

} // namespace bd::engine
