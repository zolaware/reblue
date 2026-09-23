#pragma once

#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

class InputActions {
public:
  static InputActions &Get();

  void Resolve();

  void Force(Action action);

  bool Held(Action action) const;
  bool Pressed(Action action) const;
  bool Released(Action action) const;
  bool Repeat(Action action) const;

  f32 Axis(AxisPair pair, int component) const;

private:
  InputActions() = default;

  bool held_[kActionCount] = {};
  bool prevHeld_[kActionCount] = {};
  bool repeat_[kActionCount] = {};
  bool forced_[kActionCount] = {};
  bool claiming_[kActionCount] = {};
  f32 repeatTimer_ = 0.0f;
  f32 axis_[2][2] = {};
};

} // namespace bd::engine
