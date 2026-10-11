/**
 * @file    engine/sequence_control.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/sequence_control.h"

#include <cstddef>

namespace bd::engine {

namespace {

struct SequenceControl_t {
  /* 0x00 */ u8 _pad00[0x70];
  /* 0x70 */ be_u32 currentModule;
};
static_assert(offsetof(SequenceControl_t, currentModule) == 0x70);

} // namespace

u32 SequenceControl::CurrentModuleAddress() const {
  const auto *self = Self<SequenceControl_t>();
  return self ? static_cast<u32>(self->currentModule) : 0;
}

} // namespace bd::engine
