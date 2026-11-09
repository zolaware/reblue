/**
 * @file    platform/os_language.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>
#include <string_view>

namespace bd::platform {

// Best-effort host UI language as an XLanguage id (1..12, per
// rex::runtime::XLanguage). Returns 1 (English) when the host language is
// undetectable or not one the engine knows.
u32 DetectOsXLanguage();

// English name for an XLanguage id (1..12), "Unknown" otherwise. For logging.
const char *XLanguageName(u32 id);

// Parse a language code (us/en, jp/ja, de, fr, es, it, kr/ko, tw, po/pt, cn,
// case-insensitive) to an XLanguage id. Returns 0 for an unrecognized code
// (incl. "auto").
u32 XLanguageFromCode(std::string_view code);

} // namespace bd::platform
