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

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
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

constexpr int kSheetCols = int(Glyphs::kSheetCols);
constexpr int kSheetRows = int(Glyphs::kSheetRows);

// follows, in kBindableKeys order, then the arrow cluster and the three
// modifier caps close the run.
constexpr int kKeyCellBase = 32;
constexpr int kClusterCell = kKeyCellBase + int(platform::kBindableKeyCount);
constexpr int kModCellBase = kClusterCell + 1;
constexpr int kModCellCount = 3;

constexpr int kPadSetBase = kModCellBase + kModCellCount;
constexpr int kPadStickPressCell = Glyphs::kHelpCells;
constexpr int kPadDpadCell = kPadStickPressCell + 2;
constexpr int kPadStickDirCell = kPadDpadCell + 4;
constexpr int kPadSetCells = kPadStickDirCell + 8;
static_assert(kPadSetBase + (kPadSetLast + 1) * kPadSetCells <=
              kSheetCols * kSheetRows);

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

constexpr u32 kBlockEdge = 4;

u32 Expand565(u16 c, int shift, int bits) {
  const u32 v = (c >> shift) & ((1u << bits) - 1u);
  return v * 255u / ((1u << bits) - 1u);
}

// One DXT5 block, already in little-endian order, into a 4x4 RGBA patch.
void DecodeBC3Block(const u8 *block, u8 *dst, u32 dstPitchPx) {
  u8 alpha[8] = {block[0], block[1]};
  if (alpha[0] > alpha[1]) {
    for (int i = 0; i < 6; ++i)
      alpha[2 + i] = u8(((6 - i) * alpha[0] + (i + 1) * alpha[1]) / 7);
  } else {
    for (int i = 0; i < 4; ++i)
      alpha[2 + i] = u8(((4 - i) * alpha[0] + (i + 1) * alpha[1]) / 5);
    alpha[6] = 0;
    alpha[7] = 255;
  }
  u64 alphaBits = 0;
  for (int i = 0; i < 6; ++i)
    alphaBits |= u64(block[2 + i]) << (8 * i);

  const u16 c0 = u16(block[8] | (block[9] << 8));
  const u16 c1 = u16(block[10] | (block[11] << 8));
  const u32 colorBits = u32(block[12]) | (u32(block[13]) << 8) |
                        (u32(block[14]) << 16) | (u32(block[15]) << 24);
  u32 color[4][3];
  for (int ch = 0; ch < 3; ++ch) {
    constexpr int kShift[3] = {11, 5, 0};
    constexpr int kBits[3] = {5, 6, 5};
    const u32 a = Expand565(c0, kShift[ch], kBits[ch]);
    const u32 b = Expand565(c1, kShift[ch], kBits[ch]);
    color[0][ch] = a;
    color[1][ch] = b;
    color[2][ch] = (2 * a + b) / 3;
    color[3][ch] = (a + 2 * b) / 3;
  }

  for (u32 t = 0; t < kBlockEdge * kBlockEdge; ++t) {
    u8 *px = dst + (size_t(t / kBlockEdge) * dstPitchPx + t % kBlockEdge) * 4;
    const u32 *rgb = color[(colorBits >> (2 * t)) & 3];
    px[0] = u8(rgb[0]);
    px[1] = u8(rgb[1]);
    px[2] = u8(rgb[2]);
    px[3] = alpha[(alphaBits >> (3 * t)) & 7];
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

// Position of a pad button's art inside one pad set's block.
int PadSetIndex(int padButton) {
  constexpr int kPadLS = 6, kPadRS = 7, kPadLSUp = int(Button::LSUp);
  if (padButton >= int(Button::Up) && padButton <= int(Button::Right))
    return kPadDpadCell + padButton - int(Button::Up);
  if (padButton == kPadLS || padButton == kPadRS)
    return kPadStickPressCell + padButton - kPadLS;
  if (padButton >= kPadLSUp && padButton < kPadLSUp + 8)
    return kPadStickDirCell + padButton - kPadLSUp;
  for (int i = 0; i < Glyphs::kHelpCells; ++i) {
    if (kCells[i].padButton >= 0 && kCells[i].padButton == padButton)
      return i;
  }
  return -1;
}

int PadGlyphIndex(Action action) {
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.kind == SourceKind::PadButton)
      return PadSetIndex(int(s.code));
  }
  return -1;
}

int PadSetCell(PadSet pad, int idx) {
  return kPadSetBase + static_cast<int>(pad) * kPadSetCells + idx;
}

int PadArtCell(PadSet pad, int cellIndex) {
  const GlyphCell &c = kCells[cellIndex];
  const int idx = c.action == kNoAction ? cellIndex : PadGlyphIndex(c.action);
  return idx < 0 ? kBlankCell : PadSetCell(pad, idx);
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

std::vector<u8> Glyphs::SheetPixels() {
  namespace tc = rex::graphics::texture_conversion;
  namespace tu = rex::graphics::texture_util;
  constexpr auto kSheet = bd::Embedded("glyphs/cmn_help_menue.dds");
  if (kSheet.size <= kSheetPayload)
    return {};
  const u8 *src = kSheet.data + kSheetPayload;
  const size_t bytes = kSheet.size - kSheetPayload;

  constexpr u32 kWidth = kSheetCols * kSheetCellPx;
  constexpr u32 kHeight = kSheetRows * kSheetCellPx;
  std::vector<u8> rgba(size_t(kWidth) * kHeight * 4, 0);
  for (u32 by = 0; by < kHeight / kBlockEdge; ++by) {
    for (u32 bx = 0; bx < kWidth / kBlockEdge; ++bx) {
      const i32 off = tu::GetTiledOffset2D(i32(bx), i32(by), kSheetBlockPitch,
                                           kSheetBlockLog2);
      if (off < 0 || size_t(off) + kSheetBlockBytes > bytes)
        continue;
      u8 block[kSheetBlockBytes];
      tc::CopySwapBlock(rex::graphics::xenos::Endian::k8in16, block, src + off,
                        kSheetBlockBytes);
      DecodeBC3Block(
          block,
          rgba.data() + (size_t(by) * kBlockEdge * kWidth + bx * kBlockEdge) * 4,
          kWidth);
    }
  }
  return rgba;
}

int Glyphs::PadSheetCell(int padButton) const {
  const int idx = PadSetIndex(padButton);
  return idx < 0 ? -1 : PadSetCell(pad_, idx);
}

bool Glyphs::PadButtonUV(int padButton, UVRect &uv) const {
  const int cell = PadSheetCell(padButton);
  if (cell < 0)
    return false;
  const UVRect c = CellRect(cell);
  const f32 w = c.u1 - c.u0;
  const f32 h = c.v1 - c.v0;
  uv = {c.u0 + kInkX0 * w, c.v0 + kInkY0 * h, c.u0 + kInkX1 * w,
        c.v0 + kInkY1 * h};
  return true;
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
