#pragma once

#include <rex/types.h>

#include "engine/input/binding_store.h"

namespace bd::engine {

inline constexpr f32 kMouseAxisScale = 1.0f / 20.0f;

class InputSources {
public:
  static InputSources &Get();

  void Sample(u32 padBlock);

  bool Active(const Source &source) const;

  f32 Axis(const Source &source, int component) const;

  f32 PadAnalog(int slot) const;

  u32 PadHeld() const { return padHeld_; }

private:
  InputSources() = default;

  u32 padHeld_ = 0;
  f32 padAnalog_[4] = {};
  f32 mouseDx_ = 0.0f;
  f32 mouseDy_ = 0.0f;
  f32 mouseScale_ = kMouseAxisScale;
  int wheel_ = 0;
};

} // namespace bd::engine
