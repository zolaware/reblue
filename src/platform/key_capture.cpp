/**
 * @file    platform/key_capture.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/key_capture.h"

#include <chrono>

#include <rex/hid/virtual_key.h>

#include "platform/hid.h"

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

struct MouseName {
  VK key;
  rex::hid::MouseButton button;
};

constexpr MouseName kMouseButtons[] = {
    {VK::kLButton, rex::hid::MouseButton::kLeft},
    {VK::kRButton, rex::hid::MouseButton::kRight},
    {VK::kMButton, rex::hid::MouseButton::kMiddle},
};

bool KeyDown(size_t i) {
  const VK key = kBindableKeys[i];
  for (const MouseName &mb : kMouseButtons)
    if (key == mb.key)
      return HID().IsButtonDown(mb.button);
  return HID().IsKeyDown(key);
}

bool SlotDown(size_t i) {
  if (i < kBindableKeyCount)
    return KeyDown(i);
  return (s_padButtons & (1u << (i - kBindableKeyCount))) != 0;
}

std::string ModifierPrefix() {
  const u8 mods = static_cast<u8>(HID().GetModifiers());
  std::string prefix;
  if (mods & kModCtrl)
    prefix += "Ctrl+";
  if (mods & kModAlt)
    prefix += "Alt+";
  if (mods & kModShift)
    prefix += "Shift+";
  return prefix;
}

} // namespace

void SetCapturePadButtons(u32 buttons) { s_padButtons = buttons; }

void BeginKeyCapture() {
  for (size_t i = 0; i < kCaptureSlotCount; ++i)
    s_prev_down[i] = SlotDown(i);
  s_wheelSeen = HID().GetWheel();
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
    if (hit < kBindableKeyCount && kBindableKeys[hit] == VK::kEscape &&
        std::chrono::steady_clock::now() - s_pendingSince >= kCancelHold) {
      s_canceled = true;
      return {};
    }
    if (hit < kBindableKeyCount)
      return s_pendingPrefix + rex::hid::VirtualKeyToString(kBindableKeys[hit]);
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

  const int wheel = HID().GetWheel();
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
