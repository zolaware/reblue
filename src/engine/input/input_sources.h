#pragma once

#include <mutex>
#include <vector>

#include <rex/input_keys.h>
#include <rex/types.h>

#include "engine/input/binding_registry.h"

namespace bd::engine {

class InputSources {
public:
  static InputSources &Get();

  void Sample(u32 padBlock);

  void OnInputEvent(const rex::InputEvent &event);

  bool Active(const Source &source) const;

  f32 Axis(const Source &source, int component) const;

  bool KeyboardEnabled() const;
  bool MouseEnabled() const;

  bool DeviceEnabled(const Source &source) const;

  f32 PadAnalog(int slot) const;

  u32 PadHeld() const { return padHeld_; }

private:
  InputSources() = default;

  bool Tapped(const Source &source) const;

  u32 padHeld_ = 0;
  f32 padAnalog_[4] = {};
  int wheel_ = 0;
  std::mutex tapMutex_;
  std::vector<Source> pendingTaps_;
  std::vector<Source> taps_;
  u64 sampledTick_ = 0;
};

} // namespace bd::engine
