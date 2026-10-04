/**
 * @file    vfs/file_system.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/file_system.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include "core/encoding.h"
#include "core/logging.h"

namespace bd::vfs {

namespace {

enum class ByteOrder { None, Big, Little };

ByteOrder ByteOrderOf(const std::vector<u8> &bytes) {
  if (bytes.size() < 2)
    return ByteOrder::None;
  if (bytes[0] == 0xFE && bytes[1] == 0xFF)
    return ByteOrder::Big;
  if (bytes[0] == 0xFF && bytes[1] == 0xFE)
    return ByteOrder::Little;
  return ByteOrder::None;
}

template <class Char> bool IsSpace(Char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

template <class View> View Trim(View s) {
  while (!s.empty() && IsSpace(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && IsSpace(s.back()))
    s.remove_suffix(1);
  return s;
}

// A row already in the table is recognized the way ShadowForge recognizes it
// when it bakes one: whole line, surrounding whitespace dropped, ASCII case
// folded.
template <class Str, class View> Str Fold(View s) {
  Str out(Trim(s));
  for (auto &c : out)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<typename Str::value_type>(c - 'A' + 'a');
  return out;
}

template <class Str, class View> std::unordered_set<Str> Lines(View text) {
  std::unordered_set<Str> lines;
  for (size_t at = 0; at < text.size();) {
    const size_t nl = text.find('\n', at);
    lines.insert(Fold<Str>(
        text.substr(at, nl == View::npos ? View::npos : nl - at)));
    if (nl == View::npos)
      break;
    at = nl + 1;
  }
  return lines;
}

template <class Str, class View, class Encode>
Str Extend(View text, const std::vector<std::string> &rows, Encode encode) {
  std::unordered_set<Str> present = Lines<Str>(text);
  Str out(text);
  const typename Str::value_type crlf[] = {'\r', '\n', 0};
  if (!out.empty() && out.back() != '\n')
    out += crlf;
  for (const std::string &row : rows) {
    const Str line = encode(Trim(std::string_view(row)));
    if (!present.insert(Fold<Str>(View(line))).second)
      continue;
    out += line;
    out += crlf;
  }
  return out;
}

std::vector<u8> ExtendU16(const std::vector<u8> &base,
                          const std::vector<std::string> &rows,
                          ByteOrder order) {
  std::u16string text;
  text.reserve(base.size() / 2);
  for (size_t i = 2; i + 1 < base.size(); i += 2) {
    const u16 unit = order == ByteOrder::Big
                         ? static_cast<u16>((base[i] << 8) | base[i + 1])
                         : static_cast<u16>((base[i + 1] << 8) | base[i]);
    text.push_back(static_cast<char16_t>(unit));
  }

  const std::u16string out = Extend<std::u16string>(
      std::u16string_view(text), rows,
      [](std::string_view row) { return Utf8ToU16(row); });

  std::vector<u8> bytes;
  bytes.reserve(2 + out.size() * 2);
  bytes.push_back(base[0]);
  bytes.push_back(base[1]);
  for (const char16_t c : out) {
    const u8 hi = static_cast<u8>(c >> 8);
    const u8 lo = static_cast<u8>(c & 0xFF);
    if (order == ByteOrder::Big) {
      bytes.push_back(hi);
      bytes.push_back(lo);
    } else {
      bytes.push_back(lo);
      bytes.push_back(hi);
    }
  }
  return bytes;
}

std::vector<u8> ExtendShiftJIS(const std::vector<u8> &base,
                               const std::vector<std::string> &rows) {
  const std::string_view text(reinterpret_cast<const char *>(base.data()),
                              base.size());
  const std::string out = Extend<std::string>(
      text, rows, [](std::string_view row) { return Utf8ToSjis(row); });
  return std::vector<u8>(out.begin(), out.end());
}

// In the table's own encoding: UTF-16 behind a byte order mark, Shift-JIS
// otherwise.
std::vector<u8> ExtendTable(const std::vector<u8> &base,
                            const std::vector<std::string> &rows) {
  const ByteOrder order = ByteOrderOf(base);
  if (order != ByteOrder::None)
    return ExtendU16(base, rows, order);
  return ExtendShiftJIS(base, rows);
}

} // namespace

const char *ToString(MountKind kind) {
  switch (kind) {
  case MountKind::Generated:
    return "generated";
  case MountKind::Loose:
    return "loose";
  case MountKind::Archive:
    return "archive";
  case MountKind::ShippedPack:
    return "shipped-pack";
  }
  return "?";
}

void FileSystem::Add(std::string name, int priority,
                     std::shared_ptr<Mount> mount) {
  if (!mount)
    return;

  BD_INFO("[vfs] mount '{}' prio {} {} ({} keys)", name, priority,
          ToString(mount->Kind()), mount->KeyCount());
  Insert({std::move(name), priority, 0, std::move(mount), nullptr});
}

void FileSystem::AddPatch(std::string name, int priority, DbRows rows) {
  if (rows.empty())
    return;

  BD_INFO("[vfs] patch '{}' prio {} ({} tables)", name, priority,
          rows.size());
  Insert({std::move(name), priority, 0, nullptr,
          std::make_shared<const DbRows>(std::move(rows))});
}

void FileSystem::Insert(Entry entry) {
  revision_.fetch_add(1, std::memory_order_release);

  std::lock_guard lock(mutex_);
  std::erase_if(mounts_, [&](const Entry &e) { return e.name == entry.name; });
  entry.seq = next_seq_++;
  mounts_.push_back(std::move(entry));
  std::stable_sort(mounts_.begin(), mounts_.end(),
                   [](const Entry &a, const Entry &b) {
                     if (a.priority != b.priority)
                       return a.priority > b.priority;
                     return a.seq > b.seq;
                   });
}

void FileSystem::SetDiscRoot(std::filesystem::path root) {
  std::lock_guard lock(mutex_);
  disc_root_ = std::move(root);
}

void FileSystem::Remove(std::string_view name) {
  revision_.fetch_add(1, std::memory_order_release);

  std::lock_guard lock(mutex_);
  if (std::erase_if(mounts_, [&](const Entry &e) { return e.name == name; }))
    BD_INFO("[vfs] unmount '{}'", name);
}

std::vector<FileSystem::Entry> FileSystem::Snapshot(Tier tier) const {
  std::lock_guard lock(mutex_);
  std::vector<Entry> entries;
  std::copy_if(mounts_.begin(), mounts_.end(), std::back_inserter(entries),
               [tier](const Entry &e) {
                 return (e.priority >= kPriorityEngine
                             ? Tier::Overlay
                             : Tier::Fallback) == tier;
               });
  return entries;
}

std::optional<StatResult> FileSystem::Stat(const Key &key, Tier tier) const {
  for (const auto &e : Snapshot(tier)) {
    if (e.patch) {
      if (!e.patch->contains(key))
        continue;
      auto hit = Read(key, tier);
      if (!hit)
        return std::nullopt;
      return StatResult{hit->mount, hit->kind, hit->bytes.size()};
    }
    if (auto size = e.mount->Stat(key))
      return StatResult{e.name, e.mount->Kind(), *size};
  }
  return std::nullopt;
}

std::optional<ReadResult> FileSystem::ReadUnpatched(const Key &key,
                                                    Tier tier) const {
  for (const auto &e : Snapshot(tier)) {
    if (!e.mount)
      continue;
    auto bytes = e.mount->Read(key);
    if (!bytes)
      continue;
    // Indexed but unreadable: keep walking, and the guest's own IO still
    // backstops us, so a broken overlay degrades to the shipped file.
    if (bytes->empty()) {
      BD_ERROR("[vfs] mount '{}' holds '{}' but produced nothing", e.name,
               key.str());
      continue;
    }
    return ReadResult{e.name, e.mount->Kind(), std::move(*bytes)};
  }
  return std::nullopt;
}

std::optional<ReadResult> FileSystem::ReadDisc(const Key &key) const {
  std::filesystem::path path;
  {
    std::lock_guard lock(mutex_);
    path = disc_root_;
  }
  if (path.empty())
    return std::nullopt;
  const std::string_view rel = key.str();
  for (size_t at = 0; at <= rel.size();) {
    const size_t sep = rel.find('\\', at);
    path /= rel.substr(at, sep == std::string_view::npos ? std::string_view::npos
                                                          : sep - at);
    if (sep == std::string_view::npos)
      break;
    at = sep + 1;
  }

  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return std::nullopt;
  const auto size = f.tellg();
  if (size <= 0)
    return std::nullopt;
  f.seekg(0);
  std::vector<u8> bytes(static_cast<size_t>(size));
  f.read(reinterpret_cast<char *>(bytes.data()), size);
  if (f.gcount() != size)
    return std::nullopt;
  return ReadResult{"disc", MountKind::Loose, std::move(bytes)};
}

std::optional<ReadResult> FileSystem::Read(const Key &key, Tier tier) const {
  // Only the patches above the first mount holding the key apply, outermost
  // first.
  const std::vector<Entry> snapshot = Snapshot(tier);
  std::vector<const std::vector<std::string> *> patches;
  for (const auto &e : snapshot) {
    if (e.mount) {
      if (e.mount->Stat(key))
        break;
      continue;
    }
    if (const auto it = e.patch->find(key); it != e.patch->end())
      patches.push_back(&it->second);
  }
  if (patches.empty())
    return ReadUnpatched(key, tier);

  const u64 revision = revision_.load(std::memory_order_acquire);
  {
    std::lock_guard lock(composed_mutex_);
    const auto it = composed_.find(key);
    if (it != composed_.end() && it->second.revision == revision)
      return it->second.result;
  }

  std::optional<ReadResult> base = ReadUnpatched(key, tier);
  if (!base && tier == Tier::Overlay)
    base = ReadDisc(key);
  if (!base && tier == Tier::Overlay)
    base = ReadUnpatched(key, Tier::Fallback);
  if (!base) {
    BD_WARN("[vfs] '{}' is patched but nothing serves it", key.str());
    return std::nullopt;
  }

  size_t offered = 0;
  for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
    base->bytes = ExtendTable(base->bytes, **it);
    offered += (*it)->size();
  }
  BD_INFO("[vfs] '{}' from '{}' extended by {} patch(es), {} row(s)",
          key.str(), base->mount, patches.size(), offered);

  std::lock_guard lock(composed_mutex_);
  composed_[key] = Composed{revision, *base};
  return base;
}

void FileSystem::RebuildIndex() const {
  const u64 revision = revision_.load(std::memory_order_acquire);
  if (indexed_revision_ == revision)
    return;

  index_.clear();
  for (const auto tier : {Tier::Overlay, Tier::Fallback}) {
    for (const auto &entry : Snapshot(tier)) {
      if (!entry.mount)
        continue;
      for (const auto &key : entry.mount->Keys()) {
        const std::string_view path = key.str();
        // Every separator splits one parent from one child name, and the tail
        // past the last one is the only child that is not itself a directory.
        for (size_t at = 0;;) {
          const auto sep = path.find('\\', at);
          const bool leaf = sep == std::string_view::npos;
          auto name = path.substr(at, leaf ? std::string_view::npos : sep - at);
          if (name.empty())
            break;

          auto &children = index_[at == 0 ? Key() : key.Slice(0, at - 1)];
          auto [it, inserted] = children.emplace(std::string(name), !leaf);
          if (!inserted && !leaf)
            it->second = true;
          if (leaf)
            break;
          at = sep + 1;
        }
      }
    }
  }
  indexed_revision_ = revision;
  BD_INFO("[vfs] directory index: {} directories", index_.size());
}

std::vector<DirEntry> FileSystem::List(const Key &dir) const {
  std::lock_guard lock(index_mutex_);
  RebuildIndex();

  std::vector<DirEntry> entries;
  auto it = index_.find(dir);
  if (it == index_.end())
    return entries;

  entries.reserve(it->second.size());
  for (const auto &[name, is_dir] : it->second)
    entries.push_back({name, is_dir});
  return entries;
}

} // namespace bd::vfs
