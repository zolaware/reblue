/**
 * @file    engine/scene_file.h
 * @brief   The stage scene the field loader relocated in place: its entries,
 *          the condition blocks under each, and the search points the it CSV
 *          placed.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <functional>
#include <span>

#include <rex/types.h>

#include "engine/chara.h"
#include "engine/object.h"

namespace bd::engine {

enum class SceneEntryType : u32 {
  Spawn = 0,
  Box = 1,
  Zone = 2,
  Enemy = 3,
  Link = 4,
  Entity = 5,
  Warp = 6,
};

enum class SceneOp : u32 {
  SetVariable = 5003,
  GiveItem = 5004,
  GiveGold = 5005,
  GiveMedal = 5034,
  GiveItemSpecial = 5086,
  SetVariableAlt = 5097,
};

inline constexpr u32 kSceneConditionCount = 8;
inline constexpr u32 kSceneConditionVariable = 2;
inline constexpr u32 kSceneGlobalBase = 256;

struct SceneCondition {
  u32 type = 0;
  u32 operand = 0;
  u32 op = 0;
  u32 value = 0;
};

struct SceneInstruction {
  u32 opcode = 0;
  std::span<const be_u32> params;
};

class SceneBlock : public Object {
public:
  SceneBlock() = default;
  explicit SceneBlock(u32 address) : Object(address) {}

  i32 ChapterMin() const;
  i32 ChapterMax() const;
  SceneCondition Condition(u32 index) const;
  u32 TypeData() const;
  SceneBlock Next() const;

  void ForEachInstruction(
      const std::function<void(const SceneInstruction &)> &fn) const;
};

class SceneEntry : public Object {
public:
  SceneEntry() = default;
  explicit SceneEntry(u32 address) : Object(address) {}

  u32 Id() const;
  SceneEntryType Type() const;
  Vec3 Position() const;
  u32 BlockCount() const;
  SceneBlock FirstBlock() const;
  SceneEntry Next() const;
};

class SceneFile : public Object {
public:
  SceneFile() = default;
  explicit SceneFile(u32 address) : Object(address) {}

  SceneEntry FirstEntry() const;
};

class SearchPoint : public Object {
public:
  static constexpr u32 kStride = 0x114;

  SearchPoint() = default;
  explicit SearchPoint(u32 address) : Object(address) {}

  Vec3 Position() const;
  i32 Flag() const;
  u32 Kind() const;
};

} // namespace bd::engine
