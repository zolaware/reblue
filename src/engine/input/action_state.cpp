#include "engine/input/action_state.h"

#include <cmath>
#include <vector>

#include "core/memory_helpers.h"
#include "engine/input/binding_store.h"
#include "engine/input/input_sources.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kFrameDelta = 0x829A8694;
} // namespace addr

namespace {

constexpr f32 kRepeatDelay = 0.2f;

constexpr f32 kRepeatRearm = 0.001f;

constexpr int kMaxConsumed = 16;

bool SameInput(const Source &a, const Source &b) {
  return a.kind == b.kind && a.code == b.code && a.mods == b.mods &&
         a.sign == b.sign;
}

bool Consumed(const Source *list, int count, const Source &source) {
  for (int i = 0; i < count; ++i) {
    if (SameInput(list[i], source))
      return true;
  }
  return false;
}

bool InRange(Action action) {
  const int i = static_cast<int>(action);
  return i >= 0 && i < kActionCount;
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

  Source consumed[kMaxConsumed];
  int consumedCount = 0;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (Describe(action).kind != ActionKind::Dispatched)
      continue;
    const auto &sources = binds.Sources(action);
    bool held = false;
    for (const Source &s : sources) {
      if (in.Active(s)) {
        held = true;
        break;
      }
    }
    held_[i] = held;
    repeat_[i] = false;
    if (!held || prevHeld_[i])
      continue;
    for (const Source &s : sources) {
      if (consumedCount == kMaxConsumed)
        break;
      if (in.Active(s))
        consumed[consumedCount++] = s;
    }
  }

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (Describe(action).kind != ActionKind::Mapped)
      continue;
    bool held = false;
    for (const Source &s : binds.Sources(action)) {
      if (Consumed(consumed, consumedCount, s))
        continue;
      if (in.Active(s)) {
        held = true;
        break;
      }
    }
    held_[i] = held;
  }

  for (int i = 0; i < kActionCount; ++i) {
    if (!forced_[i])
      continue;
    held_[i] = true;
    forced_[i] = false;
  }

  bool anyHeld = false;
  bool anyPressed = false;
  for (int i = 0; i < kActionCount; ++i) {
    if (Describe(static_cast<Action>(i)).kind != ActionKind::Mapped)
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
  const f32 delta = mem::try_load<f32>(addr::kFrameDelta);
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
      if (Describe(static_cast<Action>(i)).kind != ActionKind::Mapped)
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
    if (desc.axis == AxisPair::None)
      continue;
    const int pair = static_cast<int>(desc.axis);
    for (const Source &s : binds.Sources(action)) {
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
