/**
 * @file    vfs/key.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/key.h"

#include <algorithm>

#include <rex/string/utf8.h>
#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::vfs {

namespace addr {
inline constexpr u32 kBasePath = 0x827A84F8;     // g_bdBasePath
inline constexpr u32 kLocaleId = 0x827A8578;     // g_bdLocaleId
inline constexpr u32 kLocaleTable = 0x82780A24;  // locale-string pointer table
inline constexpr u32 kYYIndexArray = 0x82775710; // per-selector locale indices
inline constexpr u32 kYYSelector = 0x82DC40D8;   // current _yy. selector
} // namespace addr

namespace {

std::string ReadLocaleString(int locale_index) {
  const u32 str_addr =
      mem::load<u32>(addr::kLocaleTable + static_cast<u32>(locale_index) * 4);
  return mem::str(str_addr);
}

std::string Normalize(std::string_view raw_path, std::string_view base_path) {
  std::string_view path = raw_path;

  // 'game:\dir\file' and the CSV disc root escape ':d2anime\...' both name the
  // root the base path is relative to.
  if (auto colon = path.find(':');
      colon != std::string_view::npos && colon + 1 < path.size()) {
    path.remove_prefix(colon + 1);
    if (!path.empty() && (path.front() == '\\' || path.front() == '/'))
      path.remove_prefix(1);
  }

  if (rex::utf8_starts_with_case(path, base_path))
    path.remove_prefix(base_path.size());

  std::string key = rex::utf8_lower_ascii(path);
  std::replace(key.begin(), key.end(), '/', '\\');

  if (!key.empty() && key.front() == '\\')
    key.erase(0, 1);
  return key;
}

} // namespace

Key Key::FromGuestPath(std::string_view raw) {
  // g_bdBasePath is fixed by bdFileSystemInit, which has run by the time any IO
  // hook fires, so one read stands for the process.
  static const std::string base = mem::str(addr::kBasePath);
  return Key(Normalize(raw, base));
}

Key Key::FromRelative(std::string_view raw) { return Key(Normalize(raw, {})); }

Key Key::operator/(const Key &child) const {
  if (key_.empty())
    return child;
  if (child.key_.empty())
    return *this;
  return Key(key_ + '\\' + child.key_);
}

Key Key::Slice(size_t from, size_t to) const {
  return Key(key_.substr(from, to == std::string::npos ? std::string::npos
                                                       : to - from));
}

Key Key::Localized() const {
  std::string result = key_;

  if (auto xx = result.find("_xx."); xx != std::string::npos) {
    auto locale = ReadLocaleString(mem::load<i32>(addr::kLocaleId));
    if (!locale.empty())
      result.replace(xx, 3, locale);
    return Key(std::move(result));
  }

  if (auto yy = result.find("_yy."); yy != std::string::npos) {
    const i32 selector = mem::load<i32>(addr::kYYSelector);
    auto locale = ReadLocaleString(
        mem::load<i32>(addr::kYYIndexArray + static_cast<u32>(selector) * 4));
    if (!locale.empty())
      result.replace(yy, 3, locale);
    return Key(std::move(result));
  }

  // _zz. is the same substitution except locale 0 drops the marker entirely.
  if (auto zz = result.find("_zz."); zz != std::string::npos) {
    const i32 locale_id = mem::load<i32>(addr::kLocaleId);
    if (locale_id == 0) {
      result.erase(zz, 3);
    } else {
      auto locale = ReadLocaleString(locale_id);
      if (!locale.empty())
        result.replace(zz, 3, locale);
    }
  }

  return Key(std::move(result));
}

} // namespace bd::vfs
