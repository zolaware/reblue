/**
 * @file    core/i18n.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/i18n.h"

#include "core/app_root.h"
#include "core/logging.h"
#include "core/settings.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <toml++/toml.h>

#include "engine/engine.h"
#include "platform/platform.h"
#include "vfs/vfs.h"

#include "embedded.h"

namespace bd::i18n {
namespace {

struct KeyHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const {
    return std::hash<std::string_view>{}(s);
  }
};
struct KeyEq {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const {
    return a == b;
  }
};
using Catalog = std::unordered_map<std::string, std::string, KeyHash, KeyEq>;

constexpr std::string_view kGamePrefix = "@game:";

// English is both the fallback layer and a locale in its own right. Every
// catalog entry carries a locale suffix, so one key holds a ".us" line and,
// where a translator wrote one, a ".fr" line beside it.
constexpr u32 kLocaleUS = 1;
constexpr std::string_view kEnglishCode = "us";

std::mutex g_mutex;
Catalog g_catalog;
Catalog g_markers;   // "#key" strings, kept so Text can return a reference
Catalog g_fallbacks; // literal half of a borrow that did not resolve
u32 g_localeId = kLocaleUS;
GameTermResolver g_resolver;
bool g_loaded = false;

void Flatten(const toml::table &tbl, const std::string &prefix, Catalog &out) {
  for (const auto &[k, v] : tbl) {
    std::string key = prefix.empty() ? std::string(k.str())
                                     : prefix + "." + std::string(k.str());
    if (const auto *sub = v.as_table())
      Flatten(*sub, key, out);
    // Empty means not translated yet, so it falls back instead of blanking.
    else if (const auto *str = v.as_string(); str && !str->get().empty())
      out[key] = str->get();
  }
}

void MergeTOML(std::string_view text, std::string_view origin, Catalog &out) {
  try {
    Flatten(toml::parse(text), "", out);
  } catch (const toml::parse_error &e) {
    REXBD_WARN("i18n {} parse error: {}", origin, e.description());
  }
}

bool MergeFile(const std::filesystem::path &path, Catalog &out) {
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec))
    return false;
  try {
    Flatten(toml::parse_file(path.string()), "", out);
    return true;
  } catch (const toml::parse_error &e) {
    REXBD_WARN("i18n {} parse error: {}", path.string(), e.description());
    return false;
  }
}

// What a missing key renders as, and what bd_i18n_keys renders everything as.
const std::string &KeyMarker(std::string_view key) {
  auto it = g_markers.find(key);
  if (it == g_markers.end())
    it = g_markers.emplace(std::string(key), "#" + std::string(key)).first;
  return it->second;
}

std::filesystem::path OverrideFolder() {
  const std::filesystem::path dir(bd::Settings::Get().LanguagePath());
  return dir.is_absolute() ? dir : AppRootFolder() / dir;
}

void Reload() {
  g_catalog.clear();
  g_markers.clear();
  g_fallbacks.clear();

  const auto delivered = vfs::VFS::Get().Content().Find("localization.toml");
  if (MergeFile(delivered, g_catalog)) {
    REXBD_INFO("i18n catalog from {}", delivered.string());
  } else {
    constexpr auto kCatalog = bd::Embedded("localization.toml");
    MergeTOML(kCatalog.text(), "localization.toml", g_catalog);
  }

  MergeFile(OverrideFolder() / "localization.toml", g_catalog);

  g_loaded = true;
}

// "@game:cfg_str/CFG_S0079|Cancel" takes the disc's own wording for the current
// language, falling back to the half after the bar when it cannot be read.
const std::string *Lookup(std::string_view key, std::string_view code) {
  static std::string suffixed;
  suffixed.assign(key).append(1, '.').append(code);

  auto it = g_catalog.find(suffixed);
  if (it == g_catalog.end())
    return nullptr;
  if (!it->second.starts_with(kGamePrefix))
    return &it->second;

  std::string_view ref = it->second;
  ref.remove_prefix(kGamePrefix.size());
  std::string_view literal;
  if (const size_t bar = ref.find('|'); bar != std::string_view::npos) {
    literal = ref.substr(bar + 1);
    ref = ref.substr(0, bar);
  }

  const size_t slash = ref.find('/');
  if (g_resolver && slash != std::string_view::npos) {
    std::string text;
    if (g_resolver(ref.substr(0, slash), ref.substr(slash + 1), g_localeId,
                   text) &&
        !text.empty()) {
      it->second = std::move(text); // a locale change reloads the file
      return &it->second;
    }
  }

  if (literal.empty())
    return nullptr;
  auto fb = g_fallbacks.find(suffixed);
  if (fb == g_fallbacks.end())
    fb = g_fallbacks.emplace(suffixed, std::string(literal)).first;
  return &fb->second;
}

} // namespace

void SetLocale(u32 locale) {
  {
    std::lock_guard lock(g_mutex);
    if (g_loaded && locale == g_localeId)
      return;
    g_localeId = locale;
    Reload();
  }
  REXBD_INFO("i18n locale {} ({})", engine::Locale(locale).Code(),
          engine::Locale(locale).Name());
}

void SyncLocale() {
  static bool resolverInstalled = false;
  if (!resolverInstalled) {
    resolverInstalled = true;
    engine::InstallGameTermResolver();
  }

  const engine::Language language;
  if (language) {
    SetLocale(language.Current().Id());
    return;
  }
  const u32 xlang = platform::XLanguageFromCode(bd::Settings::Get().Language());
  SetLocale(engine::Locale::FromXLanguage(xlang ? xlang
                                                : platform::DetectOsXLanguage())
                .Id());
}

u32 CurrentLocale() {
  std::lock_guard lock(g_mutex);
  return g_localeId;
}

void SetGameTermResolver(GameTermResolver resolver) {
  std::lock_guard lock(g_mutex);
  g_resolver = std::move(resolver);
  if (g_loaded)
    Reload();
}

std::string Bytes(u64 bytes) {
  constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
  constexpr double kMiB = 1024.0 * 1024.0;
  const double b = static_cast<double>(bytes);
  if (b >= kGiB)
    return std::format("{:.2f} GiB", b / kGiB);
  return std::format("{:.1f} MiB", b / kMiB);
}

const std::string &Text(std::string_view key) {
  std::lock_guard lock(g_mutex);
  if (!g_loaded)
    Reload();

  if (bd::Settings::Get().I18nKeys())
    return KeyMarker(key);

  const std::string_view code = engine::Locale(g_localeId).CodeLower();
  if (code != kEnglishCode)
    if (const std::string *text = Lookup(key, code))
      return *text;
  if (const std::string *text = Lookup(key, kEnglishCode))
    return *text;

  const bool first = !g_markers.contains(key);
  if (first)
    REXBD_WARN("i18n missing key: {}", key);
  return KeyMarker(key);
}

} // namespace bd::i18n
