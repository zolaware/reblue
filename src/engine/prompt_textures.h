/**
 * @file    engine/prompt_textures.h
 * @brief   The button prompts that name their own texture instead of a UV.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// Everything outside the footers: the field interact icon, the script and
// battle prompt atlas, the menu character pager, and the QTE buttons.
//
// The footers all read eleven shared globals, so restamping one sheet moves
// every one of them at once. These do not. Each names a texture of its own
// and picks its cell itself, whole-texture for the field and QTE art and
// through SCA_DECIDE_BTN_UV name substitution for the script atlas, so each
// file is served composed for the device driving right now.
//
// Composition runs per read in the VFS provider. Sync rewrites the instances
// already loaded when the device or a bind changes.
class PromptTextures {
public:
  static PromptTextures &Get();

  // Registers the providers. Without pad button art in the cap library it falls
  // back to serving only while a keyboard drives, and Sync degrades to mounting
  // and unmounting on the device change.
  void Publish();

  // Once per engine tick, with Glyphs' generation. Restamps every live loaded
  // instance whose art is not the current generation's.
  void Sync(u32 generation);

private:
  PromptTextures() = default;

  void SetEnabled(bool on);

  bool published_ = false;
  bool padArt_ = false;
  bool enabled_ = false;
};

} // namespace bd::engine
