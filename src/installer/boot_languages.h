/**
 * @file    installer/boot_languages.h
 * @brief   The [Language] and [Voice] lines of bd_boot.ini.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rex::filesystem {
class Entry;
} // namespace rex::filesystem

namespace bd::installer {

class BootLanguages {
public:
  static BootLanguages FromIni(std::string_view text);
  static BootLanguages FromFile(const std::filesystem::path &file);
  static BootLanguages FromDisc(rex::filesystem::Entry &root);

  const std::vector<std::string> &Text() const { return text_; }
  const std::vector<std::string> &Voice() const { return voice_; }
  const std::vector<std::string> &Movie() const { return movie_; }
  std::set<std::string> All() const;

  bool Removable(const std::string &code) const;

  std::set<std::string> Missing(const BootLanguages &disc) const;
  bool Add(const BootLanguages &disc, const std::set<std::string> &langs);
  bool Remove(const std::string &code);
  bool Keep(const std::set<std::string> &text,
            const std::set<std::string> &voice);
  void SetMovie(const std::vector<std::string> &codes) { movie_ = codes; }
  bool Flush(const std::filesystem::path &file) const;

private:
  std::vector<std::string> text_, voice_, movie_;
  std::set<std::string> required_;
};

} // namespace bd::installer
