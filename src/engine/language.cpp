/**
 * @file    engine/language.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/language.h"

#include "core/memory_helpers.h"
#include "engine/state_layout.h"
#include "installer/installer.h"
#include "vfs/vfs.h"

#include <rex/ppc.h>

#include <algorithm>
#include <string>
#include <vector>

namespace bd::engine {

namespace {

// bdParseLanguageCode fixes this order.
constexpr const char *kCodes[kLocaleCount] = {"JP", "US", "DE", "FR", "ES",
                                              "IT", "KR", "TW", "CN", "PO"};

constexpr const char *kCodesLower[kLocaleCount] = {
    "jp", "us", "de", "fr", "es", "it", "kr", "tw", "cn", "po"};

// Short enough for the settings menu's value column. The two Chinese entries
// are abbreviated to fit.
constexpr const char *kNames[kLocaleCount] = {
    "Japanese", "English", "German",          "French",          "Spanish",
    "Italian",  "Korean",  "Chinese (Trad.)", "Chinese (Simp.)", "Portuguese"};

// g_xLangToBdLocaleJumpOffsets (0x820644C8), resolved through the jump targets
// at 0x822709C8 in bdFileSystemInit. Indexed by xlang-1.
constexpr u8 kXLangToLocale[10] = {1, 0, 2, 3, 4, 5, 6, 7, 9, 8};
constexpr u32 kXLangMin = 1;
constexpr u32 kXLangMax = 10;

const std::vector<std::string> &MovieVoices() {
  static const std::vector<std::string> kMovieVoices =
      bd::installer::BootLanguages::FromFile(
          bd::vfs::VFS::Get().Paths().Game() / "bd_boot.ini")
          .Movie();
  return kMovieVoices;
}

void RemapMovieVoice(PPCRegister &r4) {
  r4.u64 = static_cast<u32>(
      Language().MovieVoiceType(static_cast<i32>(r4.u32)));
}

} // namespace

Locale Locale::FromXLanguage(u32 xlang) {
  if (xlang < kXLangMin || xlang > kXLangMax)
    return Locale(0);
  return Locale(kXLangToLocale[xlang - 1]);
}

const char *Locale::Code() const {
  return id_ < kLocaleCount ? kCodes[id_] : "";
}

const char *Locale::CodeLower() const {
  return id_ < kLocaleCount ? kCodesLower[id_] : "";
}

const char *Locale::Name() const {
  return id_ < kLocaleCount ? kNames[id_] : "Unknown";
}

u32 Language::AvailableMask() const {
  const u8 *avail = bd::mem::try_at<const u8>(addr::kLanguageAvailable);
  if (!avail)
    return 0;
  u32 mask = 0;
  for (u32 i = 0; i < kLocaleCount; ++i)
    if (avail[i])
      mask |= 1u << i;
  return mask;
}

// An empty mask means bd_boot.ini has not been parsed yet (or is missing), not
// that the install ships no languages.
Language::operator bool() const { return AvailableMask() != 0; }

Locale Language::Current() const {
  return Locale(bd::mem::try_load<u32>(addr::kLocaleId));
}

Locale Language::Default() const {
  return Locale(bd::mem::try_load<u32>(addr::kBootDefaultLocale));
}

i32 Language::VoiceCount() const {
  return bd::mem::try_load<i32>(addr::kVoiceLanguageCount);
}

Locale Language::VoiceLocale(i32 voiceType) const {
  if (voiceType < 1 || voiceType > VoiceCount())
    return Locale(kLocaleCount);
  return Locale(bd::mem::try_load<u32>(addr::kVoiceLanguages +
                                       4 * (voiceType - 1)));
}

bool Language::IsAvailable(Locale l) const {
  return l.Id() < kLocaleCount && (AvailableMask() & (1u << l.Id())) != 0;
}

i32 Language::MovieVoiceType(i32 voiceType) const {
  const auto &movies = MovieVoices();
  const auto it = std::find(movies.begin(), movies.end(),
                            VoiceLocale(voiceType).CodeLower());
  if (it == movies.end())
    return 1;
  return static_cast<i32>(it - movies.begin()) + 1;
}

} // namespace bd::engine

void bdDemoPlayMovieVoiceHook(PPCRegister &r4) {
  bd::engine::RemapMovieVoice(r4);
}

void viewMovieVoiceHook(PPCRegister &r4) { bd::engine::RemapMovieVoice(r4); }

void bdEventSceneMovieVoiceHook(PPCRegister &r4) {
  bd::engine::RemapMovieVoice(r4);
}
