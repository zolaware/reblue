/**
 * @file    engine/d2anime/anime_data.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_data.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>
#include <rex/ppc/stack.h>
#include <rex/kernel/types/memory.h>
#include <rex/types.h>
#include <rex/string/text.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_var.h"

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
REX_IMPORT(__imp__bdTextCalcWidth, TextCalcWidth, f64(f64, u32, u32, u32));

namespace bd::engine {

namespace {

constexpr u32 kMaxChainEntries = 4096;
constexpr u32 kAnimeVarTrackCap = 512;

constexpr u32 kDefaultFont = 1;
constexpr u32 kWholeText = 0xFFFFFFFFu;
constexpr std::u16string_view kEllipsis = u"...";

struct AnimeData_t;

struct AnimeElement_t {
  /* 0x00 */ u8 _pad00[0xC4];
  /* 0xC4 */ rex::kernel::be_ptr<AnimeData_t> childAnime;
  /* 0xC8 */ be_f32 timer;
};
static_assert(offsetof(AnimeElement_t, childAnime) == 0xC4);
static_assert(offsetof(AnimeElement_t, timer) == 0xC8);

struct AnimeChainNode_t {
  /* 0x00 */ u8 _pad00[0x50];
  /* 0x50 */ rex::kernel::GuestVector<rex::kernel::be_ptr<AnimeElement_t>> elements;
};
static_assert(offsetof(AnimeChainNode_t, elements) == 0x50);

struct AnimeData_t {
  /* 0x000 */ u8 _pad000[0xC0];
  /* 0x0C0 */ rex::kernel::GuestVector<u32> varTracks;
  /* 0x0CC */ u8 _pad0CC[0x138 - 0xCC];
  /* 0x138 */ be_f32 length;
  /* 0x13C */ be_u32 state;
  /* 0x140 */ u8 _pad140[0x150 - 0x140];
  /* 0x150 */ be_f32 frame;
  /* 0x154 */ be_f32 speed;
  /* 0x158 */ u8 _pad158[0x160 - 0x158];
  /* 0x160 */ be_u32 childEnabled;
  /* 0x164 */ u8 _pad164[0x178 - 0x164];
  /* 0x178 */ rex::kernel::GuestVector<rex::kernel::be_ptr<AnimeChainNode_t>> activeChain;
};
static_assert(offsetof(AnimeData_t, varTracks) == 0x0C0);
static_assert(offsetof(AnimeData_t, length) == 0x138);
static_assert(offsetof(AnimeData_t, state) == 0x13C);
static_assert(offsetof(AnimeData_t, frame) == 0x150);
static_assert(offsetof(AnimeData_t, speed) == 0x154);
static_assert(offsetof(AnimeData_t, childEnabled) == 0x160);
static_assert(offsetof(AnimeData_t, activeChain) == 0x178);
static_assert(sizeof(AnimeData_t) == 0x184);

f32 TextWidth(std::u16string_view text, int fontWidth) {
  rex::ppc::StackFrame frame;
  const u32 addr = frame.PushWideString(text).GetGuestAddress();
  return static_cast<f32>(TextCalcWidth(static_cast<f64>(fontWidth), addr,
                                        kDefaultFont, kWholeText));
}

size_t FittingPrefix(std::u16string_view word, int fontWidth, f32 maxWidth) {
  size_t fits = 1;
  size_t tooLong = word.size() + 1;
  while (tooLong - fits > 1) {
    const size_t middle = (fits + tooLong) / 2;
    if (TextWidth(word.substr(0, middle), fontWidth) <= maxWidth)
      fits = middle;
    else
      tooLong = middle;
  }
  return fits;
}

void WrapParagraph(std::u16string_view paragraph, int fontWidth, f32 maxWidth,
                   std::vector<std::u16string> &lines) {
  std::u16string line;
  size_t start = 0;
  while (start <= paragraph.size()) {
    size_t end = paragraph.find(u' ', start);
    if (end == std::u16string_view::npos)
      end = paragraph.size();
    std::u16string_view word = paragraph.substr(start, end - start);
    start = end + 1;

    std::u16string joined = line;
    if (!joined.empty())
      joined += u' ';
    joined += word;
    if (TextWidth(joined, fontWidth) <= maxWidth) {
      line = std::move(joined);
      continue;
    }
    if (!line.empty())
      lines.push_back(std::move(line));
    while (TextWidth(word, fontWidth) > maxWidth) {
      const size_t cut = FittingPrefix(word, fontWidth, maxWidth);
      lines.emplace_back(word.substr(0, cut));
      word.remove_prefix(cut);
    }
    line = std::u16string(word);
  }
  lines.push_back(std::move(line));
}

std::u16string FitWithEllipsis(std::u16string_view text, int fontWidth,
                               f32 maxWidth) {
  const auto shortened = [&](size_t length) {
    std::u16string cut(text.substr(0, length));
    while (!cut.empty() && cut.back() == u' ')
      cut.pop_back();
    cut += kEllipsis;
    return cut;
  };
  size_t fits = 0;
  size_t tooLong = text.size();
  while (tooLong - fits > 1) {
    const size_t middle = (fits + tooLong) / 2;
    if (TextWidth(shortened(middle), fontWidth) <= maxWidth)
      fits = middle;
    else
      tooLong = middle;
  }
  return shortened(fits);
}

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
  const auto view = mem::view();
  if (!view)
    return;
  const f32 clock = self->frame;
  for (u32 i = 0; i < self->activeChain.size(); ++i) {
    const auto *nodeSlot = self->activeChain.TryAt(i, *view);
    auto *node = nodeSlot ? nodeSlot->TryGetHost(*view) : nullptr;
    if (!node || node->elements.size() > kMaxChainEntries)
      continue;
    for (u32 j = 0; j < node->elements.size(); ++j) {
      const auto *elementSlot = node->elements.TryAt(j, *view);
      auto *element = elementSlot ? elementSlot->TryGetHost(*view) : nullptr;
      if (!element)
        continue;
      element->timer = clock;
      if (auto *child = element->childAnime.TryGetHost(*view))
        child->childEnabled = 1;
    }
  }
}

void AnimeData::ApplyVarTracks(f32 frame) {
  const auto *self = Self<AnimeData_t>();
  if (!self)
    return;
  const auto &tracks = self->varTracks;
  const u32 count =
      std::min<u32>(static_cast<u32>(tracks.size()), kAnimeVarTrackCap);
  for (u32 i = 0; i < count; ++i) {
    if (const u32 track = mem::try_load<u32>(tracks.GetAddress(i)))
      AnimeVarTrack_Apply(track, f64(frame));
  }
  if (count)
    AnimeData_SyncDerivedVars(Address());
}

void AnimeData::SetString(const char *name, const char *value) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::StackFrame frame;
  u32 nameAddr = frame.PushString(name).GetGuestAddress();
  u32 valueAddr = frame.PushString(value).GetGuestAddress();
  VarBag_SetString(bag, nameAddr, valueAddr);
}

void AnimeData::SetTextU16(const char *name, std::u16string_view text) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::StackFrame frame;
  u32 nameAddr = frame.PushString(name).GetGuestAddress();
  u32 entry = VarBag_FindVar(bag, nameAddr);
  auto *var = mem::at<AnimeVar_t>(entry);
  if (!var || !var->Is(AnimeVarType::String))
    return;

  u32 buf = frame.PushWideString(text).GetGuestAddress();
  WString_Assign(entry + offsetof(AnimeVar_t, value), buf,
                 static_cast<u32>(text.size()));
  Var_PropagateString(entry);
}

void AnimeData::SetText(const char *name, std::string_view utf8) {
  SetTextU16(name, rex::string::ToUTF16(utf8));
}

std::vector<std::string> AnimeData::WrapText(std::string_view utf8,
                                             int fontWidth, f32 maxWidth,
                                             size_t maxLines) {
  const std::u16string text = rex::string::ToUTF16(utf8);
  std::vector<std::u16string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(u'\n', start);
    if (end == std::u16string::npos)
      end = text.size();
    WrapParagraph(std::u16string_view(text).substr(start, end - start),
                  fontWidth, maxWidth, lines);
    start = end + 1;
  }

  if (maxLines > 0 && lines.size() > maxLines) {
    std::u16string rest;
    for (size_t i = maxLines - 1; i < lines.size(); ++i) {
      if (!rest.empty())
        rest += u' ';
      rest += lines[i];
    }
    lines.resize(maxLines);
    lines.back() = FitWithEllipsis(rest, fontWidth, maxWidth);
  }

  std::vector<std::string> out;
  out.reserve(lines.size());
  for (const std::u16string &line : lines)
    out.push_back(rex::string::ToUTF8(line));
  return out;
}

void AnimeData::SetColor(const char *name, u32 rgba) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::StackFrame frame;
  u32 nameAddr = frame.PushString(name).GetGuestAddress();
  VarBag_SetColor(bag, nameAddr, rgba);
}

void AnimeData::SetFloat(const char *name, double value) {
  const u32 bag = Address();
  if (!bag)
    return;
  rex::ppc::StackFrame frame;
  u32 nameAddr = frame.PushString(name).GetGuestAddress();
  VarBag_SetFloat(bag, nameAddr, value);
}

std::optional<f32> AnimeData::Float(const char *name) const {
  const u32 bag = Address();
  if (!bag)
    return std::nullopt;
  rex::ppc::StackFrame frame;
  u32 nameAddr = frame.PushString(name).GetGuestAddress();
  const auto *var = mem::at<AnimeVar_t>(VarBag_FindVar(bag, nameAddr));
  if (!var || !var->Is(AnimeVarType::Float))
    return std::nullopt;
  return static_cast<f32>(var->value);
}

} // namespace bd::engine
