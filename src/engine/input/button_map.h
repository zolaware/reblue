#pragma once

#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

class ButtonMap {
public:
  static ButtonMap &Get();

  void Rebuild();

  int Id(Action action) const;

  u32 Mask(Action action) const;

  void Install();

  u32 GeneralRowVA() const { return rowVA_; }

private:
  ButtonMap() = default;

  static constexpr u32 kNoGeneration = 0xFFFFFFFFu;

  int id_[kActionCount] = {};
  u32 rowVA_ = 0;
  u32 generation_ = kNoGeneration;
  bool installed_ = false;
};

} // namespace bd::engine
