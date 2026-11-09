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

std::atomic<rex::hid::IHIDBackend *> g_hid{nullptr};
std::atomic<bool> g_game_cursor{false};
std::atomic<bool> g_look{false};

void ApplyPointerMode() {
  rex::hid::PointerMode mode = rex::hid::PointerMode::kFree;
  if (g_look.load(std::memory_order_relaxed))
    mode = rex::hid::PointerMode::kLocked;
  else if (g_game_cursor.load(std::memory_order_relaxed))
    mode = rex::hid::PointerMode::kHidden;
  HID().SetPointerMode(mode);
}

} // namespace

rex::hid::IHIDBackend &HID() { return *g_hid.load(std::memory_order_acquire); }

void SetHID(rex::hid::IHIDBackend *backend) {
  g_hid.store(backend, std::memory_order_release);
}

void SetGameCursorActive(bool active) {
  if (g_game_cursor.exchange(active, std::memory_order_relaxed) != active)
    ApplyPointerMode();
}

void SetLookActive(bool active) {
  if (g_look.exchange(active, std::memory_order_relaxed) != active)
    ApplyPointerMode();
}

rex::hid::MouseButton ToHIDButton(rex::ui::MouseEvent::Button button) {
  switch (button) {
  case rex::ui::MouseEvent::Button::kRight:
    return rex::hid::MouseButton::kRight;
  case rex::ui::MouseEvent::Button::kMiddle:
    return rex::hid::MouseButton::kMiddle;
  case rex::ui::MouseEvent::Button::kX1:
    return rex::hid::MouseButton::kX1;
  case rex::ui::MouseEvent::Button::kX2:
    return rex::hid::MouseButton::kX2;
  default:
    return rex::hid::MouseButton::kLeft;
  }
}

} // namespace bd::platform
