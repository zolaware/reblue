/**
 * @file    platform/keyboard_bridge.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/keyboard_bridge.h"

#include <atomic>

#include <rex/kernel/title_runtime.h>
#include <rex/input_keys.h>

#include "core/memory_helpers.h"
#include "engine/engine.h"
#include "platform/hid.h"

namespace bd::platform {

using VK = rex::VirtualKey;

inline constexpr VK kPolledKeys[] = {
    VK::kReturn,   VK::kEscape,    VK::kTab,      VK::kBack,
    VK::kSpace,    VK::kUp,        VK::kDown,     VK::kLeft,
    VK::kRight,    VK::kPrior,     VK::kNext,     VK::kHome,
    VK::kEnd,      VK::kInsert,    VK::kDelete,   VK::kAdd,
    VK::kSubtract, VK::kMultiply,  VK::kDivide,   VK::kDecimal,
    VK::kSeparator, VK::kF1,       VK::kF2,       VK::kF3,
    VK::kF4,       VK::kF5,        VK::kF6,       VK::kF7,
    VK::kF8,       VK::kF9,        VK::kF10,      VK::kF11,
    VK::kF12,      VK::kA,         VK::kB,        VK::kC,
    VK::kD,        VK::kE,         VK::kF,        VK::kG,
    VK::kH,        VK::kI,         VK::kJ,        VK::kK,
    VK::kL,        VK::kM,         VK::kN,        VK::kO,
    VK::kP,        VK::kQ,         VK::kR,        VK::kS,
    VK::kT,        VK::kU,         VK::kV,        VK::kW,
    VK::kX,        VK::kY,         VK::kZ,        VK::k0,
    VK::k1,        VK::k2,         VK::k3,        VK::k4,
    VK::k5,        VK::k6,         VK::k7,        VK::k8,
    VK::k9,        VK::kNumpad0,   VK::kNumpad1,  VK::kNumpad2,
    VK::kNumpad3,  VK::kNumpad4,   VK::kNumpad5,  VK::kNumpad6,
    VK::kNumpad7,  VK::kNumpad8,   VK::kNumpad9,  VK::kScroll,
    VK::kLWin,     VK::kRWin,
};

inline constexpr size_t kPolledKeyCount =
    sizeof(kPolledKeys) / sizeof(kPolledKeys[0]);

namespace {

using Mods = rex::KeyModifiers;

constexpr rex::InputSource Key(VK key, Mods mods = Mods::kNone) {
  return {.type = rex::InputSourceType::kKey,
          .code = static_cast<u16>(key),
          .modifiers = mods};
}

constexpr rex::InputSource kMindowsKeys[] = {
    Key(VK::kTab),
    Key(VK::kTab, Mods::kShift),
    Key(VK::kUp),
    Key(VK::kDown),
    Key(VK::kLeft),
    Key(VK::kRight),
    Key(VK::kUp, Mods::kCtrl),
    Key(VK::kDown, Mods::kCtrl),
    Key(VK::kLeft, Mods::kCtrl),
    Key(VK::kRight, Mods::kCtrl),
    Key(VK::kUp, Mods::kShift),
    Key(VK::kDown, Mods::kShift),
    Key(VK::kLeft, Mods::kShift),
    Key(VK::kRight, Mods::kShift),
    Key(VK::kUp, Mods::kAlt),
    Key(VK::kDown, Mods::kAlt),
    Key(VK::kLeft, Mods::kAlt),
    Key(VK::kRight, Mods::kAlt),
    Key(VK::kAdd, Mods::kCtrl),
    Key(VK::kSubtract, Mods::kCtrl),
    Key(VK::kPrior),
    Key(VK::kNext),
    Key(VK::kHome),
    Key(VK::kEnd),
    Key(VK::kReturn),
    Key(VK::kSeparator),
    Key(VK::kEscape),
    Key(VK::kBack),
    Key(VK::kNumpad0),
    Key(VK::kNumpad1),
    Key(VK::kNumpad2),
    Key(VK::kNumpad3),
    Key(VK::kNumpad4),
    Key(VK::kNumpad5),
    Key(VK::kNumpad6),
    Key(VK::kNumpad7),
    Key(VK::kNumpad8),
    Key(VK::kNumpad9),
    Key(VK::kSubtract),
    Key(VK::kDecimal),
    Key(VK::kLWin),
    Key(VK::kRWin),
    Key(VK::kKanji),
};

std::atomic<bool> g_mindowsVisible{false};

struct QueuedKey {
  VK vk;
  u8 modifiers;
};

constexpr size_t kKeyQueueSize = 8;

struct KeyQueue {
  QueuedKey keys[kKeyQueueSize] = {};
  size_t head = 0;
  size_t count = 0;

  void Push(const QueuedKey &key) {
    if (count == kKeyQueueSize)
      return;
    keys[(head + count) % kKeyQueueSize] = key;
    ++count;
  }

  QueuedKey Pop() {
    const QueuedKey key = keys[head];
    head = (head + 1) % kKeyQueueSize;
    --count;
    return key;
  }

  void Clear() { head = count = 0; }
};

KeyQueue g_keyQueue;

void ClearGuestEntry(GuestKeyEntry *entry) {
  entry->vkCode = 0;
  entry->charCode = 0;
  entry->modifiers = 0;
}

} // namespace

// Write the next queued key press into the guest buffer. Edge-detected so
// held keys don't repeat every frame.
void PollKeyboardToGuest() {
  if (!bd::mem::memory())
    return;

  const auto view = bd::mem::view();
  auto *entry = view ? kGuestKeyBuffer.TryGetHost(*view) : nullptr;
  if (!entry)
    return;

  // Guest debug handlers (bdPadLogDebugHandler and friends) poll this buffer
  // whenever debugMindows is set, regardless of debugInputKey. Forward keys
  // only while the overlay is on-screen so gameplay keystrokes can't trip
  // debug toggles.
  const auto &game = bd::engine::Game::Get();
  const bool overlay_visible = game.IsReady() && !game.MindowsHidden();

  auto &kb = HID();
  static bool prev_down[kPolledKeyCount] = {};

  const u8 modifiers = static_cast<u8>(kb.GetModifiers());
  for (size_t i = 0; i < kPolledKeyCount; ++i) {
    const bool now_down = kb.IsKeyDown(kPolledKeys[i]);
    if (now_down && !prev_down[i] && overlay_visible)
      g_keyQueue.Push({kPolledKeys[i], modifiers});
    prev_down[i] = now_down;
  }
  if (!overlay_visible)
    g_keyQueue.Clear();

  const bool publish = bd::engine::TickDue() ||
                       bd::engine::CutscenePause::Get().Frozen();
  if (publish && g_keyQueue.count != 0) {
    const QueuedKey key = g_keyQueue.Pop();
    entry->vkCode = static_cast<u16>(key.vk);
    entry->charCode = static_cast<u16>(key.vk);
    entry->modifiers = key.modifiers;
  } else {
    // Clear rather than leave a stale key: guest handlers re-fire every frame
    // the buffer holds a value, not on edges.
    ClearGuestEntry(entry);
  }
}

void SetMindowsVisible(bool visible) {
  g_mindowsVisible.store(visible, std::memory_order_relaxed);
}

std::span<const rex::InputSource> MindowsKeys() {
  if (!g_mindowsVisible.load(std::memory_order_relaxed))
    return {};
  return kMindowsKeys;
}

} // namespace bd::platform
