/**
 * @file    engine/glyph_set.h
 * @brief   Which button glyphs every prompt in the game draws.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <string_view>
#include <vector>

#include <rex/types.h>

#include "engine/d2anime/anime_layout.h"
#include "engine/input/actions.h"
#include "engine/live_texture_stamp.h"

namespace bd::engine {

// Auto follows whichever device the player last used. The other two pin the
// sheet, for a player recording video or reading a guide.
enum class GlyphSet : i32 {
  Auto = 0,
  Controller = 1,
  Keyboard = 2,
};

// Which controller's art the prompts wear whenever they are wearing a
// controller's. Auto follows the pad the host has connected, and falls back to
// the 360, which is the disc's own block and the one set that costs no
// substitution.
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

int BoundKeyIndex(Action action);

// The same lookup for a bare key name ("Up", "LMB"), with any modifier prefix
// already stripped.
int KeyIndex(std::string_view keyName);

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

  // UV rect of one of the eleven prompts, named the way Uv.csv names it
  // ("Help_RT_Uv"): always the pad cell, whose texels carry whatever the
  // resolved set stamped there. What a re:Blue layout writes into its own
  // vars in place of the snapshot the parse took.
  UVRect CellUV(const char *helpName) const;

  UVRect PromptUV(const PromptGlyph &glyph) const;

  // The ink band of one key's cap, cut clear of the cell's transparent
  // margins, for a surface that scales the cap well below the footers' 64px.
  // Index is a kBindableKeys position, negative gets the blank cell.
  static UVRect KeyArtUV(int keyIndex);

  // Position of a bind's modifier prefix ("Shift+", "Ctrl+", "Alt+", with or
  // without the plus) in the sheet's modifier run, or -1 for anything else.
  static int ModifierIndex(std::string_view prefix);

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

  // The full sheet blob for the resolved set: the served bytes as shipped on
  // a pad, and with each pad cell's texels replaced by its bound key's cap on
  // a keyboard. Serves fresh loads and restamps live instances alike.
  std::vector<u8> ComposeSheet() const;

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
  u32 bindGeneration_ = 0;
};

} // namespace bd::engine
