/**
 * @file    engine/game_tables.h
 * @brief   Name lookup in the record tables DatabaseTask has already parsed.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string>

#include <rex/types.h>

namespace bd::engine {

// Both tables are flat arrays the engine searches linearly by an id field. The
// phenome table holds the accessory records the equipment array indexes, and
// bdPhenomeTableFindById is fed one everywhere the camp screens call it. It is
// not a skill table: a skill carries its own name on its class table record.
//
// The name each record carries is already in the active language, so reading
// them beats parsing the shipped .u16 files ourselves.
//
// A lookup is a linear scan, so call it once per subject per frame rather than
// per draw.
class GameTables {
public:
  static GameTables &Get();

  // An engine wide string at an absolute address, for records that carry a name
  // pointer of their own. Empty when the pointer does not resolve.
  static std::string Name(u32 stringVA);

  // Empty when the table is not loaded or the id is not in it.
  std::string PhenomeName(u32 id) const;
  std::string ItemName(u32 id) const;

  // Engine address of the record, or zero.
  u32 PhenomeRecord(u32 id) const;
  u32 ItemRecord(u32 id) const;

private:
  GameTables() = default;
};

} // namespace bd::engine
