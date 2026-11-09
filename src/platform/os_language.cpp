/**
 * @file    platform/os_language.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/os_language.h"

#include <string>
#include <string_view>

#include <rex/string/utf8.h>

#include "platform/host_ui.h"

namespace bd::platform {
namespace {

// The host reports no script subtag, so Traditional Chinese goes by region.
u32 XLangFromLocale(const rex::ui::HostLocale &locale) {
  const std::string_view lang(locale.language);
  if (lang == "zh") {
    const std::string_view country(locale.country);
    return (country == "TW" || country == "HK" || country == "MO") ? 8 : 10;
  }
  if (lang == "pl")
    return 11;
  if (lang == "ru")
    return 12;
  return XLanguageFromCode(lang);
}

} // namespace

u32 DetectOsXLanguage() {
  rex::ui::IUserInterface *ui = UI();
  if (!ui)
    return 1;
  for (const rex::ui::HostLocale &locale : ui->GetPreferredLocales()) {
    if (const u32 xlang = XLangFromLocale(locale))
      return xlang;
  }
  return 1;
}

u32 XLanguageFromCode(std::string_view code) {
  // Codes follow BD's Language Set (bd_boot.ini): US JP DE FR ES IT KR TW CN
  // PO, with common ISO aliases also accepted. Values are XLanguage ids.
  const std::string s = rex::utf8_lower_ascii(code.substr(0, 7));
  if (s == "us" || s == "en")
    return 1;
  if (s == "jp" || s == "ja")
    return 2;
  if (s == "de")
    return 3;
  if (s == "fr")
    return 4;
  if (s == "es")
    return 5;
  if (s == "it")
    return 6;
  if (s == "kr" || s == "ko")
    return 7;
  if (s == "tw")
    return 8;
  if (s == "po" || s == "pt")
    return 9;
  if (s == "cn")
    return 10;
  return 0;
}

const char *XLanguageName(u32 id) {
  switch (id) {
  case 1:
    return "English";
  case 2:
    return "Japanese";
  case 3:
    return "German";
  case 4:
    return "French";
  case 5:
    return "Spanish";
  case 6:
    return "Italian";
  case 7:
    return "Korean";
  case 8:
    return "Chinese (Traditional)";
  case 9:
    return "Portuguese";
  case 10:
    return "Chinese (Simplified)";
  case 11:
    return "Polish";
  case 12:
    return "Russian";
  default:
    return "Unknown";
  }
}
} // namespace bd::platform
