#include "engine/input/input_sources.h"

#include <algorithm>
#include <cmath>

#include <rex/ui/virtual_key.h>

#include "core/memory_helpers.h"
#include "engine/input/actions.h"
#include "engine/virtual_buttons.h"
#include "platform/keyboard_input.h"
#include "platform/mouse_input.h"

namespace bd::engine {

namespace {

constexpr u32 kPadHeldOffset = 0x0C;
constexpr u32 kPadAnalogOffset[4] = {0x1C, 0x20, 0x24, 0x28};

constexpr u32 kPadButtonBits[24] = {
    1u << 0,   1u << 1,   1u << 2,   1u << 3,  1u << 4,  1u << 5,
    1u << 6,   1u << 7,   1u << 8,   1u << 9,  1u << 10, 1u << 11,
    0x4000u,   0x8000u,   0x2000u,   0x1000u,
    0x10000u,  0x20000u,  0x40000u,  0x80000u,
    0x100000u, 0x200000u, 0x400000u, 0x800000u,
};

constexpr f32 kAxisLiveEpsilon = 0.001f;

constexpr f32 kMouseAxisScale = 1.0f / 20.0f;

u32 PadButtonBit(u16 id) {
  constexpr u16 kCount = sizeof(kPadButtonBits) / sizeof(kPadButtonBits[0]);
  return id < kCount ? kPadButtonBits[id] : 0u;
}

f32 ClampAxis(f32 v) { return std::clamp(v, -1.0f, 1.0f); }

struct MouseKey {
  rex::ui::VirtualKey key;
  rex::ui::MouseEvent::Button button;
};

constexpr MouseKey kMouseKeys[] = {
    {rex::ui::VirtualKey::kLButton, rex::ui::MouseEvent::Button::kLeft},
    {rex::ui::VirtualKey::kRButton, rex::ui::MouseEvent::Button::kRight},
    {rex::ui::VirtualKey::kMButton, rex::ui::MouseEvent::Button::kMiddle},
};

rex::ui::MouseEvent::Button MouseKeyButton(u16 code) {
  const auto key = static_cast<rex::ui::VirtualKey>(code);
  for (const MouseKey &entry : kMouseKeys)
    if (entry.key == key)
      return entry.button;
  return rex::ui::MouseEvent::Button::kNone;
}

bool IsMouse(const Source &source) {
  switch (source.kind) {
  case SourceKind::MouseButton:
  case SourceKind::MouseWheel:
  case SourceKind::MouseAxes:
    return true;
  case SourceKind::Key:
    return MouseKeyButton(source.code) != rex::ui::MouseEvent::Button::kNone;
  default:
    return false;
  }
}

bool MouseTaken(const Source &source) {
  return IsMouse(source) && HostOverlayOwnsPointer();
}

int PadAxisBase(const Source &source) {
  return static_cast<AxisPair>(source.code) == AxisPair::Right ? 2 : 0;
}

} // namespace

InputSources &InputSources::Get() {
  static InputSources instance;
  return instance;
}

void InputSources::Sample(u32 padBlock) {
  padHeld_ = mem::try_load<u32>(padBlock + kPadHeldOffset, padHeld_);
  for (int i = 0; i < 4; ++i) {
    padAnalog_[i] =
        mem::try_load<f32>(padBlock + kPadAnalogOffset[i], padAnalog_[i]);
  }

  wheel_ = platform::Mouse().WheelDetents();

  f32 dx = 0.0f;
  f32 dy = 0.0f;
  if (platform::Mouse().TakeDelta(dx, dy)) {
    mouseDx_ = dx;
    mouseDy_ = dy;
  } else {
    mouseDx_ = 0.0f;
    mouseDy_ = 0.0f;
  }
}

bool InputSources::Active(const Source &source) const {
  if (MouseTaken(source))
    return false;
  switch (source.kind) {
  case SourceKind::Key: {
    if (platform::Keyboard().Modifiers() != source.mods)
      return false;
    const rex::ui::MouseEvent::Button button = MouseKeyButton(source.code);
    if (button != rex::ui::MouseEvent::Button::kNone)
      return platform::Mouse().IsButtonDown(button);
    return platform::Keyboard().IsDown(
        static_cast<rex::ui::VirtualKey>(source.code));
  }
  case SourceKind::MouseWheel:
    return source.sign != 0 &&
           ((wheel_ > 0 && source.sign > 0) || (wheel_ < 0 && source.sign < 0));
  case SourceKind::MouseAxes:
    return std::abs(mouseDx_) > kAxisLiveEpsilon ||
           std::abs(mouseDy_) > kAxisLiveEpsilon;
  case SourceKind::PadAxes: {
    const int base = PadAxisBase(source);
    return padAnalog_[base] != 0.0f || padAnalog_[base + 1] != 0.0f;
  }
  case SourceKind::PadButton:
    return (padHeld_ & PadButtonBit(source.code)) != 0;
  case SourceKind::MouseButton:
    return platform::Mouse().IsButtonDown(
        static_cast<rex::ui::MouseEvent::Button>(source.code));
  }
  return false;
}

f32 InputSources::Axis(const Source &source, int component) const {
  if (MouseTaken(source))
    return 0.0f;
  switch (source.kind) {
  case SourceKind::PadAxes:
    return component == 0 || component == 1
               ? padAnalog_[PadAxisBase(source) + component]
               : 0.0f;
  case SourceKind::MouseAxes: {
    const f32 delta = component == 0 ? mouseDx_ : -mouseDy_;
    return ClampAxis(delta * kMouseAxisScale);
  }
  default:
    break;
  }

  if (source.axisSlot == 0)
    return 0.0f;
  const bool isY = source.axisSlot == 1 || source.axisSlot == 2;
  if ((component == 1) != isY)
    return 0.0f;
  if (!Active(source))
    return 0.0f;
  const bool positive = source.axisSlot == 1 || source.axisSlot == 4;
  return positive ? 1.0f : -1.0f;
}

f32 InputSources::PadAnalog(int slot) const {
  return slot >= 0 && slot < 4 ? padAnalog_[slot] : 0.0f;
}

} // namespace bd::engine
