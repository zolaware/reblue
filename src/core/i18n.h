/**
 * @file    core/i18n.h
 * @brief   Localized text for the host's own UI: settings, config menu,
 *          installer wizard.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>

#include <rex/types.h>

namespace bd::i18n {

// BD locale ids, as bd::engine::Locale::Code spells them.
void SetLocale(u32 locale);
u32 CurrentLocale();

// The locale the guest latched from bd_boot.ini, else the bd_language cvar,
// else the OS. Safe before the guest exists, which is how the installer picks.
void SyncLocale();

// Fires after SetLocale changes the catalog, so open menus can rebuild.
void OnLocaleChanged(std::function<void()> cb);

// Text for a dotted key ("settings.display.vsync.label"). An unknown key reads
// "#key", which a screenshot shows and a blank row does not. The reference
// stays valid until the next SetLocale.
const std::string &Text(std::string_view key);

// A translated format string is data, so a bad one degrades to the unformatted
// text rather than throwing out of a draw call.
template <typename... A> std::string Fmt(std::string_view key, A &&...args) {
  const std::string &pattern = Text(key);
  try {
    return std::vformat(pattern, std::make_format_args(args...));
  } catch (const std::format_error &) {
    return pattern;
  }
}

// MiB up to a gibibyte, GiB past it.
std::string Bytes(u64 bytes);

// Reads a term out of the disc's own message tables. Registered by the engine,
// the only layer that can reach them. The installer runs without one.
using GameTermResolver = std::function<bool(
    std::string_view file, std::string_view key, u32 locale, std::string &out)>;
void SetGameTermResolver(GameTermResolver resolver);

} // namespace bd::i18n
