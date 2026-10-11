/**
 * @file    engine/language.h
 * @brief   The language declarations BD parsed out of bd_boot.ini, plus the BD
 *          locale names the rest of the host maps onto.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// A BD locale id, in BD's own numbering. Anything that tests a locale spells it
// with these rather than restating the order.
enum LocaleId : u32 {
  kLocaleJP = 0,
  kLocaleUS,
  kLocaleDE,
  kLocaleFR,
  kLocaleES,
  kLocaleIT,
  kLocaleKR,
  kLocaleTW,
  kLocaleCN,
  kLocalePO,
};

inline constexpr u32 kLocaleCount = kLocalePO + 1;

class Locale {
public:
  Locale() = default;
  explicit Locale(u32 id) : id_(id) {}

  // XLanguage id (1..12, host side) mapped through the engine's
  // g_xLangToBdLocaleJumpOffsets. Out of range yields JP, as the engine does.
  static Locale FromXLanguage(u32 xlang);

  u32 Id() const { return id_; }

  const char *Code() const;      // "US", empty out of range
  const char *CodeLower() const; // "us", as the game spells it on disk
  const char *Name() const;      // "English", "Unknown" out of range

  bool operator==(const Locale &) const = default;

private:
  u32 id_ = 0;
};

class Language {
public:
  Language() = default;

  // False until bd_boot.ini has been parsed, i.e. no locale is declared.
  explicit operator bool() const;

  Locale Current() const; // g_bdLocaleId, what the engine latched
  Locale Default() const; // [DefaultLanguage]
  bool IsAvailable(Locale l) const;
  u32 AvailableMask() const; // bit i set means locale i is on [Language]

  // bd_boot.ini [Voice], the voice tracks the disc carries. The game's own
  // VoiceType numbers this list from one, so VoiceLocale takes it that way.
  i32 VoiceCount() const;
  Locale VoiceLocale(i32 voiceType) const;
  i32 MovieVoiceType(i32 voiceType) const;
};

} // namespace bd::engine
