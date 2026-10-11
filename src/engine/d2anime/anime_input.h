/**
 * @file    engine/d2anime/anime_input.h
 * @brief       Pad polling for host-driven d2anime menus.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

enum class Button : int {
  Up = 0,
  Down = 1,
  Left = 2,
  Right = 3,
  Start = 4,
  Back = 5,
  A = 8,
  B = 9,
  X = 10,
  Y = 11,
  LT = 12,
  RT = 13,
  LB = 14,
  RB = 15,
  LSUp = 16,
  LSDown = 17,
  LSRight = 18,
  LSLeft = 19,
};

// Stick axes, in the order the pad state stores them.
enum class StickAxis : int {
  LeftX = 0,
  LeftY = 1,
  RightX = 2,
  RightY = 3,
};

// Pressed this frame, subject to the engine's own repeat delay.
bool CheckButton(Button btn);

// Held right now, which CheckButton's repeat gate hides.
bool ButtonHeld(Button btn);

// Stick deflection in [-1, 1].
float StickValue(StickAxis axis);

bool CheckAction(Action action);
bool ActionHeld(Action action);
Button ActionButton(Action action);

} // namespace bd::engine
