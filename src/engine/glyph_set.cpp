/**
 * @file    engine/glyph_set.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/glyph_set.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <rex/graphics/pipeline/texture/util.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/ui/keybinds.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mount.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/input/actions.h"
#include "engine/input/binding_store.h"
#include "engine/prompt_textures.h"
#include "engine/settings.h"
#include "engine/virtual_buttons.h"
#include "embedded.h"
#include "gpu/gpu.h"
#include "platform/platform.h"
#include "reblue_init.h"

REX_IMPORT(__imp__AnimeVarBag_FindVar, GlyphFindVar, u32(u32, u32));
// 0x8215D008, the element analogue of AnimeVar_ApplyFloatKeyframes: it pushes
// all five of an element's floats through the same per-value apply the engine
// runs after writing them. CampRank_ApplySlotUVs writes a UV rect and then
// calls this, so a write without it is a write the readers may never see.
// Named by the recompiler, and renaming it in functions.toml would cost a full
// rebuild for nothing.
REX_IMPORT(__imp__AnimeVar_nearby_D008, GlyphCommitVar, u32(u32));
REX_EXTERN(__imp__bdUiCharUvCacheInit);

namespace bd::engine {

namespace {

// The character UV cache, whose tail holds the one d2anime\Uv.csv bag that
// every 'uv.' name in the game resolves against.
constexpr u32 kUiCharUvCache = 0x82DC98B4;
constexpr u32 kUvBagOffset = 440;

// An AnimeVar entry: type at +4, and for an element the five floats
// AnimeVarBag_GetElementXYWHP hands back. Uv.csv puts a UV rect in the first
// four, so 'w' is really u1 and 'h' is v1.
constexpr u32 kVarType = 0x04;
constexpr u32 kVarU0 = 0x28;
constexpr u32 kVarV0 = 0x2C;
constexpr u32 kVarU1 = 0x30;
constexpr u32 kVarV1 = 0x34;
constexpr u32 kVarTypeElement = 0;

// Cell grid of the served sheet.
constexpr int kSheetCols = 8;
constexpr int kSheetRows = 24;

// The shipped controller block is pasted into the top-left quadrant, so its
// eleven cells keep the arrangement Uv.csv describes. One cell per bindable key
// follows, in kBindableKeys order, then the arrow cluster and the three
// modifier caps close the run.
constexpr int kKeyCellBase = 32;
constexpr int kClusterCell = kKeyCellBase + int(platform::kBindableKeyCount);
constexpr int kModCellBase = kClusterCell + 1;
constexpr int kModCellCount = 3;

// One block of the eleven prompts per pad the player can pick, in PadSet
// order from XboxSeries on. The 360 has no block: its art is the shipped one
// already sitting in the cells Uv.csv names.
constexpr int kPadSetBase = kModCellBase + kModCellCount;
constexpr int kPadSetCells = Glyphs::kHelpCells;
static_assert(kPadSetBase + kPadSetLast * kPadSetCells <=
              kSheetCols * kSheetRows);

// The shipped block fills only the left half of its four rows, so the right
// half is transparent and pointing a prompt at it draws nothing. That is the
// honest answer for a button with no key bound to it.
constexpr int kBlankCell = kSheetCols - 1;

constexpr Action kNoAction = Action::Count;
constexpr int kNoPadButton = -1;

struct GlyphCell {
  const char *name;
  int padCell;
  int padButton;
  Action action;
};

constexpr GlyphCell kCells[] = {
    {"Help_A_Uv", 0, int(Button::A), Action::Confirm},
    {"Help_B_Uv", 1, int(Button::B), Action::Cancel},
    {"Help_X_Uv", 2, int(Button::X), Action::Attack},
    {"Help_Y_Uv", 3, int(Button::Y), Action::MainMenu},
    {"Help_BACK_Uv", 8, int(Button::Back), Action::Back},
    {"Help_CROSS_Uv", 9, kNoPadButton, kNoAction},
    {"Help_LB_Uv", 16, int(Button::LB), Action::FieldSkill2},
    {"Help_RB_Uv", 17, int(Button::RB), Action::FieldSkill1},
    {"Help_LT_Uv", 18, int(Button::LT), Action::ResetCamera},
    {"Help_RT_Uv", 19, int(Button::RT), Action::FieldMenu},
    {"Help_STARTUv", 24, int(Button::Start), Action::WorldMap},
};
static_assert(int(sizeof(kCells) / sizeof(kCells[0])) == Glyphs::kHelpCells);

// Longest name is Help_CROSS_Uv at thirteen characters.
constexpr u32 kNameStride = 16;

constexpr const char *kSheetMount = "ui:glyph-sheet";
constexpr const char *kSheetKey = "d2anime\\res\\cmn_help_menue.dds";

UVRect CellRect(int cell) {
  const f32 u0 = f32(cell % kSheetCols) / f32(kSheetCols);
  const f32 v0 = f32(cell / kSheetCols) / f32(kSheetRows);
  return {u0, v0, u0 + 1.0f / f32(kSheetCols), v0 + 1.0f / f32(kSheetRows)};
}

// The sheet ships as tiled DXT5: a 64px cell is 16x16 blocks of 16 bytes, so a
// cell moves between grid slots as raw block copies through the same offsets
// the payload was tiled with. No decode, and the shipped block stays pristine
// in its own cells.
constexpr size_t kSheetPayload = 2048;
constexpr u32 kSheetBlockPitch = 128;
constexpr u32 kCellBlocks = 16;
constexpr u32 kSheetBlockBytes = 16;
constexpr u32 kSheetBlockLog2 = 4;

void BlitSheetCell(u8 *dst, const u8 *src, size_t payloadBytes, int dstCell,
                   int srcCell) {
  namespace tu = rex::graphics::texture_util;
  const i32 sx = i32(srcCell % kSheetCols) * i32(kCellBlocks);
  const i32 sy = i32(srcCell / kSheetCols) * i32(kCellBlocks);
  const i32 dx = i32(dstCell % kSheetCols) * i32(kCellBlocks);
  const i32 dy = i32(dstCell / kSheetCols) * i32(kCellBlocks);
  for (i32 by = 0; by < i32(kCellBlocks); ++by) {
    for (i32 bx = 0; bx < i32(kCellBlocks); ++bx) {
      const i32 so = tu::GetTiledOffset2D(sx + bx, sy + by, kSheetBlockPitch,
                                          kSheetBlockLog2);
      const i32 dof = tu::GetTiledOffset2D(dx + bx, dy + by, kSheetBlockPitch,
                                           kSheetBlockLog2);
      if (so < 0 || dof < 0 || size_t(so) + kSheetBlockBytes > payloadBytes ||
          size_t(dof) + kSheetBlockBytes > payloadBytes)
        continue;
      std::memcpy(dst + dof, src + so, kSheetBlockBytes);
    }
  }
}

// The cell whose art a prompt should show on a keyboard: the cap of the bound
// key, the arrow cluster for the D-pad, or the blank half-cell for a button
// nobody bound.
int ArtCell(const GlyphCell &c) {
  if (c.action == kNoAction)
    return kClusterCell;
  const int key = BoundKeyIndex(c.action);
  return key < 0 ? kBlankCell : kKeyCellBase + key;
}

int PadGlyphIndex(Action action) {
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.kind != SourceKind::PadButton)
      continue;
    for (int i = 0; i < Glyphs::kHelpCells; ++i) {
      if (kCells[i].padButton == int(s.code))
        return i;
    }
    return -1;
  }
  return -1;
}

int PadArtCell(PadSet pad, int cellIndex) {
  const GlyphCell &c = kCells[cellIndex];
  const int idx = c.action == kNoAction ? cellIndex : PadGlyphIndex(c.action);
  if (idx < 0)
    return kBlankCell;
  if (pad == PadSet::Xbox360)
    return kCells[idx].padCell;
  return kPadSetBase + (static_cast<int>(pad) - 1) * kPadSetCells + idx;
}

// Where a cap inks inside its 64px cell, and the wider band the modifier cells
// get.
constexpr f32 kInkX0 = 6.0f / 64.0f;
constexpr f32 kInkX1 = 44.0f / 64.0f;
constexpr f32 kInkY0 = 12.0f / 64.0f;
constexpr f32 kInkY1 = 52.0f / 64.0f;
constexpr f32 kModInkX0 = 4.0f / 64.0f;
constexpr f32 kModInkX1 = 60.0f / 64.0f;

// The connected pad's own art, for PadSet::Auto. A pad the host cannot place
// gets the 360's block, which is the one the disc ships.
PadSet HostPadSet() {
  switch (platform::ConnectedPad()) {
  case platform::PadBrand::XboxSeries:
    return PadSet::XboxSeries;
  case platform::PadBrand::PlayStation:
    return PadSet::PlayStation;
  case platform::PadBrand::Switch:
    return PadSet::Switch;
  case platform::PadBrand::SteamDeck:
    return PadSet::SteamDeck;
  default:
    return PadSet::Xbox360;
  }
}

const char *HelpNameForAction(Action action) {
  switch (ActionButton(action)) {
  case Button::A:
    return "Help_A_Uv";
  case Button::B:
    return "Help_B_Uv";
  case Button::X:
    return "Help_X_Uv";
  case Button::Y:
    return "Help_Y_Uv";
  default:
    return nullptr;
  }
}

} // namespace

int KeyIndex(std::string_view keyName) {
  for (size_t i = 0; i < platform::kBindableKeyCount; ++i)
    if (keyName == platform::kBindableKeys[i])
      return int(i);
  return -1;
}

int BoundKeyIndex(Action action) {
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.kind != SourceKind::Key)
      continue;
    const std::string name =
        rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(s.code));
    return name.empty() ? -1 : KeyIndex(name);
  }
  return -1;
}

const char *ToString(GlyphSet set) {
  switch (set) {
  case GlyphSet::Auto:
    return "auto";
  case GlyphSet::Controller:
    return "controller";
  case GlyphSet::Keyboard:
    return "keyboard";
  }
  return "?";
}

const char *ToString(PadSet set) {
  switch (set) {
  case PadSet::Auto:
    return "auto";
  case PadSet::Xbox360:
    return "xbox360";
  case PadSet::XboxSeries:
    return "xbox";
  case PadSet::PlayStation:
    return "playstation";
  case PadSet::Switch:
    return "switch";
  case PadSet::SteamDeck:
    return "steamdeck";
  }
  return "?";
}

Glyphs &Glyphs::Get() {
  static Glyphs s;
  return s;
}

void Glyphs::InitOnce() {
  if (started_)
    return;
  started_ = true;
  autoPad_ = HostPadSet();

  LayoutMount mount;
  mount.AddRaw(kSheetKey, [] { return Glyphs::Get().ComposeSheet(); });
  mount.Publish(kSheetMount);
  PromptTextures::Get().Publish();
  bindGeneration_ = Bindings::Get().Generation();
}

void Glyphs::Rebind() {
  bound_ = false;
  const u32 cache = mem::try_load<u32>(kUiCharUvCache);
  if (!cache)
    return;
  const u32 bag = cache + kUvBagOffset;

  // One engine block for the eleven names, allocated once and reused across
  // every camp visit. FindVar takes an engine pointer, so the names have to live
  // where the engine can read them.
  if (!nameBlock_) {
    nameBlock_ =
        gpu::HostHeap::Get().AllocGuest(kNameStride * kHelpCells, 16);
    if (!nameBlock_) {
      BD_WARN("[glyphs] no engine memory for cell names, prompts stay stock");
      return;
    }
    for (int i = 0; i < kHelpCells; ++i) {
      char *dst = mem::try_at<char>(nameBlock_ + u32(i) * kNameStride);
      if (dst)
        std::snprintf(dst, kNameStride, "%s", kCells[i].name);
    }
  }

  for (int i = 0; i < kHelpCells; ++i) {
    const u32 va = GlyphFindVar(bag, nameBlock_ + u32(i) * kNameStride);
    vars_[i] = (va && mem::try_load<u32>(va + kVarType) == kVarTypeElement)
                   ? va
                   : 0;
  }
  bound_ = true;
  Apply();
}

void Glyphs::WriteCell(u32 va, int cell) const {
  const UVRect r = CellRect(cell);
  mem::try_store<f32>(va + kVarU0, r.u0);
  mem::try_store<f32>(va + kVarV0, r.v0);
  mem::try_store<f32>(va + kVarU1, r.u1);
  mem::try_store<f32>(va + kVarV1, r.v1);
  GlyphCommitVar(va);
}

UVRect Glyphs::PromptUV(const PromptGlyph &glyph) const {
  const char *name = nullptr;
  if (std::strcmp(glyph.helpName, "Help_A_Uv") == 0)
    name = HelpNameForAction(Action::Confirm);
  else if (std::strcmp(glyph.helpName, "Help_B_Uv") == 0)
    name = HelpNameForAction(Action::Cancel);
  return CellUV(name ? name : glyph.helpName);
}

UVRect Glyphs::CellUV(const char *helpName) const {
  for (const GlyphCell &c : kCells) {
    if (std::strcmp(c.name, helpName) == 0)
      return CellRect(c.padCell);
  }
  return {};
}

UVRect Glyphs::KeyArtUV(int keyIndex) {
  const int cell = keyIndex < 0 ? kBlankCell : kKeyCellBase + keyIndex;
  const UVRect c = CellRect(cell);
  const f32 w = c.u1 - c.u0;
  const f32 h = c.v1 - c.v0;
  return {c.u0 + kInkX0 * w, c.v0 + kInkY0 * h, c.u0 + kInkX1 * w,
          c.v0 + kInkY1 * h};
}

int Glyphs::ModifierIndex(std::string_view prefix) {
  if (!prefix.empty() && prefix.back() == '+')
    prefix.remove_suffix(1);
  if (prefix == "Shift")
    return 0;
  if (prefix == "Ctrl")
    return 1;
  if (prefix == "Alt")
    return 2;
  return -1;
}

UVRect Glyphs::ModifierArtUV(int modIndex) {
  const int cell = (modIndex < 0 || modIndex >= kModCellCount)
                       ? kBlankCell
                       : kModCellBase + modIndex;
  const UVRect c = CellRect(cell);
  const f32 w = c.u1 - c.u0;
  const f32 h = c.v1 - c.v0;
  return {c.u0 + kModInkX0 * w, c.v0 + kInkY0 * h, c.u0 + kModInkX1 * w,
          c.v0 + kInkY1 * h};
}

void Glyphs::Apply() {
  ++generation_;
  if (!bound_)
    return;
  // Always the pad cell. The shipped rects assume the disc's 4x4 sheet, so
  // the bag has to be rewritten onto this sheet's grid, but the values never
  // move again: what a device change moves is the texels under them, which is
  // the only write that reaches CSVs the engine already parsed.
  for (int i = 0; i < kHelpCells; ++i) {
    if (vars_[i])
      WriteCell(vars_[i], kCells[i].padCell);
  }
}

std::vector<u8> Glyphs::ComposeSheet() const {
  constexpr auto kSheet = bd::Embedded("glyphs/cmn_help_menue.dds");
  std::vector<u8> blob(kSheet.data, kSheet.data + kSheet.size);
  if (blob.size() <= kSheetPayload)
    return blob;
  const bool keyboard = resolved_ == GlyphSet::Keyboard;
  u8 *payload = blob.data() + kSheetPayload;
  const u8 *src = kSheet.data + kSheetPayload;
  const size_t bytes = blob.size() - kSheetPayload;
  for (int i = 0; i < kHelpCells; ++i) {
    const GlyphCell &c = kCells[i];
    const int art = keyboard ? ArtCell(c) : PadArtCell(pad_, i);
    if (art != c.padCell)
      BlitSheetCell(payload, src, bytes, c.padCell, art);
  }
  return blob;
}

GlyphSet Glyphs::Wanted() const {
  const auto chosen = static_cast<GlyphSet>(Settings::Get().GlyphSetMode());
  return chosen == GlyphSet::Auto ? lastDevice_ : chosen;
}

PadSet Glyphs::WantedPad() const {
  const auto chosen = static_cast<PadSet>(Settings::Get().PadGlyphSet());
  return chosen == PadSet::Auto ? autoPad_ : chosen;
}

void Glyphs::Tick() {
  InitOnce();

  // Drained every tick whether or not it is read, so a press made while the
  // keyboard was also busy cannot claim the next quiet frame.
  const bool pad = TakePadInputSeen();
  const bool keyboard = platform::Keyboard().AnyDown() ||
                        platform::Mouse().AnyButtonDown();

  if (keyboard) {
    lastDevice_ = GlyphSet::Keyboard;
  } else if (pad) {
    lastDevice_ = GlyphSet::Controller;
    autoPad_ = HostPadSet();
    // A genuine pad press takes the menu pointer with it, wherever the cursor
    // stood. Mouse motion hands it back through MenuMouse::BeginFrame.
    MenuMouse::Get().SetMouseHasCursor(false);
  }

  const GlyphSet want = Wanted();
  const PadSet wantPad = WantedPad();
  const u32 bindGen = Bindings::Get().Generation();
  if (want != resolved_ || wantPad != pad_ || bindGen != bindGeneration_) {
    resolved_ = want;
    pad_ = wantPad;
    bindGeneration_ = bindGen;
    Apply();
  }

  // The providers cover every load made from here on, the stamps rewrite the
  // instances already in engine memory, including one that raced this very
  // change through the loader.
  sheetStamp_.Sync(kSheetKey, generation_, [this] { return ComposeSheet(); });
  PromptTextures::Get().Sync(generation_);
}

} // namespace bd::engine

// The one moment the Uv.csv bag exists and nothing has read a Help cell out of
// it yet: bdUiCharUvCacheAcquire polls the load to completion and calls this
// before handing the cache to anyone.
REX_HOOK_RAW(bdUiCharUvCacheInit) {
  __imp__bdUiCharUvCacheInit(ctx, base);
  bd::engine::Glyphs::Get().Rebind();
}
