/**
 * @file    engine/prompt_textures.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/prompt_textures.h"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <span>
#include <vector>

#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/midasm.h>
#include <stb_image.h>

#include "core/logging.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mount.h"
#include "engine/glyph_set.h"
#include "engine/guest_texture.h"
#include "engine/input/actions.h"
#include "engine/input/binding_registry.h"
#include "engine/live_texture_stamp.h"
#include "embedded.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

// The cap library beside the footer sheet: one 128px cell per bindable key in
// kBindableKeys order, then the arrow cluster, then four face buttons per pad
// in PadSet order, then LB and RB per pad in the same order.
constexpr u32 kLibCell = 128;
constexpr u32 kLibCols = 8;
constexpr u32 kPadCellBase = u32(bd::platform::kBindableKeyCount) + 1;
constexpr u32 kPadFaceButtons = 4;
constexpr u32 kPadShoulderBase =
    kPadCellBase + u32(kPadSetLast + 1) * kPadFaceButtons;
constexpr u32 kPadShoulderButtons = 2;
constexpr u32 kLibPadCellEnd =
    kPadShoulderBase + u32(kPadSetLast + 1) * kPadShoulderButtons;
constexpr u32 kLibModCellBase = kLibPadCellEnd;

constexpr const char *kMount = "ui:prompt-textures";

// How far a pressed frame is dimmed. The stock art redraws the button pushed in
// and the CSVs slide it three pixels down on the same frame, so the drop is what
// has to read here, not the shading.
constexpr float kPressedDim = 0.62f;

struct PromptCell {
  Action action;
  bool pressed;
};

// Where a cell's art sits inside the cell, measured off the shipped textures:
// every 64px prompt inks 38x40 at (13,15), and the QTE button inks 172x92 at
// (42,14) of its 256x128. Matching those keeps a cap where the art it replaces
// was, the position the surrounding layouts are laid out against.
using InkBox = CapBox;

constexpr InkBox kInk64 = {13, 15, 38, 40};
constexpr InkBox kInkActEv = {42, 14, 172, 92};
constexpr InkBox kInkShoulder = {1, 1, 36, 43};

// One served texture: the file the engine opens, its shipped dimensions, its cell
// grid and the cells in row-major order.
struct PromptTex {
  const char *key;
  u32 width, height;
  u32 cols, rows;
  InkBox ink;
  const PromptCell *cells;
  u32 cellCount;
};

constexpr PromptCell kCellA[] = {{Action::Confirm, false}};
constexpr PromptCell kCellAPsh[] = {{Action::Confirm, true}};
constexpr PromptCell kCellB[] = {{Action::Cancel, false}};
constexpr PromptCell kCellBPsh[] = {{Action::Cancel, true}};
constexpr PromptCell kCellX[] = {{Action::Attack, false}};
constexpr PromptCell kCellXPsh[] = {{Action::Attack, true}};
constexpr PromptCell kCellQteX[] = {{Action::QteAction1, false}};
constexpr PromptCell kCellQteXPsh[] = {{Action::QteAction1, true}};
constexpr PromptCell kCellQteY[] = {{Action::QteAction2, false}};
constexpr PromptCell kCellQteYPsh[] = {{Action::QteAction2, true}};

// The menu pager pair, LB then RB.
constexpr PromptCell kCellsPager[] = {{Action::PrevPage, false},
                                      {Action::NextPage, false}};

// ic_btn's own pos rows, normal in the left column and pressed in the right:
// posAnr and posApsh, then posBnr and posBpsh, the row kInteractRowButton
// selects.
constexpr PromptCell kCellsIcBtn[] = {
    {Action::Confirm, false}, {Action::Confirm, true},
    {Action::Interact, false}, {Action::Interact, true}};

constexpr u32 kInteractRowButton = u32(Button::B);

#define BD_PROMPT_TEX(path, w, h, cols, rows, ink, cells)                      \
  { path, w, h, cols, rows, ink, cells, u32(std::size(cells)) }

constexpr PromptTex kTextures[] = {
    // The field interact prompt, the loading icon, the save icon and the scene
    // transition, all through d2anime\icon\ic_abtn\ic_abtn.csv.
    BD_PROMPT_TEX("d2anime\\icon\\ic_abtn\\ic_abtn_nr.dds", 64, 64, 1, 1, kInk64,
                  kCellA),
    BD_PROMPT_TEX("d2anime\\icon\\ic_abtn\\ic_abtn_psh.dds", 64, 64, 1, 1,
                  kInk64, kCellAPsh),
    BD_PROMPT_TEX("d2anime\\icon\\ic_abtn\\ic_xbtn_nr.dds", 64, 64, 1, 1, kInk64,
                  kCellX),
    BD_PROMPT_TEX("d2anime\\icon\\ic_abtn\\ic_xbtn_psh.dds", 64, 64, 1, 1,
                  kInk64, kCellXPsh),

    // The script prompts, the script window, the battle charge prompt and the
    // carriage, all through d2anime\icon\ic_btn\ic_btn.csv.
    BD_PROMPT_TEX("d2anime\\icon\\ic_btn\\ic_btn.dds", 128, 256, 2, 4, kInk64,
                  kCellsIcBtn),

    // The SCA copies of both, which the cutscene system loads from its own tree.
    BD_PROMPT_TEX("sca\\common\\res\\ic_abtn_nr.dds", 64, 64, 1, 1, kInk64,
                  kCellA),
    BD_PROMPT_TEX("sca\\common\\res\\ic_abtn_psh.dds", 64, 64, 1, 1, kInk64,
                  kCellAPsh),
    BD_PROMPT_TEX("sca\\common\\res\\ic_btn.dds", 128, 256, 2, 4, kInk64,
                  kCellsIcBtn),

    // The character pager on the shop's equipment screens and every camp
    // screen that reuses L_shp_lbrb.csv.
    BD_PROMPT_TEX("d2anime\\shp\\res\\ic_shp_lbrb.dds", 128, 64, 2, 1,
                  kInkShoulder, kCellsPager),

    // The action event QTEs, one texture per button per state.
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_a_nrm.dds", 256, 128, 1,
                  1, kInkActEv, kCellA),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_a_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellAPsh),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_b_nrm.dds", 256, 128, 1,
                  1, kInkActEv, kCellB),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_b_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellBPsh),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_x_nrm.dds", 256, 128, 1,
                  1, kInkActEv, kCellQteX),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_x_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellQteXPsh),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_y_nrm.dds", 256, 128, 1,
                  1, kInkActEv, kCellQteY),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_y_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellQteYPsh),
};

#undef BD_PROMPT_TEX

// The decoded cap library, with each cell's ink box found once so a cap can be
// fit to a destination without rescanning it.
struct CapLibrary {
  std::vector<u8> rgba;
  u32 width = 0, height = 0;
  u32 cellPx = 0, cols = 0;
  std::vector<InkBox> ink;
};

std::mutex s_mutex;
CapLibrary s_lib;
bool s_libTried = false;
CapLibrary s_sheet;
bool s_sheetTried = false;

// Alpha bounds of one library cell, in cell-local coordinates. A cell with no
// art at all comes back zero-sized and is skipped rather than drawn as a dot.
InkBox ScanCell(const CapLibrary &lib, u32 cell) {
  const u32 ox = (cell % lib.cols) * lib.cellPx;
  const u32 oy = (cell / lib.cols) * lib.cellPx;
  u32 x0 = lib.cellPx, y0 = lib.cellPx, x1 = 0, y1 = 0;
  for (u32 y = 0; y < lib.cellPx; ++y) {
    for (u32 x = 0; x < lib.cellPx; ++x) {
      const u8 a = lib.rgba[(size_t(oy + y) * lib.width + ox + x) * 4 + 3];
      if (!a)
        continue;
      x0 = std::min(x0, x);
      y0 = std::min(y0, y);
      x1 = std::max(x1, x + 1);
      y1 = std::max(y1, y + 1);
    }
  }
  if (x1 <= x0 || y1 <= y0)
    return {0, 0, 0, 0};
  return {x0, y0, x1 - x0, y1 - y0};
}

void ScanCells(CapLibrary &lib) {
  const u32 cells = (lib.width / lib.cellPx) * (lib.height / lib.cellPx);
  lib.ink.resize(cells);
  for (u32 i = 0; i < cells; ++i)
    lib.ink[i] = ScanCell(lib, i);
}

// Guarded because a VFS provider runs on whichever thread opened the file, and
// the first prompt to load is what decodes the library.
const CapLibrary *Library() {
  if (s_libTried)
    return s_lib.width ? &s_lib : nullptr;
  s_libTried = true;

  constexpr auto kCaps = bd::Embedded("glyphs/key_caps.png");
  int w = 0, h = 0, channels = 0;
  stbi_uc *pixels = stbi_load_from_memory(kCaps.data, int(kCaps.size), &w, &h,
                                          &channels, 4);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels)
      stbi_image_free(pixels);
    REXBD_ERROR("prompts the key cap library did not decode");
    return nullptr;
  }

  s_lib.width = u32(w);
  s_lib.height = u32(h);
  s_lib.cellPx = kLibCell;
  s_lib.cols = kLibCols;
  s_lib.rgba.assign(pixels, pixels + size_t(w) * h * 4);
  stbi_image_free(pixels);
  ScanCells(s_lib);
  return &s_lib;
}

const CapLibrary *Sheet() {
  if (s_sheetTried)
    return s_sheet.width ? &s_sheet : nullptr;
  s_sheetTried = true;

  s_sheet.rgba = Glyphs::SheetPixels();
  if (s_sheet.rgba.empty()) {
    REXBD_ERROR("[prompts] the glyph sheet did not decode");
    return nullptr;
  }
  s_sheet.cellPx = Glyphs::kSheetCellPx;
  s_sheet.cols = Glyphs::kSheetCols;
  s_sheet.width = Glyphs::kSheetCols * Glyphs::kSheetCellPx;
  s_sheet.height = Glyphs::kSheetRows * Glyphs::kSheetCellPx;
  ScanCells(s_sheet);
  return &s_sheet;
}

struct CapRef {
  const CapLibrary *lib = nullptr;
  u32 cell = 0;
};

// The cap of the pad button an action is bound to, in one pad's art. The face
// and shoulder buttons come from the library at its larger size, everything
// else from that pad's block on the glyph sheet.
CapRef PadCap(Action action, u32 set) {
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.type != SourceKind::kPadButton)
      continue;
    const auto button = static_cast<Button>(s.code);
    switch (button) {
    case Button::A:
    case Button::B:
    case Button::X:
    case Button::Y:
      return {Library(), kPadCellBase + set * kPadFaceButtons + u32(button) -
                             u32(Button::A)};
    case Button::LB:
    case Button::RB:
      return {Library(), kPadShoulderBase + set * kPadShoulderButtons +
                             u32(button) - u32(Button::LB)};
    default:
      break;
    }
    const int cell = Glyphs::Get().PadSheetCell(int(s.code));
    if (cell < 0)
      return {};
    return {Sheet(), u32(cell)};
  }
  return {};
}

CapImage LibImage(const CapLibrary &lib, u32 cell) {
  const InkBox &ink = lib.ink[cell];
  return {lib.rgba.data(), lib.width,
          {(cell % lib.cols) * lib.cellPx + ink.x,
           (cell / lib.cols) * lib.cellPx + ink.y, ink.w, ink.h}};
}

void DrawCaps(std::vector<u8> &dst, u32 dstW,
              std::span<const CapImage> caps, const InkBox &box,
              bool pressed) {
  const f32 dim = pressed ? kPressedDim : 1.0f;
  if (!DrawCapRow(caps, dst.data(), dstW, box, dim))
    DrawCapRow(caps.last(1), dst.data(), dstW, box, dim);
}

std::vector<u8> Compose(const PromptTex &tex) {
  std::lock_guard lock(s_mutex);
  const CapLibrary *lib = Library();
  if (!lib)
    return {};

  const bool keyboard = Glyphs::Get().Resolved() == GlyphSet::Keyboard;
  std::vector<u8> rgba(size_t(tex.width) * tex.height * 4, 0);
  const u32 cellW = tex.width / tex.cols;
  const u32 cellH = tex.height / tex.rows;

  for (u32 i = 0; i < tex.cellCount; ++i) {
    const InkBox box = {(i % tex.cols) * cellW + tex.ink.x,
                        (i / tex.cols) * cellH + tex.ink.y, tex.ink.w,
                        tex.ink.h};
    const bool pressed = tex.cells[i].pressed;
    if (keyboard) {
      const ChordCaps chord = BoundChord(tex.cells[i].action);
      if (chord.key < 0 || u32(chord.key) >= lib->ink.size())
        continue;
      CapImage caps[kMaxChordModifiers + 1];
      size_t count = 0;
      for (int m = 0; m < chord.modCount; ++m) {
        const u32 cell = kLibModCellBase + u32(chord.mods[m]);
        if (cell < lib->ink.size() && lib->ink[cell].w)
          caps[count++] = LibImage(*lib, cell);
      }
      if (count != u32(chord.modCount))
        count = 0;
      caps[count++] = LibImage(*lib, u32(chord.key));
      DrawCaps(rgba, tex.width, {caps, count}, box, pressed);
      continue;
    }
    const CapRef cap = PadCap(tex.cells[i].action, u32(Glyphs::Get().Pad()));
    if (!cap.lib || cap.cell >= cap.lib->ink.size())
      continue;
    const CapImage image = LibImage(*cap.lib, cap.cell);
    DrawCaps(rgba, tex.width, {&image, 1}, box, pressed);
  }
  return BuildGuestTexture(rgba, tex.width, tex.height);
}

// One per served texture, tracking the loaded instances it has restamped.
LiveTextureStamp s_stamps[std::size(kTextures)];

bool PadArtPresent(const CapLibrary &lib) {
  // Every set's block, since the player can pick any of them and a half-built
  // library should fall back rather than serve one pad an empty prompt.
  for (u32 cell = kPadCellBase; cell < kLibPadCellEnd; ++cell) {
    if (cell >= lib.ink.size() || !lib.ink[cell].w)
      return false;
  }
  return true;
}

} // namespace

PromptTextures &PromptTextures::Get() {
  static PromptTextures s;
  return s;
}

void PromptTextures::Publish() {
  if (published_)
    return;

  // Decoded up front rather than in the first provider: a provider that fails
  // serves an empty file, and an empty file is a texture that does not load. If
  // the library is unreadable the shipped art is the better answer.
  {
    std::lock_guard lock(s_mutex);
    const CapLibrary *lib = Library();
    if (!lib)
      return;
    padArt_ = PadArtPresent(*lib);
  }
  published_ = true;

  if (!padArt_) {
    REXBD_WARN("prompts cap library has no pad button cells, composing only "
            "while a keyboard drives");
    return;
  }
  SetEnabled(true);
}

void PromptTextures::Sync(u32 generation) {
  if (!published_)
    return;
  if (!padArt_) {
    // Without pad art the shipped file is the pad answer, so the providers
    // come and go with the device and a live instance keeps what it loaded.
    SetEnabled(Glyphs::Get().Resolved() == GlyphSet::Keyboard);
    return;
  }
  for (size_t i = 0; i < std::size(kTextures); ++i) {
    s_stamps[i].Sync(kTextures[i].key, generation,
                     [i] { return Compose(kTextures[i]); });
  }
}

void PromptTextures::SetEnabled(bool on) {
  if (on == enabled_)
    return;

  if (!on) {
    enabled_ = false;
    LayoutMount::Remove(kMount);
    REXBD_DEBUG("prompts released, the shipped button art loads again");
    return;
  }
  enabled_ = true;

  LayoutMount mount;
  for (const PromptTex &tex : kTextures)
    mount.AddRaw(tex.key, [t = &tex] { return Compose(*t); });
  mount.Publish(kMount);
  REXBD_DEBUG("prompts serving {} composed prompt textures",
           std::size(kTextures));
}

} // namespace bd::engine

// ScriptMan's action icon and the carriage prompt switch on Interact's engine id
// for their ic_btn row and know only the four face ids.
REX_MIDASM_HOOK(bdPromptIconButtonHook, (PPCRegister &r11)) {
  r11.u64 = bd::engine::kInteractRowButton;
}
REX_MIDASM_AT(bdPromptIconButtonHook, 0x8219EC88, before, r11);
REX_MIDASM_AT(bdPromptIconButtonHook, 0x82228B68, before, r11);
