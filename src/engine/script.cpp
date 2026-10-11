/**
 * @file    engine/script.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/script.h"

#include <cstddef>
#include <cstdio>

#include "core/encoding.h"
#include "core/memory_helpers.h"

namespace bd::engine {

namespace addr {
// The namelist_map_<locale>.u16 table bdMapDataInit fills, one vector per
// category. bdStageRecordFindByNumber (0x82394220) is the engine's own lookup.
inline constexpr u32 kStageRecordVectors = 0x82DEB908;
} // namespace addr

namespace {

constexpr u32 kMaxCategory = 8; // bdStageRecordFindByNumber's own bound

constexpr size_t kStageNameCap = 32;

constexpr u32 kScenePathCap = 0x40;

struct ScaOp_t {
  /* 0x00 */ u8 _pad00[0x10];
  /* 0x10 */ be_u32 group;
};
static_assert(offsetof(ScaOp_t, group) == 0x10);

struct Script_t {
  /* 0x000 */ u8 _pad000[0x6C];
  /* 0x06C */ be_u32 category;
  /* 0x070 */ be_u32 combinedNum;
  u8 _pad074[0x130 - 0x074];
  be_u32 routine;
  u8 _pad134[0x160 - 0x134];
  /* 0x160 */ be_u32 scene;
  /* 0x164 */ char scenePath[kScenePathCap];
  /* 0x1A4 */ u8 _pad1A4[0x4A8 - (0x164 + kScenePathCap)];
  /* 0x4A8 */ be_u32 currentOp;
  /* 0x4AC */ u8 _pad4AC[0x6C8 - 0x4AC];
  /* 0x6C8 */ be_u32 searchPoints;
  /* 0x6CC */ be_u32 searchPointCount;
};
static_assert(offsetof(Script_t, category) == 0x06C);
static_assert(offsetof(Script_t, combinedNum) == 0x070);
static_assert(offsetof(Script_t, routine) == 0x130);
static_assert(offsetof(Script_t, scene) == 0x160);
static_assert(offsetof(Script_t, scenePath) == 0x164);
static_assert(offsetof(Script_t, currentOp) == 0x4A8);
static_assert(offsetof(Script_t, searchPoints) == 0x6C8);
static_assert(offsetof(Script_t, searchPointCount) == 0x6CC);

struct StageRecordVector_t {
  /* 0x00 */ be_u32 owner;
  /* 0x04 */ be_u32 begin;
  /* 0x08 */ be_u32 end;
  /* 0x0C */ be_u32 capacity;
};
static_assert(sizeof(StageRecordVector_t) == 16);

struct StageRecord_t {
  /* 0x000 */ be_u16 name[64];
  /* 0x080 */ be_u16 nameAlt[64];
  /* 0x100 */ u8 _pad100[0x198 - 0x100];
  /* 0x198 */ be_u32 combinedNum;
  /* 0x19C */ u8 _pad19C[0x210 - 0x19C];
};
static_assert(sizeof(StageRecord_t) == 528);
static_assert(offsetof(StageRecord_t, combinedNum) == 408);

std::string StageDisplayName(u32 cat, u32 num) {
  if (cat > kMaxCategory)
    return {};
  const auto *vec = mem::try_at<const StageRecordVector_t>(
      addr::kStageRecordVectors + cat * sizeof(StageRecordVector_t));
  if (!vec)
    return {};
  const u32 begin = vec->begin;
  const u32 end = vec->end;
  if (!begin || end <= begin)
    return {};

  for (u32 ea = begin; ea + sizeof(StageRecord_t) <= end;
       ea += sizeof(StageRecord_t)) {
    const auto *rec = mem::try_at<const StageRecord_t>(ea);
    if (!rec || rec->combinedNum != num)
      continue;
    std::u16string name;
    for (const be_u16 &unit : rec->name) {
      if (!unit)
        break;
      name.push_back(static_cast<char16_t>(static_cast<u16>(unit)));
    }
    return bd::U16ToUtf8(name);
  }
  return {};
}

} // namespace

void Script::BuildName(char *out, size_t cap, u32 cat, u32 num) {
  if (cap == 0)
    return;
  out[0] = '\0';
  const u32 hi = num / 100u;
  const u32 lo = num % 100u;
  const auto area = static_cast<AreaCategory>(cat);
  switch (area) {
  case AreaCategory::Gr:
    std::snprintf(out, cap, "gr%02u_%02u", hi, lo);
    break;
  case AreaCategory::Bg:
    std::snprintf(out, cap, "bg%02u_%02u", hi, lo);
    break;
  case AreaCategory::Dg:
    std::snprintf(out, cap, "dg%02u_%02u", hi, lo);
    break;
  case AreaCategory::Wc:
    std::snprintf(out, cap, "wc%02u_%02u", hi, lo);
    break;
  case AreaCategory::Eb:
    std::snprintf(out, cap, "eb%02u_%02u", hi, lo);
    break;
  case AreaCategory::Sp:
    std::snprintf(out, cap, "sp%02u_%02u", hi, lo);
    break;
  case AreaCategory::Bt:
    std::snprintf(out, cap, "bt%02u_%02u", hi, lo);
    break;
  case AreaCategory::Bi:
    // The 'a' at out[4] is stepped by (num % 10000) / 100.
    std::snprintf(out, cap, "bi%02ua%02u", num / 10000u, lo);
    if (cap > 4 && out[4])
      out[4] = static_cast<char>(out[4] + (num % 10000u) / 100u);
    break;
  case AreaCategory::Wd:
    // The 'a' at out[3] is stepped by hi, skipping 'l'.
    std::snprintf(out, cap, "wd_a%02u", lo + 1u);
    if (cap > 3 && out[3]) {
      unsigned c = static_cast<unsigned char>(out[3]) + hi;
      if (c >= 'l')
        ++c;
      out[3] = static_cast<char>(c);
    }
    break;
  default:
    break;
  }
}

u32 ScaOp::Group() const {
  const auto *self = Self<ScaOp_t>();
  return self ? static_cast<u32>(self->group) : 0;
}

u32 Script::Category() const {
  const auto *self = Self<Script_t>();
  return self ? static_cast<u32>(self->category) : 0;
}

u32 Script::CombinedNum() const {
  const auto *self = Self<Script_t>();
  return self ? static_cast<u32>(self->combinedNum) : 0;
}

u32 Script::Area() const { return CombinedNum() / 100u; }

u32 Script::Sub() const { return CombinedNum() % 100u; }

ScaOp Script::CurrentOp() const {
  const auto *self = Self<Script_t>();
  return ScaOp(self ? static_cast<u32>(self->currentOp) : 0);
}

bool Script::Busy() const {
  const auto *self = Self<Script_t>();
  return self && static_cast<u32>(self->routine) != 0;
}

engine::SceneFile Script::Scene() const {
  const auto *self = Self<Script_t>();
  return engine::SceneFile(self ? static_cast<u32>(self->scene) : 0);
}

std::string Script::ScenePath() const {
  const auto *self = Self<Script_t>();
  if (!self)
    return {};
  size_t len = 0;
  while (len < kScenePathCap && self->scenePath[len])
    ++len;
  return std::string(self->scenePath, len);
}

u32 Script::SearchPointCount() const {
  const auto *self = Self<Script_t>();
  return self ? static_cast<u32>(self->searchPointCount) : 0;
}

engine::SearchPoint Script::SearchPointAt(u32 index) const {
  const auto *self = Self<Script_t>();
  if (!self || index >= static_cast<u32>(self->searchPointCount))
    return engine::SearchPoint();
  const u32 first = static_cast<u32>(self->searchPoints);
  if (!first)
    return engine::SearchPoint();
  return engine::SearchPoint(first + index * engine::SearchPoint::kStride);
}

std::string Script::Name() const {
  if (!Address())
    return {};
  char buf[kStageNameCap];
  BuildName(buf, sizeof(buf), Category(), CombinedNum());
  return buf;
}

std::string Script::DisplayName() const {
  return Address() ? StageDisplayName(Category(), CombinedNum())
                   : std::string{};
}

} // namespace bd::engine
