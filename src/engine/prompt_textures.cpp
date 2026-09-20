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
#include <vector>

#include <stb_image.h>

#include "core/logging.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mount.h"
#include "engine/glyph_set.h"
#include "engine/guest_texture.h"
#include "engine/input/actions.h"
#include "engine/input/binding_store.h"
#include "engine/live_texture_stamp.h"
#include "embedded.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

// The cap library beside the footer sheet: one 128px cell per bindable key in
// kBindableKeys order, then the arrow cluster, then four face buttons per pad
// in PadSet order for the pad side of these prompts.
constexpr u32 kLibCell = 128;
constexpr u32 kLibCols = 8;
constexpr u32 kPadCellBase = u32(bd::platform::kBindableKeyCount) + 1;
constexpr u32 kPadFaceButtons = 4;

// A prompt texture's cell always stands for a face button.
int PadOrdinal(Action action) {
  for (const Source &s : Bindings::Get().Sources(action)) {
    if (s.kind != SourceKind::PadButton)
      continue;
    switch (static_cast<Button>(s.code)) {
    case Button::A:
      return 0;
    case Button::B:
      return 1;
    case Button::X:
      return 2;
    case Button::Y:
      return 3;
    default:
      return -1;
    }
  }
  return -1;
}

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
struct InkBox {
  u32 x, y, w, h;
};

constexpr InkBox kInk64 = {13, 15, 38, 40};
constexpr InkBox kInkActEv = {42, 14, 172, 92};

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
constexpr PromptCell kCellY[] = {{Action::MainMenu, false}};
constexpr PromptCell kCellYPsh[] = {{Action::MainMenu, true}};

// ic_btn's own pos rows: posAnr, posApsh, posBnr, posBpsh and so on down the
// four rows, normal in the left column and pressed in the right.
constexpr PromptCell kCellsIcBtn[] = {
    {Action::Confirm, false}, {Action::Confirm, true},
    {Action::Cancel, false},  {Action::Cancel, true},
    {Action::Attack, false},  {Action::Attack, true},
    {Action::MainMenu, false}, {Action::MainMenu, true}};

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
                  1, kInkActEv, kCellX),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_x_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellXPsh),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_y_nrm.dds", 256, 128, 1,
                  1, kInkActEv, kCellY),
    BD_PROMPT_TEX("minigame\\actev\\d2anim\\res\\acv_btn_y_push.dds", 256, 128,
                  1, 1, kInkActEv, kCellYPsh),
};

#undef BD_PROMPT_TEX

// The decoded cap library, with each cell's ink box found once so a cap can be
// fit to a destination without rescanning it.
struct CapLibrary {
  std::vector<u8> rgba;
  u32 width = 0, height = 0;
  std::vector<InkBox> ink;
};

std::mutex s_mutex;
CapLibrary s_lib;
bool s_libTried = false;

// Alpha bounds of one library cell, in cell-local coordinates. A cell with no
// art at all comes back zero-sized and is skipped rather than drawn as a dot.
InkBox ScanCell(const CapLibrary &lib, u32 cell) {
  const u32 ox = (cell % kLibCols) * kLibCell;
  const u32 oy = (cell / kLibCols) * kLibCell;
  u32 x0 = kLibCell, y0 = kLibCell, x1 = 0, y1 = 0;
  for (u32 y = 0; y < kLibCell; ++y) {
    for (u32 x = 0; x < kLibCell; ++x) {
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
    BD_ERROR("[prompts] the key cap library did not decode");
    return nullptr;
  }

  s_lib.width = u32(w);
  s_lib.height = u32(h);
  s_lib.rgba.assign(pixels, pixels + size_t(w) * h * 4);
  stbi_image_free(pixels);

  const u32 cells = (s_lib.width / kLibCell) * (s_lib.height / kLibCell);
  s_lib.ink.resize(cells);
  for (u32 i = 0; i < cells; ++i)
    s_lib.ink[i] = ScanCell(s_lib, i);
  return &s_lib;
}

// Area-averaged sample of a library cell's ink rect. Every destination here is a
// downscale from the 128px library, and point sampling a cap's outline at a
// third of its size drops whole strokes out of it.
void DrawCap(std::vector<u8> &dst, u32 dstW, const CapLibrary &lib, u32 cell,
             const InkBox &box, bool pressed) {
  const InkBox &src = lib.ink[cell];
  if (!src.w || !src.h)
    return;

  // Fit preserving aspect, then center in the box, so a wide cap keeps its
  // proportions instead of stretching to the shipped art's.
  const float scale =
      std::min(float(box.w) / float(src.w), float(box.h) / float(src.h));
  const u32 outW = std::max(1u, u32(float(src.w) * scale + 0.5f));
  const u32 outH = std::max(1u, u32(float(src.h) * scale + 0.5f));
  const u32 left = box.x + (box.w - outW) / 2;
  const u32 top = box.y + (box.h - outH) / 2;

  const u32 cx = (cell % kLibCols) * kLibCell + src.x;
  const u32 cy = (cell / kLibCols) * kLibCell + src.y;

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
          const u8 *px =
              &lib.rgba[(size_t(cy + sy) * lib.width + cx + sx) * 4];
          // Weight color by coverage so a transparent neighbor cannot wash a
          // stroke's edge toward black.
          acc[0] += u32(px[0]) * px[3];
          acc[1] += u32(px[1]) * px[3];
          acc[2] += u32(px[2]) * px[3];
          acc[3] += px[3];
          ++n;
        }
      }
      if (!n || !acc[3])
        continue;

      u8 *out = &dst[(size_t(top + y) * dstW + left + x) * 4];
      for (int c = 0; c < 3; ++c) {
        const float v = float(acc[c]) / float(acc[3]);
        out[c] = u8(std::min(255.0f, pressed ? v * kPressedDim : v));
      }
      out[3] = u8(acc[3] / n);
    }
  }
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
    int cell;
    if (keyboard) {
      cell = BoundKeyIndex(tex.cells[i].action);
    } else {
      const int ord = PadOrdinal(tex.cells[i].action);
      const int set = static_cast<int>(Glyphs::Get().Pad());
      cell = ord < 0 ? -1
                     : int(kPadCellBase) +
                           set * int(kPadFaceButtons) + ord;
    }
    if (cell < 0 || size_t(cell) >= lib->ink.size())
      continue;
    const InkBox box = {(i % tex.cols) * cellW + tex.ink.x,
                        (i / tex.cols) * cellH + tex.ink.y, tex.ink.w,
                        tex.ink.h};
    DrawCap(rgba, tex.width, *lib, u32(cell), box, tex.cells[i].pressed);
  }
  return BuildGuestTexture(rgba, tex.width, tex.height);
}

// One per served texture, tracking the loaded instances it has restamped.
LiveTextureStamp s_stamps[std::size(kTextures)];

bool PadArtPresent(const CapLibrary &lib) {
  // Every set's block, since the player can pick any of them and a half-built
  // library should fall back rather than serve one pad an empty prompt.
  for (u32 i = 0; i <= u32(kPadSetLast) * kPadFaceButtons + 3; ++i) {
    const size_t cell = kPadCellBase + i;
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
    BD_WARN("[prompts] cap library has no pad button cells, composing only "
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
    BD_DEBUG("[prompts] released, the shipped button art loads again");
    return;
  }
  enabled_ = true;

  LayoutMount mount;
  for (const PromptTex &tex : kTextures)
    mount.AddRaw(tex.key, [t = &tex] { return Compose(*t); });
  mount.Publish(kMount);
  BD_DEBUG("[prompts] serving {} composed prompt textures",
           std::size(kTextures));
}

} // namespace bd::engine
