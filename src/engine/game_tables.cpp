/**
 * @file    engine/game_tables.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/game_tables.h"

#include "core/memory_helpers.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kPhenomeTable = 0x82DC9A98;      // g_pPhenomeTable
inline constexpr u32 kPhenomeTableCount = 0x82DC9AA0; // record count
inline constexpr u32 kItemTable = 0x82DC9AF0;         // g_pItemTable
inline constexpr u32 kItemTableCount = 0x82DC9AF4;    // record count
} // namespace addr

namespace {

// Strides and the id field, from the linear scans in bdPhenomeTableFindById and
// bdItemTableFindById.
constexpr u32 kPhenomeStride = 0x50;
constexpr u32 kItemStride = 0x64;
constexpr u32 kRecordId = 0x14;

// Camp_BuildMemberStatusPanel feeds the phenome record's +0x08 to the d2anime
// wide-string setter, so it is a pointer to the localized name. The item record
// is assumed to match, which is unverified.
constexpr u32 kRecordName = 0x08;

// A record count read while the engine is still filling the table can be
// garbage. The shipped tables are on the order of a thousand rows.
constexpr u32 kTableScanCap = 8192;

// The pointer comes out of a table we do not own, so a bad entry must yield an
// empty name rather than a walk off the end of engine memory.
constexpr u32 kMaxNameChars = 128;

u32 FindRecord(u32 rootVA, u32 countVA, u32 stride, u32 id) {
  const u32 root = mem::try_load<u32>(rootVA);
  const u32 count = mem::try_load<u32>(countVA);
  if (!root || !count || count > kTableScanCap)
    return 0;
  for (u32 i = 0; i < count; ++i) {
    const u32 rec = root + i * stride;
    const auto *p = mem::try_at<const be_u32>(rec + kRecordId);
    if (!p)
      return 0;
    if (static_cast<u32>(*p) == id)
      return rec;
  }
  return 0;
}

std::string NameAt(u32 record) {
  return GameTables::Name(mem::try_load<u32>(record + kRecordName));
}

} // namespace

std::string GameTables::Name(u32 stringVA) {
  if (!stringVA)
    return {};
  std::string out;
  for (u32 i = 0; i < kMaxNameChars; ++i) {
    const auto *ch = mem::try_at<const be_u16>(stringVA + i * 2);
    if (!ch)
      break;
    const u16 c = *ch;
    if (!c)
      break;
    // Transliterating rather than dropping keeps a garbled read visible instead
    // of silently empty.
    out += (c < 0x80) ? static_cast<char>(c) : '?';
  }
  return out;
}

GameTables &GameTables::Get() {
  static GameTables instance;
  return instance;
}

u32 GameTables::PhenomeRecord(u32 id) const {
  return FindRecord(addr::kPhenomeTable, addr::kPhenomeTableCount,
                    kPhenomeStride, id);
}

u32 GameTables::ItemRecord(u32 id) const {
  return FindRecord(addr::kItemTable, addr::kItemTableCount, kItemStride, id);
}

std::string GameTables::PhenomeName(u32 id) const {
  const u32 rec = PhenomeRecord(id);
  return rec ? NameAt(rec) : std::string{};
}

std::string GameTables::ItemName(u32 id) const {
  const u32 rec = ItemRecord(id);
  return rec ? NameAt(rec) : std::string{};
}

} // namespace bd::engine
