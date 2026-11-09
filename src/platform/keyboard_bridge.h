/**
 * @file    platform/keyboard_bridge.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <cstddef>

#include <rex/kernel/memory_view.h>
#include <rex/types.h>

namespace bd::platform {

struct GuestKeyEntry {
  be_u16 charCode; // +0x00
  u16 _pad02;      // +0x02
  be_u16 vkCode;   // +0x04
  u16 _pad06;      // +0x06
  u8 modifiers;    // +0x08
};
static_assert(offsetof(GuestKeyEntry, charCode) == 0x00);
static_assert(offsetof(GuestKeyEntry, vkCode) == 0x04);
static_assert(offsetof(GuestKeyEntry, modifiers) == 0x08);

REX_GUEST_GLOBAL(GuestKeyEntry, kGuestKeyBuffer, 0x82DDA6F0);

// Per-frame keyboard bridge: polls host keyboard into the guest key buffer.
// Keys are only forwarded while the Mindows overlay is on-screen.
// Called from a midasm hook at the top of bdMainGameStep.
void PollKeyboardToGuest();

// Edge-detected Ctrl+Alt+M. Detected here because the SDK bind registry is
// single-key. The chord avoids colliding with guest debug F-key handlers.
bool PollMindowsHotkey();

// While Mindows is open its keys must not also drive the keyboard pad, so the
// title reads idle pads until it closes.
void HoldPadsForMindows(bool open);

// True while Mindows holds the keyboard. Key binds then drive nothing but the
// Mindows toggle.
bool MindowsOwnsKeyboard();

} // namespace bd::platform
