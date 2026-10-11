/**
 * @file    engine/scene_file.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/scene_file.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

constexpr u32 kSceneEntryNameCap = 24;
constexpr u32 kInstructionHeaderBytes = 8;
constexpr u32 kInstructionMaxBytes = 512;

struct SceneFile_t {
  /* 0x000 */ u8 _pad000[0x2AC];
  /* 0x2AC */ be_u32 entries;
};
static_assert(offsetof(SceneFile_t, entries) == 0x2AC);

struct SceneEntry_t {
  /* 0x00 */ be_u32 id;
  /* 0x04 */ char name[kSceneEntryNameCap];
  /* 0x1C */ u8 _pad1C[0x24 - 0x1C];
  /* 0x24 */ be_u32 type;
  /* 0x28 */ be_f32 x;
  /* 0x2C */ be_f32 y;
  /* 0x30 */ be_f32 z;
  /* 0x34 */ u8 _pad34[0x64 - 0x34];
  /* 0x64 */ be_u32 blockCount;
  /* 0x68 */ be_u32 blocks;
  /* 0x6C */ be_u32 next;
};
static_assert(offsetof(SceneEntry_t, id) == 0x00);
static_assert(offsetof(SceneEntry_t, name) == 0x04);
static_assert(offsetof(SceneEntry_t, type) == 0x24);
static_assert(offsetof(SceneEntry_t, x) == 0x28);
static_assert(offsetof(SceneEntry_t, y) == 0x2C);
static_assert(offsetof(SceneEntry_t, z) == 0x30);
static_assert(offsetof(SceneEntry_t, blockCount) == 0x64);
static_assert(offsetof(SceneEntry_t, blocks) == 0x68);
static_assert(offsetof(SceneEntry_t, next) == 0x6C);
static_assert(sizeof(SceneEntry_t) == 0x70);

struct SceneCondition_t {
  /* 0x00 */ be_u32 type;
  /* 0x04 */ be_u32 operand;
  /* 0x08 */ be_u32 op;
  /* 0x0C */ be_u32 value;
};
static_assert(sizeof(SceneCondition_t) == 0x10);

struct SceneBlock_t {
  /* 0x000 */ be_i32 chapterMin;
  /* 0x004 */ be_i32 chapterMax;
  /* 0x008 */ SceneCondition_t conditions[kSceneConditionCount];
  /* 0x088 */ u8 _pad088[0xE4 - 0x88];
  /* 0x0E4 */ be_u32 typeData;
  /* 0x0E8 */ u8 _pad0E8[0xEC - 0xE8];
  /* 0x0EC */ be_u32 bytecodeSize;
  /* 0x0F0 */ u8 _pad0F0[0xF4 - 0xF0];
  /* 0x0F4 */ be_u32 bytecode;
  /* 0x0F8 */ u8 _pad0F8[0xFC - 0xF8];
  /* 0x0FC */ be_u32 next;
};
static_assert(offsetof(SceneBlock_t, chapterMin) == 0x000);
static_assert(offsetof(SceneBlock_t, chapterMax) == 0x004);
static_assert(offsetof(SceneBlock_t, conditions) == 0x008);
static_assert(offsetof(SceneBlock_t, typeData) == 0x0E4);
static_assert(offsetof(SceneBlock_t, bytecodeSize) == 0x0EC);
static_assert(offsetof(SceneBlock_t, bytecode) == 0x0F4);
static_assert(offsetof(SceneBlock_t, next) == 0x0FC);
static_assert(sizeof(SceneBlock_t) == 0x100);

struct SceneInstruction_t {
  /* 0x00 */ be_u32 opcode;
  /* 0x04 */ be_u32 size;
};
static_assert(sizeof(SceneInstruction_t) == kInstructionHeaderBytes);

struct SearchPoint_t {
  /* 0x000 */ u8 _pad000[0x08];
  /* 0x008 */ be_f32 x;
  /* 0x00C */ be_f32 y;
  /* 0x010 */ be_f32 z;
  /* 0x014 */ u8 _pad014[0xA8 - 0x14];
  /* 0x0A8 */ be_i32 flag;
  /* 0x0AC */ be_u32 kind;
  /* 0x0B0 */ u8 _pad0B0[0x114 - 0xB0];
};
static_assert(offsetof(SearchPoint_t, x) == 0x008);
static_assert(offsetof(SearchPoint_t, y) == 0x00C);
static_assert(offsetof(SearchPoint_t, z) == 0x010);
static_assert(offsetof(SearchPoint_t, flag) == 0x0A8);
static_assert(offsetof(SearchPoint_t, kind) == 0x0AC);
static_assert(sizeof(SearchPoint_t) == SearchPoint::kStride);

} // namespace

i32 SceneBlock::ChapterMin() const {
  const auto *self = Self<SceneBlock_t>();
  return self ? static_cast<i32>(self->chapterMin) : 0;
}

i32 SceneBlock::ChapterMax() const {
  const auto *self = Self<SceneBlock_t>();
  return self ? static_cast<i32>(self->chapterMax) : 0;
}

SceneCondition SceneBlock::Condition(u32 index) const {
  const auto *self = Self<SceneBlock_t>();
  if (!self || index >= kSceneConditionCount)
    return {};
  const SceneCondition_t &c = self->conditions[index];
  return SceneCondition{static_cast<u32>(c.type), static_cast<u32>(c.operand),
                        static_cast<u32>(c.op), static_cast<u32>(c.value)};
}

u32 SceneBlock::TypeData() const {
  const auto *self = Self<SceneBlock_t>();
  return self ? static_cast<u32>(self->typeData) : 0;
}

SceneBlock SceneBlock::Next() const {
  const auto *self = Self<SceneBlock_t>();
  return SceneBlock(self ? static_cast<u32>(self->next) : 0);
}

void SceneBlock::ForEachInstruction(
    const std::function<void(const SceneInstruction &)> &fn) const {
  const auto *self = Self<SceneBlock_t>();
  if (!self)
    return;
  const u32 base = static_cast<u32>(self->bytecode);
  const u32 total = static_cast<u32>(self->bytecodeSize);
  if (!base || total < kInstructionHeaderBytes)
    return;

  for (u32 off = 0; total - off >= kInstructionHeaderBytes;) {
    const auto *head = mem::try_at<const SceneInstruction_t>(base + off);
    if (!head)
      return;
    const u32 size = static_cast<u32>(head->size);
    if (size < kInstructionHeaderBytes || size > kInstructionMaxBytes ||
        size > total - off)
      return;

    const u32 words = (size - kInstructionHeaderBytes) / sizeof(u32);
    const be_u32 *params = nullptr;
    if (words) {
      params = mem::try_at<const be_u32>(base + off + kInstructionHeaderBytes);
      if (!params)
        return;
    }
    fn(SceneInstruction{static_cast<u32>(head->opcode),
                        std::span<const be_u32>(params, words)});
    off += size;
  }
}

u32 SceneEntry::Id() const {
  const auto *self = Self<SceneEntry_t>();
  return self ? static_cast<u32>(self->id) : 0;
}

SceneEntryType SceneEntry::Type() const {
  const auto *self = Self<SceneEntry_t>();
  return static_cast<SceneEntryType>(self ? static_cast<u32>(self->type) : 0);
}

Vec3 SceneEntry::Position() const {
  const auto *self = Self<SceneEntry_t>();
  if (!self)
    return {};
  return Vec3{static_cast<f32>(self->x), static_cast<f32>(self->y),
              static_cast<f32>(self->z)};
}

u32 SceneEntry::BlockCount() const {
  const auto *self = Self<SceneEntry_t>();
  return self ? static_cast<u32>(self->blockCount) : 0;
}

SceneBlock SceneEntry::FirstBlock() const {
  const auto *self = Self<SceneEntry_t>();
  return SceneBlock(self ? static_cast<u32>(self->blocks) : 0);
}

SceneEntry SceneEntry::Next() const {
  const auto *self = Self<SceneEntry_t>();
  return SceneEntry(self ? static_cast<u32>(self->next) : 0);
}

SceneEntry SceneFile::FirstEntry() const {
  const auto *self = Self<SceneFile_t>();
  return SceneEntry(self ? static_cast<u32>(self->entries) : 0);
}

Vec3 SearchPoint::Position() const {
  const auto *self = Self<SearchPoint_t>();
  if (!self)
    return {};
  return Vec3{static_cast<f32>(self->x), static_cast<f32>(self->y),
              static_cast<f32>(self->z)};
}

i32 SearchPoint::Flag() const {
  const auto *self = Self<SearchPoint_t>();
  return self ? static_cast<i32>(self->flag) : -1;
}

u32 SearchPoint::Kind() const {
  const auto *self = Self<SearchPoint_t>();
  return self ? static_cast<u32>(self->kind) : 0;
}

} // namespace bd::engine
