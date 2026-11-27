/**
 * @file    engine/glyph_set.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/glyph_set.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>

#include "core/guest_global.h"
#include <string_view>
#include <vector>

#include <rex/xenos/texture.h>
#include <rex/logging/log_category.h>
#include <rex/kernel/memory_view.h>
#include <rex/xenos/format.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>
#include <rex/input_keys.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_var.h"
#include "engine/d2anime/anime_mount.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/input/actions.h"
#include "engine/input/binding_registry.h"
#include "engine/input/input_sources.h"
#include "engine/prompt_textures.h"
#include "core/cvar_table.h"
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

namespace bd::engine {

namespace {

// The character UV cache, whose tail holds the one d2anime\Uv.csv bag that
// every 'uv.' name in the game resolves against.
REX_GUEST_GLOBAL(be_u32, kUiCharUvCache, 0x82DC98B4);
constexpr u32 kUvBagOffset = 440;

// Slots of AnimeVar_t::element that hold the UV rect.
constexpr int kElemU0 = 0;
constexpr int kElemV0 = 1;
constexpr int kElemU1 = 2;
constexpr int kElemV1 = 3;

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
    {"Help_X_Uv", 2, int(Button::X), Action::VoiceLanguage},
    {"Help_Y_Uv", 3, int(Button::Y), Action::MainMenu},
    {"Help_BACK_Uv", 8, int(Button::Back), Action::QuitMenu},
    {"Help_CROSS_Uv", 9, kNoPadButton, kNoAction},
    {"Help_LB_Uv", 16, int(Button::LB), Action::PrevPage},
    {"Help_RB_Uv", 17, int(Button::RB), Action::NextPage},
    {"Help_LT_Uv", 18, int(Button::LT), Action::FirstItem},
    {"Help_RT_Uv", 19, int(Button::RT), Action::LastItem},
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
  namespace tu = rex::xenos;
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

constexpr f32 kCapGap = 0.08f;
constexpr double kCapShown = 255.0;
constexpr double kCapHidden = 0.0;
constexpr int kMinPairCapPx = 10;

struct ChordVarNames {
  std::string one, two, mod, key;
  std::string modX, modY, modW, modH;
  std::string keyX, keyY, keyW, keyH;
};

ChordVarNames ChordVars(std::string_view posVar) {
  ChordVarNames n;
  n.one = std::format("{}One", posVar);
  n.two = std::format("{}Two", posVar);
  n.mod = std::format("{}Mod", posVar);
  n.key = std::format("{}Key", posVar);
  n.modX = n.mod + ".x";
  n.modY = n.mod + ".y";
  n.modW = n.mod + ".w";
  n.modH = n.mod + ".h";
  n.keyX = n.key + ".x";
  n.keyY = n.key + ".y";
  n.keyW = n.key + ".w";
  n.keyH = n.key + ".h";
  return n;
}
constexpr size_t kRowsAlwaysDrawn = 2;
constexpr f32 kMinRowFill = 0.5f;

void DrawCapImage(const CapImage &cap, u8 *dst, u32 dstPitchPx,
                  const CapBox &box, f32 dim) {
  const CapBox &src = cap.ink;
  if (!src.w || !src.h)
    return;

  const f32 scale =
      std::min(f32(box.w) / f32(src.w), f32(box.h) / f32(src.h));
  const u32 outW = std::max(1u, u32(f32(src.w) * scale + 0.5f));
  const u32 outH = std::max(1u, u32(f32(src.h) * scale + 0.5f));
  const u32 left = box.x + (box.w - std::min(outW, box.w)) / 2;
  const u32 top = box.y + (box.h - std::min(outH, box.h)) / 2;

  for (u32 y = 0; y < outH; ++y) {
    const u32 sy0 = src.h * y / outH;
    const u32 sy1 = std::max(sy0 + 1, src.h * (y + 1) / outH);
    for (u32 x = 0; x < outW; ++x) {
      const u32 sx0 = src.w * x / outW;
      const u32 sx1 = std::max(sx0 + 1, src.w * (x + 1) / outW);

      u32 acc[4] = {0, 0, 0, 0};
      u32 n = 0;
      for (u32 sy = sy0; sy < sy1; ++sy) {
        for (u32 sx = sx0; sx < sx1; ++sx) {
          const u8 *px = &cap.rgba[(size_t(src.y + sy) * cap.pitchPx +
                                    src.x + sx) *
                                   4];
          acc[0] += u32(px[0]) * px[3];
          acc[1] += u32(px[1]) * px[3];
          acc[2] += u32(px[2]) * px[3];
          acc[3] += px[3];
          ++n;
        }
      }
      if (!n || !acc[3])
        continue;

      u8 *out = &dst[(size_t(top + y) * dstPitchPx + left + x) * 4];
      for (int c = 0; c < 3; ++c) {
        const f32 v = f32(acc[c]) / f32(acc[3]) * dim;
        out[c] = u8(std::min(255.0f, v));
      }
      out[3] = u8(acc[3] / n);
    }
  }
}

CapBox CellInk(const std::vector<u8> &rgba, u32 pitchPx, int cell) {
  constexpr u32 kPx = Glyphs::kSheetCellPx;
  const u32 ox = u32(cell % kSheetCols) * kPx;
  const u32 oy = u32(cell / kSheetCols) * kPx;
  u32 x0 = kPx, y0 = kPx, x1 = 0, y1 = 0;
  for (u32 y = 0; y < kPx; ++y) {
    for (u32 x = 0; x < kPx; ++x) {
      if (!rgba[(size_t(oy + y) * pitchPx + ox + x) * 4 + 3])
        continue;
      x0 = std::min(x0, x);
      y0 = std::min(y0, y);
      x1 = std::max(x1, x + 1);
      y1 = std::max(y1, y + 1);
    }
  }
  if (x1 <= x0 || y1 <= y0)
    return {ox, oy, 0, 0};
  return {ox + x0, oy + y0, x1 - x0, y1 - y0};
}

u16 Pack565(const u8 *px) {
  return u16(((px[0] * 31 + 127) / 255) << 11 |
             ((px[1] * 63 + 127) / 255) << 5 | ((px[2] * 31 + 127) / 255));
}

u32 Luma(const u8 *px) { return u32(px[0]) * 2 + u32(px[1]) * 4 + px[2]; }

void EncodeBC3Block(const u8 *src, u32 srcPitchPx, u8 *block) {
  const u8 *px[16];
  for (u32 t = 0; t < 16; ++t)
    px[t] = src + (size_t(t / kBlockEdge) * srcPitchPx + t % kBlockEdge) * 4;

  u8 aMin = 255, aMax = 0;
  const u8 *bright = px[0];
  const u8 *dark = px[0];
  for (const u8 *p : px) {
    aMin = std::min(aMin, p[3]);
    aMax = std::max(aMax, p[3]);
    if (Luma(p) > Luma(bright))
      bright = p;
    if (Luma(p) < Luma(dark))
      dark = p;
  }

  u8 alpha[8] = {aMax, aMin};
  for (int i = 0; i < 6; ++i)
    alpha[2 + i] = u8(((6 - i) * aMax + (i + 1) * aMin) / 7);
  u64 alphaBits = 0;
  for (u32 t = 0; t < 16; ++t) {
    u32 best = 0, bestErr = 256;
    for (u32 i = 0; i < 8; ++i) {
      const u32 err = u32(std::abs(int(px[t][3]) - int(alpha[i])));
      if (err < bestErr) {
        bestErr = err;
        best = i;
      }
    }
    alphaBits |= u64(aMax == aMin ? 0 : best) << (3 * t);
  }

  u16 c0 = Pack565(bright);
  u16 c1 = Pack565(dark);
  if (c0 < c1)
    std::swap(c0, c1);
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
  u32 colorBits = 0;
  for (u32 t = 0; t < 16; ++t) {
    u32 best = 0, bestErr = ~0u;
    for (u32 i = 0; i < 4 && c0 != c1; ++i) {
      u32 err = 0;
      for (int ch = 0; ch < 3; ++ch) {
        const int d = int(px[t][ch]) - int(color[i][ch]);
        err += u32(d * d);
      }
      if (err < bestErr) {
        bestErr = err;
        best = i;
      }
    }
    colorBits |= best << (2 * t);
  }

  block[0] = alpha[0];
  block[1] = alpha[1];
  for (int i = 0; i < 6; ++i)
    block[2 + i] = u8(alphaBits >> (8 * i));
  block[8] = u8(c0);
  block[9] = u8(c0 >> 8);
  block[10] = u8(c1);
  block[11] = u8(c1 >> 8);
  for (int i = 0; i < 4; ++i)
    block[12 + i] = u8(colorBits >> (8 * i));
}

void WriteSheetCell(u8 *payload, size_t payloadBytes, int cell,
                    const u8 *rgba) {
  namespace tu = rex::xenos;
  const i32 cx = i32(cell % kSheetCols) * i32(kCellBlocks);
  const i32 cy = i32(cell / kSheetCols) * i32(kCellBlocks);
  for (u32 by = 0; by < kCellBlocks; ++by) {
    for (u32 bx = 0; bx < kCellBlocks; ++bx) {
      const i32 off = tu::GetTiledOffset2D(cx + i32(bx), cy + i32(by),
                                           kSheetBlockPitch, kSheetBlockLog2);
      if (off < 0 || size_t(off) + kSheetBlockBytes > payloadBytes)
        continue;
      u8 block[kSheetBlockBytes];
      EncodeBC3Block(rgba + (size_t(by) * kBlockEdge * Glyphs::kSheetCellPx +
                             bx * kBlockEdge) *
                                4,
                     Glyphs::kSheetCellPx, block);
      tu::CopySwapBlock(rex::xenos::Endian::k8in16, payload + off, block,
                        kSheetBlockBytes);
    }
  }
}

// The cell whose art a prompt should show on a keyboard: the cap of the bound
// key, the arrow cluster for the D-pad, or the blank half-cell for a button
// nobody bound.
int ArtCell(const GlyphCell &c) {
  if (c.action == kNoAction)
    return kClusterCell;
  const int key = BoundChord(c.action).key;
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
    if (s.type == SourceKind::kPadButton)
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
constexpr f32 kCellPx = f32(Glyphs::kSheetCellPx);
constexpr f32 kInkX0 = f32(Glyphs::kKeyInkX0Px) / kCellPx;
constexpr f32 kInkX1 = f32(Glyphs::kKeyInkX1Px) / kCellPx;
constexpr f32 kInkY0 = f32(Glyphs::kInkY0Px) / kCellPx;
constexpr f32 kInkY1 = f32(Glyphs::kInkY1Px) / kCellPx;
constexpr f32 kModInkX0 = f32(Glyphs::kModInkX0Px) / kCellPx;
constexpr f32 kModInkX1 = f32(Glyphs::kModInkX1Px) / kCellPx;

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

int IndexOfKey(rex::VirtualKey key) {
  const auto *begin = std::begin(platform::kBindableKeys);
  const auto *end = std::end(platform::kBindableKeys);
  const auto *found = std::find(begin, end, key);
  return found == end ? -1 : static_cast<int>(found - begin);
}

} // namespace

int KeyIndex(const rex::InputSource &source) {
  if (source.type != SourceKind::kKey)
    return -1;
  return IndexOfKey(static_cast<rex::VirtualKey>(source.code));
}

ChordCaps BoundChord(Action action) {
  constexpr rex::KeyModifiers kOrder[] = {rex::KeyModifiers::kShift,
                                          rex::KeyModifiers::kCtrl,
                                          rex::KeyModifiers::kAlt};
  ChordCaps out;
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.type != SourceKind::kKey)
      continue;
    out.key = KeyIndex(s);
    for (const rex::KeyModifiers mod : kOrder) {
      if ((u8(s.modifiers) & u8(mod)) != 0)
        out.mods[out.modCount++] = Glyphs::ModifierIndex(mod);
    }
    return out;
  }
  return out;
}

bool DrawCapRow(std::span<const CapImage> caps, u8 *dst, u32 dstPitchPx,
                const CapBox &box, f32 dim) {
  f32 width = 0.0f;
  u32 tallest = 0;
  for (const CapImage &cap : caps) {
    width += f32(cap.ink.w);
    tallest = std::max(tallest, cap.ink.h);
  }
  if (width <= 0.0f || !tallest || !box.w || !box.h)
    return false;
  const f32 gaps = kCapGap * f32(tallest) * f32(caps.size() - 1);
  const f32 scale = std::min(f32(box.w) / (width + gaps),
                             f32(box.h) / f32(tallest));
  if (caps.size() > kRowsAlwaysDrawn &&
      f32(tallest) * scale < f32(box.h) * kMinRowFill)
    return false;

  f32 x = f32(box.x) + (f32(box.w) - (width + gaps) * scale) * 0.5f;
  for (const CapImage &cap : caps) {
    const u32 w = std::max(1u, u32(f32(cap.ink.w) * scale + 0.5f));
    DrawCapImage(cap, dst, dstPitchPx, {u32(x), box.y, w, box.h}, dim);
    x += f32(cap.ink.w) * scale + kCapGap * f32(tallest) * scale;
  }
  return true;
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
  const u32 cache = mem::global_load(kUiCharUvCache);
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
      REXBD_WARN("glyphs no engine memory for cell names, prompts stay stock");
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
    const auto *var = mem::try_at<const AnimeVar_t>(va);
    vars_[i] = (var && var->Is(AnimeVarType::Element)) ? va : 0;
  }
  bound_ = true;
  Apply();
}

void Glyphs::WriteCell(u32 va, int cell) const {
  const UVRect r = CellRect(cell);
  if (auto *var = mem::try_at<AnimeVar_t>(va)) {
    var->element[kElemU0] = r.u0;
    var->element[kElemV0] = r.v0;
    var->element[kElemU1] = r.u1;
    var->element[kElemV1] = r.v1;
  }
  GlyphCommitVar(va);
}

UVRect Glyphs::ActionUV(Action action) const {
  if (action == kNoAction) {
    for (const GlyphCell &c : kCells) {
      if (c.action == kNoAction)
        return CellRect(c.padCell);
    }
    return CellRect(kBlankCell);
  }
  if (resolved_ == GlyphSet::Keyboard) {
    const int key = BoundChord(action).key;
    return CellRect(key < 0 ? kBlankCell : kKeyCellBase + key);
  }
  const int idx = PadGlyphIndex(action);
  return CellRect(idx < 0 ? kBlankCell : PadSetCell(pad_, idx));
}

bool Glyphs::ChordUV(Action action, UVRect &mod, UVRect &key) const {
  if (resolved_ != GlyphSet::Keyboard || action == kNoAction)
    return false;
  const ChordCaps chord = BoundChord(action);
  if (chord.key < 0 || chord.modCount != 1)
    return false;
  mod = ModifierArtUV(chord.mods[0]);
  key = KeyArtUV(chord.key);
  return true;
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
  namespace tc = rex::xenos;
  namespace tu = rex::xenos;
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
      tc::CopySwapBlock(rex::xenos::Endian::k8in16, block, src + off,
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

int Glyphs::ModifierIndex(rex::KeyModifiers mods) {
  switch (mods) {
  case rex::KeyModifiers::kShift:
    return 0;
  case rex::KeyModifiers::kCtrl:
    return 1;
  case rex::KeyModifiers::kAlt:
    return 2;
  default:
    return -1;
  }
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
    if (keyboard && c.action != kNoAction) {
      const ChordCaps chord = BoundChord(c.action);
      if (chord.key >= 0 && chord.modCount > 0) {
        ComposeChordCell(payload, bytes, c.padCell, chord);
        continue;
      }
    }
    const int art = keyboard ? ArtCell(c) : PadArtCell(pad_, i);
    if (art != c.padCell)
      BlitSheetCell(payload, src, bytes, c.padCell, art);
  }
  return blob;
}

void Glyphs::DeclareChordCap(CsvBuilder &b, const char *posVar) {
  const ChordVarNames n = ChordVars(posVar);
  b.var(FloatV{n.one.c_str(), kCapShown});
  b.var(FloatV{n.two.c_str(), kCapHidden});
  b.pos(AnimePos{n.mod.c_str(), 0, 0, 0, 0});
  b.pos(AnimePos{n.key.c_str(), 0, 0, 0, 0});
}

void Glyphs::EmitChordCap(CsvBuilder &b, const AnimeTexAbs &single,
                          const char *posVar) {
  constexpr int kModUnits = int(kModInkX1Px - kModInkX0Px);
  constexpr int kKeyUnits = int(kKeyInkX1Px - kKeyInkX0Px);
  constexpr int kTallUnits = int(kInkY1Px - kInkY0Px);
  constexpr int kCell = int(kSheetCellPx);
  const int bandX = single.x + single.w * int(kKeyInkX0Px) / kCell;
  const int bandW = single.w * kKeyUnits / kCell;
  const int bandY = single.y + single.h * int(kInkY0Px) / kCell;
  const int bandH = single.h * kTallUnits / kCell;
  const f32 scale =
      std::min(f32(bandW) / f32(kModUnits + kKeyUnits),
               f32(bandH) / f32(kTallUnits));
  const int modW = int(f32(kModUnits) * scale + 0.5f);
  const int keyW = int(f32(kKeyUnits) * scale + 0.5f);
  const int h = int(f32(kTallUnits) * scale + 0.5f);
  const int left = bandX + (bandW - modW - keyW) / 2;
  const int top = bandY + (bandH - h) / 2;
  if (h < kMinPairCapPx) {
    b.tex(single);
    return;
  }

  const ChordVarNames n = ChordVars(posVar);
  AnimeTexAbs one = single;
  one.alphaExpr = n.one.c_str();
  b.tex(one);

  const HelpUv modUv = {n.modX.c_str(), n.modY.c_str(), n.modW.c_str(),
                        n.modH.c_str()};
  const HelpUv keyUv = {n.keyX.c_str(), n.keyY.c_str(), n.keyW.c_str(),
                        n.keyH.c_str()};
  AnimeTexAbs mod = single;
  mod.x = left;
  mod.y = top;
  mod.w = modW;
  mod.h = h;
  mod.alphaExpr = n.two.c_str();
  mod.helpUv = &modUv;
  b.tex(mod);
  AnimeTexAbs key = mod;
  key.x = left + modW;
  key.w = keyW;
  key.helpUv = &keyUv;
  b.tex(key);
}

void Glyphs::PushChordCap(D2AnimeTask &task, const char *posVar,
                          const HelpUv &uv, Action action) const {
  const auto set = [&](const char *u0, const char *v0, const char *u1,
                       const char *v1, const UVRect &r) {
    task.SetFloat(u0, r.u0);
    task.SetFloat(v0, r.v0);
    task.SetFloat(u1, r.u1);
    task.SetFloat(v1, r.v1);
  };
  set(uv.u0, uv.v0, uv.u1, uv.v1, ActionUV(action));

  const ChordVarNames n = ChordVars(posVar);
  UVRect mod{}, key{};
  const bool pair = ChordUV(action, mod, key);
  if (pair) {
    set(n.modX.c_str(), n.modY.c_str(), n.modW.c_str(), n.modH.c_str(), mod);
    set(n.keyX.c_str(), n.keyY.c_str(), n.keyW.c_str(), n.keyH.c_str(), key);
  }
  task.SetFloat(n.one.c_str(), pair ? kCapHidden : kCapShown);
  task.SetFloat(n.two.c_str(), pair ? kCapShown : kCapHidden);
}

void Glyphs::ComposeChordCell(u8 *payload, size_t bytes, int cell,
                              const ChordCaps &chord) const {
  std::lock_guard lock(sheetMutex_);
  if (sheetRgba_.empty())
    sheetRgba_ = SheetPixels();
  if (sheetRgba_.empty())
    return;

  constexpr u32 kPitch = kSheetCols * kSheetCellPx;
  const auto image = [&](int art) {
    return CapImage{sheetRgba_.data(), kPitch,
                    CellInk(sheetRgba_, kPitch, art)};
  };
  CapImage caps[kMaxChordModifiers + 1];
  size_t count = 0;
  for (int i = 0; i < chord.modCount; ++i)
    caps[count++] = image(kModCellBase + chord.mods[i]);
  caps[count++] = image(kKeyCellBase + chord.key);

  std::vector<u8> rgba(size_t(kSheetCellPx) * kSheetCellPx * 4, 0);
  const CapBox box = {kKeyInkX0Px, kInkY0Px, kKeyInkX1Px - kKeyInkX0Px,
                      kInkY1Px - kInkY0Px};
  const std::span<const CapImage> row(caps, count);
  if (!DrawCapRow(row, rgba.data(), kSheetCellPx, box, 1.0f))
    DrawCapRow(row.last(1), rgba.data(), kSheetCellPx, box, 1.0f);
  WriteSheetCell(payload, bytes, cell, rgba.data());
}

GlyphSet Glyphs::Wanted() const {
  const auto chosen = static_cast<GlyphSet>(bd::cvars::Get().glyph_set.Get());
  return chosen == GlyphSet::Auto ? lastDevice_ : chosen;
}

PadSet Glyphs::WantedPad() const {
  const auto chosen = static_cast<PadSet>(bd::cvars::Get().glyph_pad.Get());
  return chosen == PadSet::Auto ? autoPad_ : chosen;
}

void Glyphs::Tick() {
  InitOnce();

  // Drained every tick whether or not it is read, so a press made while the
  // keyboard was also busy cannot claim the next quiet frame.
  const bool pad = TakePadInputSeen();
  const InputSources &in = InputSources::Get();
  const bool keyboard =
      (in.KeyboardEnabled() && platform::HID().IsAnyKeyDown()) ||
      (in.MouseEnabled() && platform::HID().IsAnyButtonDown());

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
REX_DETOUR(bdUiCharUvCacheInit, u32, (u32 cache)) {
  const u32 result = original(cache);
  bd::engine::Glyphs::Get().Rebind();
  return result;
}
