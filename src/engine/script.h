/**
 * @file    engine/script.h
 * @brief   The stage script the ScriptManTask is running: its area identity,
 *          the names built from it, and the scene and search points loaded
 *          under it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>

#include "engine/scene_file.h"
#include "engine/task.h"

namespace bd::engine {

// Script area category, named for the stage-name prefix each one selects.
enum class AreaCategory : u32 {
  Gr = 0,
  Bg = 1,
  Bi = 2,
  Dg = 3,
  Wd = 4,
  Wc = 5,
  Eb = 6,
  Sp = 7,
  Bt = 8,
};

// One SCA opcode record, as the script VM is about to run it.
class ScaOp : public Object {
public:
  ScaOp() = default;
  explicit ScaOp(u32 address) : Object(address) {}

  // The engine builds a sequence id as group * 1000 + number, reading a
  // group of 0 as 1.
  u32 Group() const;
};

class Script : public Task {
public:
  Script() = default;
  explicit Script(u32 address) : Task(address) {}

  u32 Category() const;    // area kind, selects the stage-name prefix
  u32 CombinedNum() const; // area * 100 + sub
  u32 Area() const;
  u32 Sub() const;

  ScaOp CurrentOp() const;

  bool Busy() const;

  engine::SceneFile Scene() const;
  std::string ScenePath() const;

  u32 SearchPointCount() const;
  engine::SearchPoint SearchPointAt(u32 index) const;

  // Built the way bdStageNameBuild does, e.g. "bg03_01". Empty when the
  // category has no known prefix.
  std::string Name() const;

  // What the game calls this stage on screen, e.g. "Ancient Hospital Ruins -
  // 1F". Empty when namelist_map has no row for it.
  std::string DisplayName() const;

  // Mirrors bdStageNameBuild: the stem from a category and a combinedNum,
  // empty for a category with no stem.
  static void BuildName(char *out, size_t cap, u32 cat, u32 num);
};

} // namespace bd::engine
