/**
 * @file    engine/game_text.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/game_text.h"

#include "core/i18n.h"
#include "core/logging.h"
#include "engine/language.h"
#include "vfs/vfs.h"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/memory/endian.h>
#include <rex/string/text.h>

namespace bd::engine {
namespace {

// Where each borrowable table lives, verified against an installed pack dir.
// {} is the locale code.
struct Source {
  const char *alias;
  const char *pack; // relative to the game data root
  const char *key;  // record name inside the pack
  bool flat;        // "id,text" rows, not "string,KEY,VALUE" among others
};

constexpr Source kSources[] = {
    {"cfg_str", "pack/packmem_{}.ipk", "cfg_str_{}.u16", false},
    {"cmp_str", "pack/packmem_{}.ipk", "cmp_str_{}.u16", false},
    {"cmn_str", "pack/packmem_{}.ipk", "cmn_str_{}.u16", false},
    {"common", "pack/!necessity.ipk", "bd_gamedat\\common_{}.u16", true},
    {"warn_mes", "pack/!necessity.ipk", "pack\\warn_mes_{}.u16", false},
};

using Table = std::unordered_map<std::string, std::string>;

std::mutex g_mutex;
std::map<std::string, Table> g_tables; // keyed "<alias>/<code>"

std::string Substitute(const char *pattern, const char *code) {
  std::string out = pattern;
  const auto pos = out.find("{}");
  if (pos != std::string::npos)
    out.replace(pos, 2, code);
  return out;
}

const Source *FindSource(std::string_view alias) {
  for (const auto &s : kSources)
    if (alias == s.alias)
      return &s;
  return nullptr;
}

// Fields may be quoted, and the message tables carry commas inside them.
std::vector<std::string> SplitRow(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c != '"') {
        field.push_back(c);
      } else if (i + 1 < line.size() && line[i + 1] == '"') {
        field.push_back('"');
        ++i;
      } else {
        quoted = false;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      fields.push_back(std::move(field));
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  fields.push_back(std::move(field));
  return fields;
}

Table Parse(const std::vector<u8> &bytes, bool flat) {
  std::u16string wide(bytes.size() / 2, u'\0');
  rex::memory::copy_and_swap(wide.data(), reinterpret_cast<const char16_t *>(bytes.data()),
                             wide.size());
  if (!wide.empty() && wide.front() == 0xFEFF)
    wide.erase(0, 1);

  Table table;
  const std::string text = rex::string::ToUTF8(wide);
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos)
      end = text.size();
    std::string_view line(text.data() + pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty() || line.front() == '#')
      continue;

    auto fields = SplitRow(line);
    if (flat) {
      if (fields.size() >= 2 && !fields[0].empty() && !fields[1].empty())
        table.emplace(fields[0], fields[1]);
    } else if (fields.size() >= 3 && fields[0] == "string" &&
               !fields[1].empty()) {
      table.emplace(fields[1], fields[2]);
    }
  }
  return table;
}

const Table *GetOrLoadTable(const Source &src, const char *code) {
  const std::string id = std::string(src.alias) + "/" + code;
  if (auto it = g_tables.find(id); it != g_tables.end())
    return it->second.empty() ? nullptr : &it->second;

  // Nothing is cached until the data root exists, so the first lookups
  // during boot do not poison the table for the rest of the session.
  const auto &root = bd::vfs::VFS::Get().Paths().Game();
  if (root.empty())
    return nullptr;

  Table loaded;
  if (auto pack = bd::vfs::IPKMount::Open(root / Substitute(src.pack, code))) {
    if (auto bytes =
            pack->Read(bd::vfs::Key::FromRelative(Substitute(src.key, code)));
        bytes && !bytes->empty())
      loaded = Parse(*bytes, src.flat);
  }

  Table &slot = g_tables.emplace(id, std::move(loaded)).first->second;
  if (slot.empty()) {
    REXBD_WARN("i18n no borrowable terms in {}", id);
    return nullptr;
  }
  REXBD_INFO("i18n borrowed {} terms from {}", slot.size(), id);
  return &slot;
}

bool Resolve(std::string_view alias, std::string_view key, u32 locale,
             std::string &out) {
  const Source *src = FindSource(alias);
  if (!src)
    return false;

  const char *code = Locale(locale).CodeLower();
  if (!*code)
    return false;

  std::lock_guard lock(g_mutex);
  const Table *table = GetOrLoadTable(*src, code);
  if (!table)
    return false;

  auto it = table->find(std::string(key));
  if (it == table->end())
    return false;
  out = it->second;
  return true;
}

} // namespace

void InstallGameTermResolver() { i18n::SetGameTermResolver(&Resolve); }

} // namespace bd::engine
