/**
 * @file    engine/game_tables.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/game_tables.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kPhenomeTable = 0x82DC9A98;      // g_pPhenomeTable
inline constexpr u32 kPhenomeTableCount = 0x82DC9AA0; // record count
inline constexpr u32 kItemTable = 0x82DC9AF0;         // g_pItemTable
inline constexpr u32 kItemTableCount = 0x82DC9AF4;    // record count
} // namespace addr

namespace {

// Sizes and the id field, from the linear scans in bdPhenomeTableFindById and
// bdItemTableFindById. name is a pointer to a wide string. The phenome one is
// handed to the d2anime string setter by Camp_BuildMemberStatusPanel, and the
// item one is read by wcslen in sub_821C5380.
struct PhenomeRecord_t {
  /* 0x00 */ u8 _pad00[0x08];
  /* 0x08 */ be_u32 name;
  /* 0x0C */ u8 _pad0C[0x14 - 0x0C];
  /* 0x14 */ be_u32 id;
  /* 0x18 */ u8 _pad18[0x50 - 0x18];
};
static_assert(offsetof(PhenomeRecord_t, name) == 0x08);
static_assert(offsetof(PhenomeRecord_t, id) == 0x14);
static_assert(sizeof(PhenomeRecord_t) == 0x50);

struct ItemRecord_t {
  /* 0x00 */ u8 _pad00[0x08];
  /* 0x08 */ be_u32 name;
  /* 0x0C */ u8 _pad0C[0x14 - 0x0C];
  /* 0x14 */ be_u32 id;
  /* 0x18 */ u8 _pad18[0x64 - 0x18];
};
static_assert(offsetof(ItemRecord_t, name) == 0x08);
static_assert(offsetof(ItemRecord_t, id) == 0x14);
static_assert(sizeof(ItemRecord_t) == 0x64);

// A record count read while the engine is still filling the table can be
// garbage. The shipped tables are on the order of a thousand rows.
constexpr u32 kTableScanCap = 8192;

// The pointer comes out of a table we do not own, so a bad entry must yield an
// empty name rather than a walk off the end of engine memory.
constexpr u32 kMaxNameChars = 128;

template <typename Record> u32 FindRecord(u32 rootVA, u32 countVA, u32 id) {
  const u32 root = mem::try_load<u32>(rootVA);
  const u32 count = mem::try_load<u32>(countVA);
  if (!root || !count || count > kTableScanCap)
    return 0;
  for (u32 i = 0; i < count; ++i) {
    const u32 rec = root + i * u32(sizeof(Record));
    const auto *p = mem::try_at<const Record>(rec);
    if (!p)
      return 0;
    if (static_cast<u32>(p->id) == id)
      return rec;
  }
  return 0;
}

template <typename Record> std::string NameAt(u32 record) {
  const auto *p = mem::try_at<const Record>(record);
  return GameTables::Name(p ? u32(p->name) : 0);
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
  return FindRecord<PhenomeRecord_t>(addr::kPhenomeTable,
                                     addr::kPhenomeTableCount, id);
}

u32 GameTables::ItemRecord(u32 id) const {
  return FindRecord<ItemRecord_t>(addr::kItemTable, addr::kItemTableCount, id);
}

std::string GameTables::PhenomeName(u32 id) const {
  const u32 rec = PhenomeRecord(id);
  return rec ? NameAt<PhenomeRecord_t>(rec) : std::string{};
}

std::string GameTables::ItemName(u32 id) const {
  const u32 rec = ItemRecord(id);
  return rec ? NameAt<ItemRecord_t>(rec) : std::string{};
}

} // namespace bd::engine
