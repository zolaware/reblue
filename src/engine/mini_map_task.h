/**
 * @file    engine/mini_map_task.h
 * @brief   The minimap task and the floor databases it loads, which the field
 *          compass and the area map screen both draw from.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <vector>

#include <rex/types.h>

#include "engine/object.h"
#include "engine/task.h"

namespace bd::engine {

// One floor of the area minimap: the database\minimap\db_dgXX_YY.mmp record
// loaded beside a minimap\MM_dgXX_YY texture. Every accessor below is named by
// MiniMapDB_RegisterMindowsNodes, and MiniMapTask__DrawWidget reads them in
// this combination to place the compass crop.
class MiniMapDB : public Object {
public:
  MiniMapDB() = default;
  explicit MiniMapDB(u32 address) : Object(address) {}

  // Whether the record and the texture it names have both resolved.
  bool Ready() const;

  // Visual__SelectRenderTarget takes the holder eight bytes ahead of the entry
  // table it reads.
  u32 TexHolderAddress() const;

  f32 TexW() const; // TexSize
  f32 TexH() const;
  f32 ScaleX() const; // MapScale, the world extent the texture covers
  f32 ScaleZ() const;
  f32 DispW() const; // DispSize, the compass crop half-extent
  f32 DispH() const;
  f32 OffsetX() const; // OffSet, the world origin's place on the texture
  f32 OffsetZ() const;
  f32 OffsetRot() const;
  f32 PlyRot() const;
  f32 TexRot() const;
};

class MiniMapTask : public Task {
public:
  MiniMapTask() = default;
  explicit MiniMapTask(u32 address) : Task(address) {}

  // The ring, player arrow and target marker sheet.
  u32 ChromeTexAddress() const;

  MiniMapDB BaseFloor() const;
  MiniMapDB Floor() const;

  // The MM_dgXX_YY_NN sub-floors, in the order
  // MiniMapTask_SelectCurrentFloorDB walks them.
  std::vector<MiniMapDB> Floors() const;

  // Identity triple: a repeat load with the same values returns early instead
  // of reloading.
  u32 Category() const;
  void SetCategory(u32 category);
  u32 AreaHi() const;
  u32 AreaLo() const;
};

} // namespace bd::engine
