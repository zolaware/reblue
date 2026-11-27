#include "engine/input/action_state.h"

#include <cmath>
#include <span>
#include <vector>

#include "core/guest_global.h"
#include "core/memory_helpers.h"
#include "engine/input/binding_registry.h"
#include "engine/input/dispatch.h"
#include "engine/input/input_sources.h"
#include "engine/virtual_buttons.h"
#include "platform/platform.h"

namespace bd::engine {

namespace addr {
REX_GUEST_GLOBAL(be_f32, kFrameDelta, 0x829A8694);
} // namespace addr

namespace {

constexpr f32 kRepeatDelay = 0.2f;

constexpr f32 kRepeatRearm = 0.001f;

constexpr int kMaxConsumed = 16;

bool Consumed(std::span<const Source> list, const Source &source) {
  for (const Source &taken : list) {
    if (rex::IsSameInput(taken, source))
      return true;
  }
  return false;
}

} // namespace

InputActions &InputActions::Get() {
  static InputActions instance;
  return instance;
}

void InputActions::Resolve() {
  const Bindings &binds = Bindings::Get();
  const InputSources &in = InputSources::Get();

  for (int i = 0; i < kActionCount; ++i)
    prevHeld_[i] = held_[i];

  const std::span<const Source> mindows = platform::MindowsKeys();
  const OwnerMask owner = OwnerBit(ActiveInputOwner());
  Source consumed[kMaxConsumed];
  int consumedCount = 0;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    if (desc.kind != ActionKind::Dispatched)
      continue;
    const std::span<const Source> sources = binds.FrameSources(action);
    const auto active = [&](const Source &s) {
      return in.Active(s) &&
             (action == Action::Mindows || !Consumed(mindows, s));
    };
    bool down = false;
    for (const Source &s : sources) {
      if (active(s)) {
        down = true;
        break;
      }
    }
    const bool live = (desc.owners & owner) != 0;
    if (!down)
      carried_[i] = false;
    else if (!live)
      carried_[i] = true;
    const bool held = down && live && !carried_[i];
    held_[i] = held;
    repeat_[i] = false;
    if (!held) {
      claiming_[i] = false;
      continue;
    }
    if (!prevHeld_[i] && WillDispatch(action))
      claiming_[i] = true;
    if (!claiming_[i])
      continue;
    for (const Source &s : sources) {
      if (consumedCount == kMaxConsumed)
        break;
      if (active(s))
        consumed[consumedCount++] = s;
    }
  }

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    if (desc.kind == ActionKind::Dispatched)
      continue;
    bool down = false;
    for (const Source &s : binds.FrameSources(action)) {
      if (Consumed({consumed, size_t(consumedCount)}, s) ||
          Consumed(mindows, s) || !in.DeviceEnabled(s))
        continue;
      if (in.Active(s)) {
        down = true;
        break;
      }
    }
    const bool live = (desc.owners & owner) != 0;
    if (!down)
      carried_[i] = false;
    else if (!live)
      carried_[i] = true;
    held_[i] = down && live && !carried_[i];
  }

  anyForced_ = false;
  for (int i = 0; i < kActionCount; ++i) {
    if (!forced_[i])
      continue;
    held_[i] = true;
    forced_[i] = false;
    anyForced_ = true;
  }

  bool anyHeld = false;
  bool anyPressed = false;
  for (int i = 0; i < kActionCount; ++i) {
    if (Describe(static_cast<Action>(i)).kind == ActionKind::Dispatched)
      continue;
    repeat_[i] = false;
    if (!held_[i])
      continue;
    anyHeld = true;
    if (!prevHeld_[i])
      anyPressed = true;
  }

  bool fireHeld = false;
  bool firePressed = false;
  const f32 delta = mem::global_load(addr::kFrameDelta);
  if (!anyHeld) {
    repeatTimer_ = 0.0f;
  } else if (anyPressed) {
    repeatTimer_ = kRepeatDelay;
    firePressed = true;
  } else if (repeatTimer_ > 0.0f) {
    repeatTimer_ -= delta;
  } else {
    repeatTimer_ = kRepeatRearm;
    fireHeld = true;
  }

  if (fireHeld || firePressed) {
    for (int i = 0; i < kActionCount; ++i) {
      if (Describe(static_cast<Action>(i)).kind == ActionKind::Dispatched)
        continue;
      if (!held_[i])
        continue;
      repeat_[i] = fireHeld || !prevHeld_[i];
    }
  }

  for (auto &pair : axis_) {
    for (f32 &component : pair)
      component = 0.0f;
  }
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    if (desc.axis == AxisPair::None || (desc.owners & owner) == 0)
      continue;
    const int pair = static_cast<int>(desc.axis);
    for (const Source &s : binds.FrameSources(action)) {
      if (!in.DeviceEnabled(s) || Consumed(mindows, s))
        continue;
      for (int c = 0; c < 2; ++c) {
        const f32 value = in.Axis(s, c);
        if (std::abs(value) > std::abs(axis_[pair][c]))
          axis_[pair][c] = value;
      }
    }
  }
}

void InputActions::Force(Action action) {
  if (!InRange(action))
    return;
  if (Describe(action).kind != ActionKind::Mapped)
    return;
  forced_[static_cast<int>(action)] = true;
}

bool InputActions::Held(Action action) const {
  return InRange(action) && held_[static_cast<int>(action)];
}

bool InputActions::Pressed(Action action) const {
  if (!InRange(action))
    return false;
  const int i = static_cast<int>(action);
  return held_[i] && !prevHeld_[i];
}

bool InputActions::Released(Action action) const {
  if (!InRange(action))
    return false;
  const int i = static_cast<int>(action);
  return !held_[i] && prevHeld_[i];
}

bool InputActions::Repeat(Action action) const {
  return InRange(action) && repeat_[static_cast<int>(action)];
}

f32 InputActions::Axis(AxisPair pair, int component) const {
  const int p = static_cast<int>(pair);
  if (p < 0 || p > 1 || component < 0 || component > 1)
    return 0.0f;
  return axis_[p][component];
}

} // namespace bd::engine
