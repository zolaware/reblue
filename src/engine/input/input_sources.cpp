#include "engine/input/input_sources.h"

#include <algorithm>

#include <rex/hid/pad.h>
#include <rex/input_keys.h>

#include "core/cvar_table.h"
#include "core/memory_helpers.h"
#include "engine/frame_clock.h"
#include "engine/input/actions.h"
#include "engine/input/pad_block.h"
#include "engine/virtual_buttons.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

constexpr size_t kMaxPendingTaps = 32;

constexpr u32 kPadButtonBits[24] = {
    1u << 0,   1u << 1,   1u << 2,   1u << 3,  1u << 4,  1u << 5,
    1u << 6,   1u << 7,   1u << 8,   1u << 9,  1u << 10, 1u << 11,
    0x4000u,   0x8000u,   0x2000u,   0x1000u,
    0x10000u,  0x20000u,  0x40000u,  0x80000u,
    0x100000u, 0x200000u, 0x400000u, 0x800000u,
};

u32 PadButtonBit(u16 id) {
  constexpr u16 kCount = sizeof(kPadButtonBits) / sizeof(kPadButtonBits[0]);
  return id < kCount ? kPadButtonBits[id] : 0u;
}

constexpr u32 kPadUser = 0;

struct PadPress {
  u16 button;
  rex::PadInput input;
};

constexpr PadPress kPadPresses[] = {
    {rex::hid::kPadDPadUp, rex::PadInput::kDpadUp},
    {rex::hid::kPadDPadDown, rex::PadInput::kDpadDown},
    {rex::hid::kPadDPadLeft, rex::PadInput::kDpadLeft},
    {rex::hid::kPadDPadRight, rex::PadInput::kDpadRight},
    {rex::hid::kPadStart, rex::PadInput::kStart},
    {rex::hid::kPadBack, rex::PadInput::kBack},
    {rex::hid::kPadLeftThumb, rex::PadInput::kLeftStickPress},
    {rex::hid::kPadRightThumb, rex::PadInput::kRightStickPress},
    {rex::hid::kPadLeftShoulder, rex::PadInput::kLeftShoulder},
    {rex::hid::kPadRightShoulder, rex::PadInput::kRightShoulder},
    {rex::hid::kPadA, rex::PadInput::kA},
    {rex::hid::kPadB, rex::PadInput::kB},
    {rex::hid::kPadX, rex::PadInput::kX},
    {rex::hid::kPadY, rex::PadInput::kY},
};

struct MouseKey {
  rex::VirtualKey key;
  rex::MouseButton button;
};

constexpr MouseKey kMouseKeys[] = {
    {rex::VirtualKey::kLButton, rex::MouseButton::kLeft},
    {rex::VirtualKey::kRButton, rex::MouseButton::kRight},
    {rex::VirtualKey::kMButton, rex::MouseButton::kMiddle},
};

rex::MouseButton MouseKeyButton(u16 code) {
  const auto key = static_cast<rex::VirtualKey>(code);
  for (const MouseKey &entry : kMouseKeys)
    if (entry.key == key)
      return entry.button;
  return rex::MouseButton::kNone;
}

rex::VirtualKey MouseButtonKey(rex::MouseButton button) {
  for (const MouseKey &entry : kMouseKeys)
    if (entry.button == button)
      return entry.key;
  return rex::VirtualKey::kNone;
}

u32 PadPressBits(u16 pressed) {
  u32 bits = 0;
  for (const PadPress &press : kPadPresses)
    if (pressed & press.button)
      bits |= PadButtonBit(static_cast<u16>(press.input));
  return bits;
}

bool IsMouse(const Source &source) {
  switch (source.type) {
  case SourceKind::kMouseWheel:
    return true;
  case SourceKind::kKey:
    return MouseKeyButton(source.code) != rex::MouseButton::kNone;
  default:
    return false;
  }
}

bool MouseTaken(const Source &source) {
  return IsMouse(source) && HostOverlayOwnsPointer();
}

int PadAxisBase(const Source &source) {
  return static_cast<rex::PadAxes>(source.code) == rex::PadAxes::kRight
             ? 2
             : 0;
}

} // namespace

InputSources &InputSources::Get() {
  static InputSources instance;
  return instance;
}

void InputSources::Sample(u32 padBlock) {
  if (const auto *pad = mem::try_at<const PadBlock_t>(padBlock)) {
    padHeld_ = pad->held;
    for (int i = 0; i < 4; ++i)
      padAnalog_[i] = pad->analog[i];
  }

  wheel_ = platform::HID().GetWheel();

  const u64 tick = TickCount();
  const bool unbroken = tick == sampledTick_ || tick == sampledTick_ + 1;
  sampledTick_ = tick;

  const u16 pressed = platform::HID().TakePadPresses(kPadUser).value_or(0);
  if (unbroken)
    padHeld_ |= PadPressBits(pressed);

  std::lock_guard lock(tapMutex_);
  taps_.clear();
  if (unbroken)
    taps_.swap(pendingTaps_);
  pendingTaps_.clear();
}

void InputSources::OnInputEvent(const rex::InputEvent &event) {
  using Type = rex::InputEvent::Type;
  rex::VirtualKey key = rex::VirtualKey::kNone;
  if (event.type == Type::kKeyDown && !event.repeat && !event.handled)
    key = event.key;
  else if (event.type == Type::kButtonDown)
    key = MouseButtonKey(event.button);
  if (key == rex::VirtualKey::kNone)
    return;

  Source tap;
  tap.type = SourceKind::kKey;
  tap.code = static_cast<u16>(key);
  tap.modifiers = event.modifiers;
  std::lock_guard lock(tapMutex_);
  if (pendingTaps_.size() < kMaxPendingTaps &&
      std::ranges::find(pendingTaps_, tap) == pendingTaps_.end())
    pendingTaps_.push_back(tap);
}

bool InputSources::Tapped(const Source &source) const {
  const bool mouse = MouseKeyButton(source.code) != rex::MouseButton::kNone;
  const u8 mods = static_cast<u8>(source.modifiers);
  for (const Source &tap : taps_) {
    const bool match =
        mouse ? tap.code == source.code &&
                    (static_cast<u8>(tap.modifiers) & mods) == mods
              : rex::IsSameInput(tap, source);
    if (match)
      return true;
  }
  return false;
}

bool InputSources::Active(const Source &source) const {
  if (MouseTaken(source))
    return false;
  switch (source.type) {
  case SourceKind::kKey: {
    if (Tapped(source))
      return true;
    const u8 held = static_cast<u8>(platform::HID().GetModifiers());
    const u8 mods = static_cast<u8>(source.modifiers);
    const rex::MouseButton button = MouseKeyButton(source.code);
    if (button != rex::MouseButton::kNone)
      return (held & mods) == mods &&
             platform::HID().IsButtonDown(button);
    if (held != mods)
      return false;
    return platform::HID().IsKeyDown(
        static_cast<rex::VirtualKey>(source.code));
  }
  case SourceKind::kMouseWheel:
    return source.sign != 0 &&
           ((wheel_ > 0 && source.sign > 0) || (wheel_ < 0 && source.sign < 0));
  case SourceKind::kPadAxes: {
    const int base = PadAxisBase(source);
    return padAnalog_[base] != 0.0f || padAnalog_[base + 1] != 0.0f;
  }
  case SourceKind::kPadButton:
    return (padHeld_ & PadButtonBit(source.code)) != 0;
  }
  return false;
}

f32 InputSources::Axis(const Source &source, int component) const {
  if (MouseTaken(source))
    return 0.0f;
  switch (source.type) {
  case SourceKind::kPadAxes:
    return component == 0 || component == 1
               ? padAnalog_[PadAxisBase(source) + component]
               : 0.0f;
  default:
    break;
  }

  if (source.axis_slot == 0)
    return 0.0f;
  const int direction = rex::hid::GetAxisDirection(source.axis_slot);
  const bool isY = direction == 0 || direction == 1;
  if ((component == 1) != isY)
    return 0.0f;
  if (!Active(source))
    return 0.0f;
  const bool positive = direction == 0 || direction == 3;
  return positive ? 1.0f : -1.0f;
}

bool InputSources::KeyboardEnabled() const {
  return bd::cvars::Get().keyboard_input.Get();
}

bool InputSources::MouseEnabled() const {
  return bd::cvars::Get().mouse_input.Get();
}

bool InputSources::DeviceEnabled(const Source &source) const {
  if (IsMouse(source))
    return MouseEnabled();
  if (source.type == SourceKind::kKey)
    return KeyboardEnabled();
  return true;
}

f32 InputSources::PadAnalog(int slot) const {
  return slot >= 0 && slot < 4 ? padAnalog_[slot] : 0.0f;
}

} // namespace bd::engine
