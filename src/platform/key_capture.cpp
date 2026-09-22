/**
 * @file    platform/key_capture.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/key_capture.h"

#include <chrono>
#include <string_view>

#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "platform/keyboard_input.h"
#include "platform/mouse_input.h"

namespace bd::platform {
namespace {

constexpr const char *kPadTokens[] = {
    "PadDUp",   "PadDDown",   "PadDLeft",    "PadDRight",
    "PadStart", "PadBack",    "PadLS",       "PadRS",
    "PadA",     "PadB",       "PadX",        "PadY",
    "PadLT",    "PadRT",      "PadLB",       "PadRB",
    "LStickUp", "LStickDown", "LStickRight", "LStickLeft",
    "RStickUp", "RStickDown", "RStickRight", "RStickLeft",
};
constexpr size_t kPadTokenCount = sizeof(kPadTokens) / sizeof(kPadTokens[0]);
constexpr size_t kCaptureSlotCount = kBindableKeyCount + kPadTokenCount;
constexpr size_t kPadLTSlot = kBindableKeyCount + 12;
constexpr size_t kPadRTSlot = kBindableKeyCount + 13;

constexpr auto kCancelHold = std::chrono::milliseconds(800);

bool s_prev_down[kCaptureSlotCount] = {};
bool s_capturing = false;
// The hit waiting for its key to come back up, with the modifier prefix taken
// at the press, since the modifier may be released first.
int s_pending = -1;
std::string s_pendingPrefix;
std::chrono::steady_clock::time_point s_pendingSince;
u32 s_padButtons = 0;
int s_wheelSeen = 0;
bool s_canceled = false;

// The three mouse names sit in the same bindable list as the keys, and
// ParseVirtualKey even resolves them, but a mouse button never reaches the
// keyboard tracker: that is fed by key events alone. Polled against the
// keyboard they read as permanently up, so a mouse button could be typed into
// a config file and never captured from the bind screen.
struct MouseName {
  const char *name;
  rex::ui::MouseEvent::Button button;
};

constexpr MouseName kMouseButtons[] = {
    {"LMB", rex::ui::MouseEvent::Button::kLeft},
    {"RMB", rex::ui::MouseEvent::Button::kRight},
    {"MMB", rex::ui::MouseEvent::Button::kMiddle},
};

bool KeyDown(size_t i) {
  const std::string_view name = kBindableKeys[i];
  for (const MouseName &mb : kMouseButtons)
    if (name == mb.name)
      return Mouse().IsButtonDown(mb.button);

  auto vk = rex::ui::ParseVirtualKey(name);
  if (vk == rex::ui::VirtualKey::kNone)
    return false;
  return Keyboard().IsDown(vk);
}

bool SlotDown(size_t i) {
  if (i < kBindableKeyCount)
    return KeyDown(i);
  return (s_padButtons & (1u << (i - kBindableKeyCount))) != 0;
}

std::string ModifierPrefix() {
  const u8 mods = Keyboard().Modifiers();
  std::string prefix;
  if (mods & KeyboardInput::kModCtrl)
    prefix += "Ctrl+";
  if (mods & KeyboardInput::kModAlt)
    prefix += "Alt+";
  if (mods & KeyboardInput::kModShift)
    prefix += "Shift+";
  return prefix;
}

} // namespace

void SetCapturePadButtons(u32 buttons) { s_padButtons = buttons; }

void BeginKeyCapture() {
  for (size_t i = 0; i < kCaptureSlotCount; ++i)
    s_prev_down[i] = SlotDown(i);
  s_wheelSeen = Mouse().WheelDetents();
  s_capturing = true;
  s_pending = -1;
  s_canceled = false;
}

std::string PollKeyCapture() {
  if (!s_capturing)
    return {};

  if (s_pending >= 0) {
    if (SlotDown(kPadLTSlot) && SlotDown(kPadRTSlot)) {
      s_capturing = false;
      s_pending = -1;
      s_canceled = true;
      return {};
    }
    if (SlotDown(size_t(s_pending)))
      return {};
    s_capturing = false;
    const size_t hit = static_cast<size_t>(s_pending);
    s_pending = -1;
    if (hit < kBindableKeyCount &&
        std::string_view(kBindableKeys[hit]) == "Escape" &&
        std::chrono::steady_clock::now() - s_pendingSince >= kCancelHold) {
      s_canceled = true;
      return {};
    }
    if (hit < kBindableKeyCount)
      return s_pendingPrefix + kBindableKeys[hit];
    return kPadTokens[hit - kBindableKeyCount];
  }

  for (size_t i = 0; i < kCaptureSlotCount; ++i) {
    const bool down = SlotDown(i);
    if (down && !s_prev_down[i] && s_pending < 0) {
      s_pending = static_cast<int>(i);
      s_pendingSince = std::chrono::steady_clock::now();
      s_pendingPrefix =
          i < kBindableKeyCount ? ModifierPrefix() : std::string();
    }
    s_prev_down[i] = down;
  }
  if (s_pending >= 0)
    return {};

  const int wheel = Mouse().WheelDetents();
  if (wheel == 0) {
    s_wheelSeen = 0;
    return {};
  }
  if (wheel == s_wheelSeen)
    return {};
  s_capturing = false;
  return wheel > 0 ? "WheelUp" : "WheelDown";
}

bool KeyCapturePending() { return s_capturing && s_pending >= 0; }

bool KeyCaptureCanceled() { return s_canceled; }

} // namespace bd::platform
