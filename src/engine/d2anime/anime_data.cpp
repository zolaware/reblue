/**
 * @file    engine/d2anime/anime_data.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_data.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/encoding.h"
#include "core/memory_helpers.h"

REX_IMPORT(__imp__AnimeVarBag_SetStringVar, VarBag_SetString,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetColorRGBA, VarBag_SetColor,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetFloatVar, VarBag_SetFloat,
           void(u32, u32, f64));
REX_IMPORT(__imp__AnimeVarBag_FindVar, VarBag_FindVar, u32(u32, u32));
REX_IMPORT(__imp__AnimeVar_PropagateStringToElements, Var_PropagateString,
           void(u32));
// std::wstring::assign(const wchar_t*, size_t), the tail of
// AnimeVar_SetWideString once its Shift-JIS conversion is done.
REX_IMPORT(__imp__WString__Assign, WString_Assign, u32(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarTrack_Apply, AnimeVarTrack_Apply, void(u32, f64));
REX_IMPORT(__imp__AnimeData_SyncDerivedVars, AnimeData_SyncDerivedVars,
           void(u32));

namespace bd::engine {

namespace {

constexpr u32 kMaxChainEntries = 4096;
constexpr u32 kAnimeVarTrackCap = 512;

// AnimeVar_t::type, the two the engine's own setters check before writing.
enum class AnimeVarType : u32 {
  Float = 2,
  String = 3,
};

// One entry of an AnimeVarBag, as AnimeVarBag_SetStringVar and
// AnimeVarBag_GetFloatVar read it.
struct AnimeVar_t {
  /* 0x00 */ u8 _pad000[0x04];
  /* 0x04 */ be_u32 type;
  /* 0x08 */ u8 _pad008[0x1C];
  // The scalar for a float var. A string var stores its std::wstring in the
  // same slot, so a wide assignment takes the address of this field rather
  // than its value.
  /* 0x24 */ be_f32 value;

  bool Is(AnimeVarType t) const {
    return static_cast<u32>(type) == static_cast<u32>(t);
  }
};
static_assert(offsetof(AnimeVar_t, type) == 0x04);
static_assert(offsetof(AnimeVar_t, value) == 0x24);

struct AnimeData_t;

struct AnimeElement_t {
  /* 0x00 */ u8 _pad00[0xC4];
  /* 0xC4 */ mem::GuestPtr<AnimeData_t> childAnime;
  /* 0xC8 */ be_f32 timer;
};
static_assert(offsetof(AnimeElement_t, childAnime) == 0xC4);
static_assert(offsetof(AnimeElement_t, timer) == 0xC8);

struct AnimeChainNode_t {
  /* 0x00 */ u8 _pad00[0x50];
  /* 0x50 */ mem::GuestVec<mem::GuestPtr<AnimeElement_t>> elements;
};
static_assert(offsetof(AnimeChainNode_t, elements) == 0x50);

struct AnimeData_t {
  /* 0x000 */ u8 _pad000[0xC0];
  /* 0x0C0 */ mem::GuestVec<u32> varTracks;
  /* 0x0CC */ u8 _pad0CC[0x138 - 0xCC];
  /* 0x138 */ be_f32 length;
  /* 0x13C */ be_u32 state;
  /* 0x140 */ u8 _pad140[0x150 - 0x140];
  /* 0x150 */ be_f32 frame;
  /* 0x154 */ be_f32 speed;
  /* 0x158 */ u8 _pad158[0x160 - 0x158];
  /* 0x160 */ be_u32 childEnabled;
  /* 0x164 */ u8 _pad164[0x178 - 0x164];
  /* 0x178 */ mem::GuestVec<mem::GuestPtr<AnimeChainNode_t>> activeChain;
};
static_assert(offsetof(AnimeData_t, varTracks) == 0x0C0);
static_assert(offsetof(AnimeData_t, length) == 0x138);
static_assert(offsetof(AnimeData_t, state) == 0x13C);
static_assert(offsetof(AnimeData_t, frame) == 0x150);
static_assert(offsetof(AnimeData_t, speed) == 0x154);
static_assert(offsetof(AnimeData_t, childEnabled) == 0x160);
static_assert(offsetof(AnimeData_t, activeChain) == 0x178);
static_assert(sizeof(AnimeData_t) == 0x184);

} // namespace

f32 AnimeData::Length() const {
  const auto *self = Self<AnimeData_t>();
  return self ? static_cast<f32>(self->length) : 0.0f;
}

f32 AnimeData::Frame() const {
  const auto *self = Self<AnimeData_t>();
  return self ? static_cast<f32>(self->frame) : 0.0f;
}

bool AnimeData::SetFrame(f32 v) {
  auto *self = Self<AnimeData_t>();
  if (!self)
    return false;
  self->frame = v;
  return true;
}

f32 AnimeData::Speed() const {
  const auto *self = Self<AnimeData_t>();
  return self ? static_cast<f32>(self->speed) : 0.0f;
}

bool AnimeData::SetSpeed(f32 v) {
  auto *self = Self<AnimeData_t>();
  if (!self)
    return false;
  self->speed = v;
  return true;
}

void AnimeData::SyncActiveChain() {
  auto *self = Self<AnimeData_t>();
  if (!self || self->activeChain.size() > kMaxChainEntries)
    return;
  const f32 clock = self->frame;
  for (u32 i = 0; i < self->activeChain.size(); ++i) {
    auto *node = self->activeChain[i].get();
    if (!node || node->elements.size() > kMaxChainEntries)
      continue;
    for (u32 j = 0; j < node->elements.size(); ++j) {
      auto *element = node->elements[j].get();
      if (!element)
        continue;
      element->timer = clock;
      if (auto *child = element->childAnime.get())
        child->childEnabled = 1;
    }
  }
}

void AnimeData::ApplyVarTracks(f32 frame) {
  const auto *self = Self<AnimeData_t>();
  if (!self)
    return;
  const auto &tracks = self->varTracks;
  const u32 count = std::min<u32>(tracks.size(), kAnimeVarTrackCap);
  for (u32 i = 0; i < count; ++i) {
    if (const u32 track = mem::try_load<u32>(tracks.address(i)))
      AnimeVarTrack_Apply(track, f64(frame));
  }
  if (count)
    AnimeData_SyncDerivedVars(Address());
}

void AnimeData::SetString(const char *name, const char *value) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  u32 valueAddr = rex::ppc::stack_push_string(value);
  VarBag_SetString(bag, nameAddr, valueAddr);
}

void AnimeData::SetTextU16(const char *name, std::u16string_view text) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  u32 entry = VarBag_FindVar(bag, nameAddr);
  auto *var = mem::at<AnimeVar_t>(entry);
  if (!var || !var->Is(AnimeVarType::String))
    return;

  std::vector<u16> be(text.size() + 1, 0);
  for (size_t i = 0; i < text.size(); ++i)
    be[i] = __builtin_bswap16(static_cast<u16>(text[i]));

  u32 buf = rex::ppc::stack_push(be.data(), static_cast<u32>(be.size() * 2));
  WString_Assign(entry + offsetof(AnimeVar_t, value), buf,
                 static_cast<u32>(text.size()));
  Var_PropagateString(entry);
}

void AnimeData::SetText(const char *name, std::string_view utf8) {
  SetTextU16(name, bd::Utf8ToU16(utf8));
}

void AnimeData::SetColor(const char *name, u32 rgba) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  VarBag_SetColor(bag, nameAddr, rgba);
}

void AnimeData::SetFloat(const char *name, double value) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  VarBag_SetFloat(bag, nameAddr, value);
}

std::optional<f32> AnimeData::Float(const char *name) const {
  const u32 bag = Address();
  if (!bag)
    return std::nullopt;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  const auto *var = mem::at<AnimeVar_t>(VarBag_FindVar(bag, nameAddr));
  if (!var || !var->Is(AnimeVarType::Float))
    return std::nullopt;
  return static_cast<f32>(var->value);
}

} // namespace bd::engine
