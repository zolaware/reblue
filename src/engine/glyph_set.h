/**
 * @file    engine/glyph_set.h
 * @brief   Which button glyphs every prompt in the game draws.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <mutex>
#include <span>
#include <string_view>
#include <vector>

#include <rex/input_keys.h>
#include <rex/types.h>

#include "engine/d2anime/anime_layout.h"
#include "engine/input/actions.h"
#include "engine/live_texture_stamp.h"

namespace bd::engine {

class D2AnimeTask;

// Auto follows whichever device the player last used. The other two pin the
// sheet, for a player recording video or reading a guide.
enum class GlyphSet : i32 {
  Auto = 0,
  Controller = 1,
  Keyboard = 2,
};

// Which controller's art the prompts wear whenever they are wearing a
// controller's. Auto follows the pad the host has connected, and falls back to
enum class PadSet : i32 {
  Auto = -1,
  Xbox360 = 0,
  XboxSeries = 1,
  PlayStation = 2,
  Switch = 3,
  SteamDeck = 4,
};
inline constexpr i32 kPadSetFirst = static_cast<i32>(PadSet::Auto);
inline constexpr i32 kPadSetLast = static_cast<i32>(PadSet::SteamDeck);

const char *ToString(GlyphSet set);
const char *ToString(PadSet set);

inline constexpr int kMaxChordModifiers = 3;

struct ChordCaps {
  int key = -1;
  int mods[kMaxChordModifiers] = {};
  int modCount = 0;
};

ChordCaps BoundChord(Action action);

struct CapBox {
  u32 x, y, w, h;
};

struct CapImage {
  const u8 *rgba;
  u32 pitchPx;
  CapBox ink;
};

bool DrawCapRow(std::span<const CapImage> caps, u8 *dst, u32 dstPitchPx,
                const CapBox &box, f32 dim);

// The same lookup for one source, ignoring its modifiers.
int KeyIndex(const rex::InputSource &source);

// Publishes the button glyph set. The eleven Help_*_Uv globals are pinned to
// the sheet's pad cells once per bag, since the shipped rects assume the
// disc's smaller sheet. What a device change or a rebind moves is the texels
// under those cells, rewritten in the served blob and every loaded instance.
//
// Texels rather than UVs because the engine copies a 'uv.' reference at parse
// instead of linking it: a UV write cannot reach a CSV already on screen, but
// every prompt samples the texture live.
//
// A pad button has one bound key, so eleven cells cover any keyboard.
class Glyphs {
public:
  static Glyphs &Get();

  // Once per engine tick. Follows the input device and reapplies on a change of
  // device or of any keybind.
  void Tick();

  // Re-resolves the eleven variables against a freshly parsed Uv.csv bag and
  // reapplies. The bag is torn down with the character UV cache and rebuilt on
  // the next acquire, so the values never survive on their own.
  void Rebind();

  // What Auto currently resolves to. Never Auto itself.
  GlyphSet Resolved() const { return resolved_; }

  // The controller the prompts are wearing. Read only when Resolved() is not
  // Keyboard, and what prompt_textures.cpp indexes its pad caps by. Never Auto
  // itself.
  PadSet Pad() const { return pad_; }

  // Bumped by every reapply, so a layout that outlives one can tell its caps
  // went stale. The engine copies a 'uv.' reference at parse rather than
  // linking it, so an already-parsed CSV cannot follow the bag.
  u32 Generation() const { return generation_; }

  // UV rect of the sheet cell showing one bind's art in the resolved set, or
  // the D-pad cross for Action::Count. What a re:Blue layout writes into its
  // own vars in place of the snapshot the parse took.
  UVRect ActionUV(Action action) const;

  bool ChordUV(Action action, UVRect &mod, UVRect &key) const;

  static void DeclareChordCap(CsvBuilder &b, const char *posVar);
  static void EmitChordCap(CsvBuilder &b, const AnimeTexAbs &single,
                           const char *posVar);
  void PushChordCap(D2AnimeTask &task, const char *posVar, const HelpUv &uv,
                    Action action) const;

  // The ink band of one key's cap, cut clear of the cell's transparent
  // margins, for a surface that scales the cap well below the footers' 64px.
  // Index is a kBindableKeys position, negative gets the blank cell.
  static UVRect KeyArtUV(int keyIndex);

  bool PadButtonUV(int padButton, UVRect &uv) const;

  // Sheet cell holding a pad button's art in the current pad set, or -1 for a
  // code the sheet has no art for.
  int PadSheetCell(int padButton) const;

  static std::vector<u8> SheetPixels();

  static constexpr u32 kSheetCols = 8;
  static constexpr u32 kSheetRows = 32;
  static constexpr u32 kSheetCellPx = 64;

  static constexpr u32 kKeyInkX0Px = 6;
  static constexpr u32 kKeyInkX1Px = 44;
  static constexpr u32 kInkY0Px = 12;
  static constexpr u32 kInkY1Px = 52;
  static constexpr u32 kModInkX0Px = 4;
  static constexpr u32 kModInkX1Px = 60;

  static int ModifierIndex(rex::KeyModifiers mods);

  // The ink band of one modifier cap. The modifier cells ink wider than the
  // key cells, so this is not KeyArtUV over a different index.
  static UVRect ModifierArtUV(int modIndex);

  // The eleven named cells of res\cmn_help_menue.
  static constexpr int kHelpCells = 11;

private:
  Glyphs() = default;

  void InitOnce();
  void Apply();
  void WriteCell(u32 va, int cell) const;
  GlyphSet Wanted() const;
  PadSet WantedPad() const;

  // a keyboard. Serves fresh loads and restamps live instances alike.
  std::vector<u8> ComposeSheet() const;
  void ComposeChordCell(u8 *payload, size_t bytes, int cell,
                        const ChordCaps &chord) const;

  u32 vars_[kHelpCells] = {};
  u32 nameBlock_ = 0;
  bool started_ = false;
  bool bound_ = false;
  GlyphSet lastDevice_ = GlyphSet::Keyboard;
  GlyphSet resolved_ = GlyphSet::Auto; // forces the first apply
  // What Auto resolves to: re-read from the host whenever a pad is used, so
  // swapping controllers mid-session reaches the prompts on the next press.
  PadSet autoPad_ = PadSet::Xbox360;
  PadSet pad_ = PadSet::Xbox360;
  u32 generation_ = 0;
  LiveTextureStamp sheetStamp_;
  mutable std::mutex sheetMutex_;
  mutable std::vector<u8> sheetRgba_;
  u32 bindGeneration_ = 0;
};

} // namespace bd::engine
