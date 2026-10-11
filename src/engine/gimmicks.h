/**
 * @file    engine/gimmicks.h
 * @brief   How many of a map's gimmicks are still untouched: search points,
 *          scripted loot, treasure chests and elemental barriers, read off the
 *          stage the engine has loaded.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

class Script;

// The first eleven are Gmk::ReactGim's kind, in the order of the engine's own
// name table, and are what the search point CSVs spell NONE, MESS and so on.
// Chest and Barrier sit past that table.
enum class GimmickKind : u32 {
  Nothing,
  Message,
  Item,
  Gold,
  Heal,
  Damage,
  Status,
  Medal,
  Lock,
  Grass,
  Param,
  Chest,
  Barrier,
};
inline constexpr u32 kSearchKindCount = 11; // the ones a search point can be

const char *ToString(GimmickKind kind);

// bdFlagListLoad's own order.
enum class BarrierColor : u32 { Blue, Red, Green, White, Black };
inline constexpr u32 kBarrierColorCount = 5;

const char *ToString(BarrierColor color);

struct Tally {
  u32 total = 0;
  u32 remaining = 0;

  u32 Found() const { return total - remaining; }
  bool IsComplete() const { return remaining == 0; }
};

struct Marker {
  GimmickKind kind = GimmickKind::Nothing;
  bool collected = false;
  // False when a search point carries no flag: it respawns on every map load,
  // so collected is meaningless and it can never be counted down.
  bool trackable = false;
  u16 flag = 0;
  u8 opensAt = 0;
  u32 value = 0;
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
};

// Rows built from the stage the engine has loaded, read against the live flag
// array. Every query takes a map stem as bdStageNameBuild spells it
// ("dg05_01", "bi03d02"). A stem no held stage carries tallies zero, including
// the one an off-field Stage().Name() gives.
//
// Points whose flag is -1 respawn on every map load and count in neither total
// nor remaining, so a floor made only of those reads as 0 of 0.
class Gimmicks {
public:
  static Gimmicks &Get();

  Gimmicks(const Gimmicks &) = delete;
  Gimmicks &operator=(const Gimmicks &) = delete;

  void Init();

  // True once the engine's flag array resolves. Every tally is zero until
  // then.
  bool IsReady() const;

  bool Has(std::string_view stem) const;

  Tally Points(std::string_view stem,
               std::optional<GimmickKind> kind = {}) const;
  Tally Chests(std::string_view stem) const;
  Tally Barriers(std::string_view stem,
                 std::optional<BarrierColor> color = {}) const;

  // Everything placed on one map, taken or not.
  std::vector<Marker> Markers(std::string_view stem) const;

private:
  Gimmicks();
  ~Gimmicks();

  struct Stage;

  void Build(const Script &script);
  void Drop(const Script &script);
  const Stage *Find(std::string_view stem) const;

  std::vector<Stage> stages_;
};

} // namespace bd::engine
