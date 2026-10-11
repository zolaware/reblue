/**
 * @file    engine/d2anime/anime_data.h
 * @brief       One d2anime timeline and the variable bag its CSV layout binds
 *              against.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <optional>
#include <string_view>

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class AnimeData : public Object {
public:
  AnimeData() = default;
  explicit AnimeData(u32 address) : Object(address) {}

  f32 Length() const;
  f32 Frame() const;

  // The plain field write. AnimeData_SetAnimTime recurses into every child
  // anime, which a caller seeking the whole tree wants instead.
  bool SetFrame(f32 v);

  f32 Speed() const;
  bool SetSpeed(f32 v);

  void SyncActiveChain();
  void ApplyVarTracks(f32 frame);

  // Engine tokens only. User-facing text goes through SetText, which skips the
  // Shift-JIS widener.
  void SetString(const char *name, const char *value);
  void SetTextU16(const char *name, std::u16string_view text);
  void SetText(const char *name, std::string_view utf8);
  void SetColor(const char *name, u32 rgba);
  void SetFloat(const char *name, double value);

  std::optional<f32> Float(const char *name) const;
};

} // namespace bd::engine
