/**
 * @file    engine/script_vars.h
 * @brief   The script variable block: the globals and the two-bit flags the
 *          save block is cut from.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

// bdSaveBlockCapture copies its first 20480 bytes straight into the save
// block, so reading it live is reading save state. Two flag namespaces share
// it: 32-bit globals, and a two-bit array above them. Falsy off-field.
class ScriptVars : public Object {
public:
  // The highest two-bit flag the block has room for.
  static constexpr u32 kFlagMax = 0x257F;

  ScriptVars() = default;
  explicit ScriptVars(u32 address) : Object(address) {}

  // Global script variable N, indexed from 0 for what the script calls 256.
  // Chest and barrier flag ids index this directly.
  u32 Global(u32 index) const;

  // Two-bit script flag, set by a search point when it is used up.
  u32 Flag(u32 id) const;

  u32 NothingsCollected() const;
};

} // namespace bd::engine
