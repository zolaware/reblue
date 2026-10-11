/**
 * @file    installer/boot_languages.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/boot_languages.h"

#include <rex/filesystem/entry.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

#include "core/xcontent.h"

namespace bd::installer {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kTextTag = "[Language]";
constexpr std::string_view kVoiceTag = "[Voice]";
constexpr std::string_view kMovieTag = "[MovieVoice]";

constexpr std::array<std::string_view, 3> kSingleCodeTags = {
    "[DefaultLanguage]", "[BGM]", "[Lip]"};

constexpr std::string_view kFallbackLang = "us";

constexpr std::array<std::string_view, 10> kKnownLangCodes = {
    "us", "jp", "de", "fr", "es", "it", "kr", "tw", "cn", "po"};

bool IsKnownLang(std::string_view code) {
  for (auto known : kKnownLangCodes) {
    if (known == code)
      return true;
  }
  return false;
}

std::vector<std::string> SplitLangCodes(std::string_view rest) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p < rest.size()) {
    while (p < rest.size() && std::isspace(static_cast<unsigned char>(rest[p])))
      ++p;
    size_t q = p;
    while (q < rest.size() &&
           !std::isspace(static_cast<unsigned char>(rest[q])))
      ++q;
    if (q > p) {
      std::string token(rest.substr(p, q - p));
      for (auto &ch : token)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (IsKnownLang(token))
        out.push_back(std::move(token));
    }
    p = q;
  }
  return out;
}

void AppendUnique(std::vector<std::string> &dst, const std::string &code) {
  if (std::find(dst.begin(), dst.end(), code) == dst.end())
    dst.push_back(code);
}

std::string JoinUpper(const std::vector<std::string> &codes) {
  std::string out;
  for (const auto &code : codes) {
    if (!out.empty())
      out += ' ';
    for (char ch : code)
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return out;
}

std::string TagLine(std::string_view tag,
                    const std::vector<std::string> &codes) {
  std::string out(tag);
  out += '\t';
  out += JoinUpper(codes);
  return out;
}

bool RewriteTagLine(std::string &line, std::string_view tag,
                    const std::vector<std::string> &codes) {
  std::string_view body(line);
  const bool cr = !body.empty() && body.back() == '\r';
  if (cr)
    body.remove_suffix(1);
  if (body.rfind(tag, 0) != 0)
    return false;
  line = TagLine(tag, codes);
  if (cr)
    line += '\r';
  return true;
}

std::vector<std::string> SplitLines(const std::string &text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (true) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

bool ReadWholeFile(const fs::path &file, std::string &out) {
  std::ifstream in(file, std::ios::binary);
  if (!in)
    return false;
  out.assign(std::istreambuf_iterator<char>(in),
             std::istreambuf_iterator<char>());
  return true;
}

} // namespace

BootLanguages BootLanguages::FromIni(std::string_view text) {
  BootLanguages out;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n')
      continue;
    std::string_view line = text.substr(start, i - start);
    start = i + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty() || line.front() == ';')
      continue;

    std::vector<std::string> *dst = nullptr;
    std::string_view tag;
    if (line.rfind(kTextTag, 0) == 0) {
      dst = &out.text_;
      tag = kTextTag;
    } else if (line.rfind(kVoiceTag, 0) == 0) {
      dst = &out.voice_;
      tag = kVoiceTag;
    } else if (line.rfind(kMovieTag, 0) == 0) {
      dst = &out.movie_;
      tag = kMovieTag;
    } else {
      for (auto single : kSingleCodeTags) {
        if (line.rfind(single, 0) != 0)
          continue;
        const auto codes = SplitLangCodes(line.substr(single.size()));
        if (!codes.empty())
          out.required_.insert(codes.front());
        break;
      }
      continue;
    }

    for (const auto &code : SplitLangCodes(line.substr(tag.size())))
      AppendUnique(*dst, code);
  }
  if (out.movie_.empty())
    out.movie_ = out.voice_;
  return out;
}

BootLanguages BootLanguages::FromFile(const fs::path &file) {
  std::string text;
  if (!ReadWholeFile(file, text))
    return BootLanguages();
  return FromIni(text);
}

BootLanguages BootLanguages::FromDisc(rex::filesystem::Entry &root) {
  auto *entry = root.ResolvePath("bd_boot.ini");
  if (entry == nullptr)
    return BootLanguages();
  return FromIni(bd::ReadEntry(*entry));
}

std::set<std::string> BootLanguages::All() const {
  std::set<std::string> out(text_.begin(), text_.end());
  out.insert(voice_.begin(), voice_.end());
  return out;
}

bool BootLanguages::Removable(const std::string &code) const {
  return code != kFallbackLang && required_.count(code) == 0;
}

std::set<std::string> BootLanguages::Missing(const BootLanguages &disc) const {
  std::set<std::string> out;
  auto scan = [&out](const std::vector<std::string> &src,
                     const std::vector<std::string> &mine) {
    for (const auto &code : src) {
      if (std::find(mine.begin(), mine.end(), code) == mine.end())
        out.insert(code);
    }
  };
  scan(disc.text_, text_);
  scan(disc.voice_, voice_);
  return out;
}

bool BootLanguages::Add(const BootLanguages &disc,
                        const std::set<std::string> &langs) {
  bool changed = false;
  auto merge = [&langs, &changed](const std::vector<std::string> &src,
                                  std::vector<std::string> &dst) {
    for (const auto &code : src) {
      if (langs.count(code) == 0)
        continue;
      if (std::find(dst.begin(), dst.end(), code) != dst.end())
        continue;
      dst.push_back(code);
      changed = true;
    }
  };
  merge(disc.text_, text_);
  merge(disc.voice_, voice_);
  return changed;
}

bool BootLanguages::Remove(const std::string &code) {
  bool changed = false;
  auto drop = [&code, &changed](std::vector<std::string> &dst) {
    const auto it = std::find(dst.begin(), dst.end(), code);
    if (it == dst.end())
      return;
    dst.erase(it);
    changed = true;
  };
  drop(text_);
  drop(voice_);
  return changed;
}

bool BootLanguages::Keep(const std::set<std::string> &text,
                         const std::set<std::string> &voice) {
  bool changed = false;
  auto prune = [&changed](std::vector<std::string> &dst,
                          const std::set<std::string> &keep) {
    const auto it = std::remove_if(
        dst.begin(), dst.end(),
        [&keep](const std::string &code) { return keep.count(code) == 0; });
    if (it == dst.end())
      return;
    dst.erase(it, dst.end());
    changed = true;
  };
  prune(text_, text);
  prune(voice_, voice);
  return changed;
}

bool BootLanguages::Flush(const fs::path &file) const {
  std::string text;
  if (!ReadWholeFile(file, text))
    return false;

  std::vector<std::string> lines = SplitLines(text);
  bool has_text = false;
  bool has_voice = false;
  bool has_movie = false;
  for (auto &line : lines) {
    if (!has_text && RewriteTagLine(line, kTextTag, text_)) {
      has_text = true;
      continue;
    }
    if (!has_voice && RewriteTagLine(line, kVoiceTag, voice_)) {
      has_voice = true;
      continue;
    }
    if (!has_movie && RewriteTagLine(line, kMovieTag, movie_))
      has_movie = true;
  }

  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i != 0)
      out += '\n';
    out += lines[i];
  }
  if (!has_text || !has_voice || !has_movie) {
    if (!out.empty() && out.back() != '\n')
      out += "\r\n";
    if (!has_text) {
      out += TagLine(kTextTag, text_);
      out += "\r\n";
    }
    if (!has_voice) {
      out += TagLine(kVoiceTag, voice_);
      out += "\r\n";
    }
    if (!has_movie) {
      out += TagLine(kMovieTag, movie_);
      out += "\r\n";
    }
  }

  std::ofstream os(file, std::ios::binary | std::ios::trunc);
  if (!os)
    return false;
  os.write(out.data(), static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(os);
}

} // namespace bd::installer
