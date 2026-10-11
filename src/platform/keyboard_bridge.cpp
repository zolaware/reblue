/**
 * @file    platform/keyboard_bridge.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/keyboard_bridge.h"

#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/virtual_key.h>

#include "core/memory_helpers.h"
#include "engine/engine.h"
#include "platform/keyboard_input.h"

namespace bd::platform {

using VK = rex::ui::VirtualKey;

inline constexpr VK kPolledKeys[] = {
    VK::kReturn,  VK::kEscape,  VK::kTab,     VK::kBack,    VK::kUp,
    VK::kDown,    VK::kLeft,    VK::kRight,   VK::kAdd,     VK::kSubtract,
    VK::kF1,      VK::kF2,      VK::kF3,      VK::kF4,      VK::kF5,
    VK::kF6,      VK::kF7,      VK::kF8,      VK::kF9,      VK::kF10,
    VK::kF11,     VK::kF12,     VK::kA,       VK::kB,       VK::kC,
    VK::kD,       VK::kE,       VK::kF,       VK::kG,       VK::kH,
    VK::kI,       VK::kJ,       VK::kK,       VK::kL,       VK::kM,
    VK::kN,       VK::kO,       VK::kP,       VK::kQ,       VK::kR,
    VK::kS,       VK::kT,       VK::kU,       VK::kV,       VK::kW,
    VK::kX,       VK::kY,       VK::kZ,       VK::k0,       VK::k1,
    VK::k2,       VK::k3,       VK::k4,       VK::k5,       VK::k6,
    VK::k7,       VK::k8,       VK::k9,       VK::kNumpad0, VK::kNumpad1,
    VK::kNumpad2, VK::kNumpad3, VK::kNumpad4, VK::kNumpad5, VK::kNumpad6,
    VK::kNumpad7, VK::kNumpad8, VK::kNumpad9, VK::kInsert,  VK::kHome,
};

inline constexpr size_t kPolledKeyCount =
    sizeof(kPolledKeys) / sizeof(kPolledKeys[0]);

namespace {

// Map VK codes to the char codes Blue Dragon's debug systems expect.
u16 VkToChar(VK vk) {
  auto v = static_cast<u16>(vk);
  switch (vk) {
  case VK::kReturn:
    return 13;
  case VK::kEscape:
    return 27;
  case VK::kTab:
    return 9;
  case VK::kBack:
    return 8;
  case VK::kLeft:
    return 37;
  case VK::kUp:
    return 38;
  case VK::kRight:
    return 39;
  case VK::kDown:
    return 40;
  case VK::kLWin:
    return 91;
  case VK::kRWin:
    return 92;
  case VK::kAdd:
    return 107;
  case VK::kSubtract:
    return 109;
  default:
    if (v >= '0' && v <= '9')
      return v;
    if (v >= 'A' && v <= 'Z')
      return v + 0x20;
    if (v >= static_cast<u16>(VK::kNumpad0) &&
        v <= static_cast<u16>(VK::kNumpad9))
      return v;
    return v;
  }
}

void ClearGuestEntry(GuestKeyEntry *entry) {
  entry->vkCode = 0;
  entry->charCode = 0;
  entry->modifiers = 0;
}

} // namespace

// Write the first newly pressed key into the guest buffer. Edge-detected so
// held keys don't repeat every frame.
void PollKeyboardToGuest() {
  auto *ks = REX_KERNEL_STATE();
  if (!ks || !ks->memory())
    return;

  auto *entry = mem::at<GuestKeyEntry>(kGuestKeyBufferAddr);
  if (!entry)
    return;

  // Guest debug handlers (bdPadLogDebugHandler and friends) poll this buffer
  // whenever debugMindows is set, regardless of debugInputKey. Forward keys
  // only while the overlay is on-screen so gameplay keystrokes can't trip
  // debug toggles.
  const auto &game = bd::engine::Game::Get();
  const bool overlay_visible = game.IsReady() && !game.MindowsHidden();

  auto &kb = Keyboard();
  static bool prev_down[kPolledKeyCount] = {};

  VK found_vk = VK::kNone;
  for (size_t i = 0; i < kPolledKeyCount; ++i) {
    const bool now_down = kb.IsDown(kPolledKeys[i]);
    if (now_down && !prev_down[i] && found_vk == VK::kNone)
      found_vk = kPolledKeys[i];
    prev_down[i] = now_down;
  }

  if (found_vk != VK::kNone && overlay_visible) {
    entry->vkCode = static_cast<u16>(found_vk);
    entry->charCode = VkToChar(found_vk);
    entry->modifiers = kb.Modifiers();
  } else {
    // Clear rather than leave a stale key: guest handlers re-fire every frame
    // the buffer holds a value, not on edges.
    ClearGuestEntry(entry);
  }
}

bool PollMindowsHotkey() {
  auto &kb = Keyboard();
  constexpr u8 kCtrlAlt = KeyboardInput::kModCtrl | KeyboardInput::kModAlt;
  const bool down =
      (kb.Modifiers() & kCtrlAlt) == kCtrlAlt && kb.IsDown(VK::kM);
  static bool prev = false;
  const bool fired = down && !prev;
  prev = down;
  return fired;
}

} // namespace bd::platform
