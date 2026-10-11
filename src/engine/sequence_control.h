/**
 * @file    engine/sequence_control.h
 * @brief   The top-level module dispatcher.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/task.h"

namespace bd::engine {

class SequenceControl : public Task {
public:
  SequenceControl() = default;
  explicit SequenceControl(u32 address) : Task(address) {}

  // The running module. Which class it is stays unresolved, so this is an
  // address and not a handle.
  u32 CurrentModuleAddress() const;
};

} // namespace bd::engine
