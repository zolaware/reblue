/**
 * @file    engine/mini_map_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/mini_map_task.h"

#include <algorithm>
#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

struct MiniMapDB_t {
  /* 0x000 */ be_u32 kind;
  /* 0x004 */ be_f32 texW;
  /* 0x008 */ be_f32 texH;
  /* 0x00C */ be_f32 scaleX;
  /* 0x010 */ be_f32 scaleZ;
  /* 0x014 */ be_f32 dispW;
  /* 0x018 */ be_f32 dispH;
  /* 0x01C */ be_f32 offsetX;
  /* 0x020 */ be_f32 offsetZ;
  /* 0x024 */ be_f32 offsetRot;
  /* 0x028 */ be_f32 plyRot;
  /* 0x02C */ u8 _pad02C[0x764 - 0x02C];
  /* 0x764 */ be_f32 texRot;
  /* 0x768 */ u8 _pad768[0x78C - 0x768];
  /* 0x78C */ be_u32 texHolder;
  /* 0x790 */ u8 _pad790[0x794 - 0x790];
  /* 0x794 */ be_u32 texEntries; // null until the .mmp and its texture resolve
};
static_assert(sizeof(MiniMapDB_t) == 0x798);
static_assert(offsetof(MiniMapDB_t, offsetRot) == 0x024);
static_assert(offsetof(MiniMapDB_t, texRot) == 0x764);
static_assert(offsetof(MiniMapDB_t, texHolder) == 0x78C);
static_assert(offsetof(MiniMapDB_t, texEntries) == 0x794);

struct MiniMapTask_t {
  /* 0x000 */ u8 _pad000[0x06C];
  /* 0x06C */ be_u32 chromeTex;
  /* 0x070 */ u8 _pad070[0x08C - 0x070];
  /* 0x08C */ MiniMapDB_t baseFloor;
  /* 0x824 */ u8 _pad824[0x83C - 0x824];
  /* 0x83C */ mem::GuestVec<u32> floors;
  /* 0x848 */ be_u32 floor; // null until an area map loads
  /* 0x84C */ u8 _pad84C[0x850 - 0x84C];
  /* 0x850 */ be_u32 category;
  /* 0x854 */ be_u32 areaHi;
  /* 0x858 */ be_u32 areaLo;
};
static_assert(offsetof(MiniMapTask_t, chromeTex) == 0x06C);
static_assert(offsetof(MiniMapTask_t, baseFloor) == 0x08C);
static_assert(offsetof(MiniMapTask_t, floors) == 0x83C);
static_assert(offsetof(MiniMapTask_t, floor) == 0x848);
static_assert(offsetof(MiniMapTask_t, category) == 0x850);
static_assert(offsetof(MiniMapTask_t, areaHi) == 0x854);
static_assert(offsetof(MiniMapTask_t, areaLo) == 0x858);

// The sub-floor names run MM_<stage>_NN, so the engine list cannot outrun two
// digits. A wild begin and end pair would otherwise reserve its way to a throw
// inside a hook.
constexpr u32 kMaxFloors = 128;

} // namespace

bool MiniMapDB::Ready() const {
  const auto *self = Self<MiniMapDB_t>();
  return self && static_cast<u32>(self->texEntries) != 0 &&
         static_cast<f32>(self->texW) > 0.0f &&
         static_cast<f32>(self->texH) > 0.0f &&
         static_cast<f32>(self->scaleX) != 0.0f &&
         static_cast<f32>(self->scaleZ) != 0.0f;
}

u32 MiniMapDB::TexHolderAddress() const {
  const u32 address = Address();
  return address ? address + offsetof(MiniMapDB_t, texHolder) : 0;
}

f32 MiniMapDB::TexW() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->texW) : 0.0f;
}

f32 MiniMapDB::TexH() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->texH) : 0.0f;
}

f32 MiniMapDB::ScaleX() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->scaleX) : 0.0f;
}

f32 MiniMapDB::ScaleZ() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->scaleZ) : 0.0f;
}

f32 MiniMapDB::DispW() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->dispW) : 0.0f;
}

f32 MiniMapDB::DispH() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->dispH) : 0.0f;
}

f32 MiniMapDB::OffsetX() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->offsetX) : 0.0f;
}

f32 MiniMapDB::OffsetZ() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->offsetZ) : 0.0f;
}

f32 MiniMapDB::OffsetRot() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->offsetRot) : 0.0f;
}

f32 MiniMapDB::PlyRot() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->plyRot) : 0.0f;
}

f32 MiniMapDB::TexRot() const {
  const auto *self = Self<MiniMapDB_t>();
  return self ? static_cast<f32>(self->texRot) : 0.0f;
}

u32 MiniMapTask::ChromeTexAddress() const {
  const u32 address = Address();
  return address ? address + offsetof(MiniMapTask_t, chromeTex) : 0;
}

MiniMapDB MiniMapTask::BaseFloor() const {
  const u32 address = Address();
  return MiniMapDB(address ? address + offsetof(MiniMapTask_t, baseFloor) : 0);
}

MiniMapDB MiniMapTask::Floor() const {
  const auto *self = Self<MiniMapTask_t>();
  return MiniMapDB(self ? static_cast<u32>(self->floor) : 0);
}

std::vector<MiniMapDB> MiniMapTask::Floors() const {
  std::vector<MiniMapDB> out;
  const auto *self = Self<MiniMapTask_t>();
  if (!self)
    return out;
  const u32 count = std::min(self->floors.size(), kMaxFloors);
  out.reserve(count);
  for (u32 i = 0; i < count; ++i)
    out.emplace_back(self->floors[i]);
  return out;
}

u32 MiniMapTask::Category() const {
  const auto *self = Self<MiniMapTask_t>();
  return self ? static_cast<u32>(self->category) : 0;
}

void MiniMapTask::SetCategory(u32 category) {
  auto *self = Self<MiniMapTask_t>();
  if (self)
    self->category = category;
}

u32 MiniMapTask::AreaHi() const {
  const auto *self = Self<MiniMapTask_t>();
  return self ? static_cast<u32>(self->areaHi) : 0;
}

u32 MiniMapTask::AreaLo() const {
  const auto *self = Self<MiniMapTask_t>();
  return self ? static_cast<u32>(self->areaLo) : 0;
}

} // namespace bd::engine
