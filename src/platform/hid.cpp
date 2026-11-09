/**
 * @file    platform/hid.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/hid.h"

#include <atomic>

namespace bd::platform {
namespace {

std::atomic<rex::hid::IInputSystem *> g_hid{nullptr};
std::atomic<bool> g_game_cursor{false};
std::atomic<bool> g_look{false};

void ApplyPointerMode() {
  rex::PointerMode mode = rex::PointerMode::kFree;
  if (g_look.load(std::memory_order_relaxed))
    mode = rex::PointerMode::kLocked;
  else if (g_game_cursor.load(std::memory_order_relaxed))
    mode = rex::PointerMode::kHidden;
  HID().SetPointerMode(mode);
}

} // namespace

rex::hid::IInputSystem &HID() { return *g_hid.load(std::memory_order_acquire); }

void SetInput(rex::hid::IInputSystem *input) {
  g_hid.store(input, std::memory_order_release);
}

void SetGameCursorActive(bool active) {
  if (g_game_cursor.exchange(active, std::memory_order_relaxed) != active)
    ApplyPointerMode();
}

void SetLookActive(bool active) {
  if (g_look.exchange(active, std::memory_order_relaxed) != active)
    ApplyPointerMode();
}

} // namespace bd::platform
