/**
 * @file    vfs/mounts.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/mounts.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <tuple>
#include <utility>

#include <rex/hash.h>
#include <rex/platform/file.h>

#include "core/logging.h"

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>
#include <xxhash.h>

namespace bd::vfs {

namespace {

template <typename Map> std::vector<Key> MapKeys(const Map &map) {
  std::vector<Key> keys;
  keys.reserve(map.size());
  for (const auto &entry : map)
    keys.push_back(entry.first);
  return keys;
}

constexpr std::string_view kIPKExt = ".ipk";
constexpr std::string_view kPackDir = "pack";

// Both 9 characters, so a bare length would not say which one it counts.
constexpr std::string_view kDatabasePrefix = "database\\";
constexpr std::string_view kDownloadPrefix = "download\\";

// bdResolveDownloadPath rewrites database\X to download\X, and on console the
// rewritten path resolves in the pack's install cache. The recomp has no such
// cache, so anything indexing a database\ record also indexes it as download\.
std::optional<Key> DownloadAlias(const Key &key) {
  if (!key.str().starts_with(kDatabasePrefix))
    return std::nullopt;
  return Key::FromRelative("download") / key.Slice(kDatabasePrefix.size());
}

// IPK1 archive layout. Little-endian on disc, which is also the host's order,
// so the fields read as they sit.
struct IpkHeader_t {
  char magic[4]; // "IPK1"
  u32 align;
  u32 count;
  u32 total;
};
static_assert(offsetof(IpkHeader_t, count) == 0x08);
static_assert(sizeof(IpkHeader_t) == 0x10);

struct IpkRecord_t {
  char name[0x40]; // NUL-padded, not necessarily NUL-terminated
  u32 compressed;
  u32 zsize;
  u32 offset;
  u32 size;
  u32 timestamp;
  u8 _pad054[0x60 - 0x54];
};
static_assert(offsetof(IpkRecord_t, compressed) == 0x40);
static_assert(offsetof(IpkRecord_t, zsize) == 0x44);
static_assert(offsetof(IpkRecord_t, offset) == 0x48);
static_assert(offsetof(IpkRecord_t, size) == 0x4C);
static_assert(sizeof(IpkRecord_t) == 0x60);

// ShippedPackMount::Entry::flags.
constexpr u32 kEntryCompressed = 1u << 0;
constexpr u32 kEntryAlias = 1u << 1; // a second name for a record listed by its
                                     // own name elsewhere in the index

// A record's bytes, given what the archive holds for it.
std::vector<u8> Inflate(std::vector<u8> packed, u32 size, bool compressed) {
  if (!compressed) {
    packed.resize(size);
    return packed;
  }

  std::vector<u8> out(size);
  mz_ulong out_len = size;
  if (mz_uncompress(out.data(), &out_len, packed.data(),
                    static_cast<mz_ulong>(packed.size())) != MZ_OK)
    return {};
  out.resize(out_len);
  return out;
}

// The built index as it sits on disk: header, entry array, then the name blob
// the entries index into. Host byte order and host layout, since nothing but
// the machine that wrote it reads it back.
constexpr char kIndexMagic[8] = {'B', 'D', 'P', 'K', 'I', 'D', 'X', '1'};

// Bump on any change to what the index holds or how.
constexpr u32 kIndexVersion = 1;

struct IndexHeader_t {
  char magic[8];
  u32 version;
  u32 entry_count;
  u64 stamp;
  u64 blob_bytes;
};
static_assert(sizeof(IndexHeader_t) == 0x20);

// The stamp covers which archives the pack tree holds and not their contents,
// so a cached index is stale exactly when one is added, dropped, or rewritten.
void AppendStamped(std::string &material, std::string_view text) {
  material.append(text);
  material.push_back('\0');
}

void AppendStamped(std::string &material, u64 value) {
  material.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

// 'fn' takes the normalized record name and the record, which is only valid
// for the duration of that call.
template <typename Fn>
bool ForEachIPKRecord(const std::filesystem::path &path, Fn &&fn) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;

  f.seekg(0, std::ios::end);
  const auto end = f.tellg();
  if (end < 0)
    return false;
  f.seekg(0);

  IpkHeader_t header;
  f.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (static_cast<size_t>(f.gcount()) != sizeof(header) ||
      std::memcmp(header.magic, "IPK1", sizeof(header.magic)) != 0) {
    REXBD_WARN("vfs not an IPK1 archive: {}", path.string());
    return false;
  }

  // The count is a disc word, so it sizes the allocation only once the file has
  // agreed it could hold that many.
  const u64 room =
      (static_cast<u64>(end) - sizeof(IpkHeader_t)) / sizeof(IpkRecord_t);
  std::vector<IpkRecord_t> table(std::min<u64>(header.count, room));

  // One read: a run of 96-byte reads costs more than the bytes it moves.
  f.read(reinterpret_cast<char *>(table.data()),
         static_cast<std::streamsize>(table.size() * sizeof(IpkRecord_t)));
  table.resize(static_cast<size_t>(f.gcount()) / sizeof(IpkRecord_t));

  for (const IpkRecord_t &record : table) {
    const size_t name_len = ::strnlen(record.name, sizeof(record.name));
    if (name_len == 0 || record.size == 0)
      continue;

    fn(Key::FromRelative(std::string_view(record.name, name_len)), record);
  }
  return true;
}

} // namespace

void GeneratedMount::Set(Key key, ContentProvider provider) {
  entries_[std::move(key)] = std::move(provider);
}

std::optional<u64> GeneratedMount::Stat(const Key &key) const {
  auto it = entries_.find(key);
  if (it == entries_.end())
    return std::nullopt;
  const auto bytes = it->second();
  if (bytes.empty())
    return std::nullopt;
  return bytes.size();
}

std::optional<std::vector<u8>> GeneratedMount::Read(const Key &key) const {
  auto it = entries_.find(key);
  if (it == entries_.end())
    return std::nullopt;
  return it->second();
}

std::vector<Key> GeneratedMount::Keys() const { return MapKeys(entries_); }

std::shared_ptr<LooseMount>
LooseMount::Scan(const std::filesystem::path &root) {
  auto mount = std::make_shared<LooseMount>();

  std::error_code walk_ec;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root, walk_ec)) {
    if (!entry.is_regular_file())
      continue;
    std::error_code ec;
    auto relative = std::filesystem::relative(entry.path(), root, ec);
    if (ec)
      continue;
    mount->Set(Key::FromRelative(relative.string()), entry.path());
  }
  if (walk_ec)
    REXBD_ERROR("vfs error walking {}: {}", root.string(), walk_ec.message());

  return mount;
}

void LooseMount::Set(Key key, std::filesystem::path path) {
  entries_[std::move(key)] = std::move(path);
}

std::optional<u64> LooseMount::Stat(const Key &key) const {
  auto it = entries_.find(key);
  if (it == entries_.end())
    return std::nullopt;

  std::error_code ec;
  const auto size = std::filesystem::file_size(it->second, ec);
  if (ec || size == 0) {
    REXBD_ERROR("vfs indexed file unreadable: {}", it->second.string());
    return std::nullopt;
  }
  return size;
}

std::optional<std::vector<u8>> LooseMount::Read(const Key &key) const {
  auto it = entries_.find(key);
  if (it == entries_.end())
    return std::nullopt;
  auto bytes = rex::platform::ReadHostFile(it->second);
  return bytes ? std::move(*bytes) : std::vector<u8>{};
}

std::vector<Key> LooseMount::Keys() const { return MapKeys(entries_); }

std::shared_ptr<IPKMount> IPKMount::Open(const std::filesystem::path &path,
                                         Key key_prefix) {
  auto mount = std::make_shared<IPKMount>();
  mount->path_ = path;
  mount->prefix_ = std::move(key_prefix);

  const bool ok =
      ForEachIPKRecord(path, [&](Key key, const IpkRecord_t &record) {
        Record r;
        r.compressed = record.compressed != 0;
        r.zsize = record.zsize;
        r.offset = record.offset;
        r.size = record.size;

        if (auto alias = DownloadAlias(key))
          mount->entries_[*alias] = r;
        mount->entries_[std::move(key)] = r;
      });

  if (!ok)
    return nullptr;

  mount->mapped_ = rex::memory::MappedMemory::Open(
      path, rex::memory::MappedMemory::Mode::kRead);
  return mount;
}

std::vector<Key> IPKMount::Keys() const {
  if (prefix_.empty())
    return MapKeys(entries_);

  std::vector<Key> keys;
  keys.reserve(entries_.size());
  for (const auto &entry : entries_) {
    // A download\ alias names a database\ record already listed.
    if (entry.first.str().starts_with(kDownloadPrefix) &&
        entries_.contains(Key::FromRelative("database") /
                          entry.first.Slice(kDownloadPrefix.size())))
      continue;
    keys.push_back(prefix_ / entry.first);
  }
  return keys;
}

const IPKMount::Record *IPKMount::Find(const Key &key) const {
  auto it = entries_.find(key);
  if (it == entries_.end() && !prefix_.empty()) {
    const std::string_view path = key.str();
    const std::string_view pre = prefix_.str();
    if (path.starts_with(pre) && path.size() > pre.size() &&
        path[pre.size()] == '\\')
      it = entries_.find(key.Slice(pre.size() + 1));
  }
  return it == entries_.end() ? nullptr : &it->second;
}

std::optional<u64> IPKMount::Stat(const Key &key) const {
  const Record *record = Find(key);
  if (!record)
    return std::nullopt;
  return record->size;
}

std::optional<std::vector<u8>> IPKMount::Read(const Key &key) const {
  const Record *found = Find(key);
  if (!found)
    return std::nullopt;
  const Record &r = *found;

  std::vector<u8> packed(r.zsize);
  if (mapped_ && size_t{r.offset} + r.zsize <= mapped_->GetSize()) {
    std::memcpy(packed.data(), mapped_->GetData() + r.offset, r.zsize);
  } else {
    std::ifstream f(path_, std::ios::binary);
    if (!f)
      return std::vector<u8>{};
    f.seekg(r.offset);
    f.read(reinterpret_cast<char *>(packed.data()), r.zsize);
    if (static_cast<u32>(f.gcount()) != r.zsize)
      return std::vector<u8>{};
  }

  return Inflate(std::move(packed), r.size, r.compressed);
}

std::shared_ptr<ShippedPackMount>
ShippedPackMount::Scan(const std::filesystem::path &game_root,
                       const std::filesystem::path &index_cache) {
  auto mount = std::make_shared<ShippedPackMount>();
  const auto pack_root = game_root / kPackDir;

  const u64 stamp = mount->ScanArchives(pack_root, game_root);

  const bool cached = mount->LoadIndex(index_cache, stamp);
  if (!cached) {
    mount->BuildIndex();
    mount->SaveIndex(index_cache, stamp);
  }

  REXBD_INFO("vfs {} shipped archive(s), {} record name(s) under {} ({})",
          mount->archives_.size(), mount->entries_.size(), pack_root.string(),
          cached ? "cached index" : "read from archives");
  return mount;
}

u64 ShippedPackMount::ScanArchives(const std::filesystem::path &pack_root,
                                   const std::filesystem::path &game_root) {
  struct Found {
    std::filesystem::path path;
    Key key;
    u64 size = 0;
    i64 written = 0;
  };
  std::vector<Found> found;

  std::error_code walk_ec;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(pack_root, walk_ec)) {
    if (!entry.is_regular_file())
      continue;
    auto name = Key::FromRelative(entry.path().filename().string());
    if (!name.str().ends_with(kIPKExt))
      continue;

    Found archive;
    archive.path = entry.path();

    std::error_code ec;
    auto relative = std::filesystem::relative(archive.path, game_root, ec);
    archive.key = ec ? Key::FromRelative(archive.path.filename().string())
                     : Key::FromRelative(relative.string());

    // Off the directory entry the walk already filled in. Reopening 1700 files
    // for a stat is the cost the index exists to avoid.
    archive.size = entry.file_size(ec);
    if (ec)
      archive.size = 0;
    archive.written = entry.last_write_time(ec).time_since_epoch().count();
    if (ec)
      archive.written = 0;

    found.push_back(std::move(archive));
  }
  if (walk_ec)
    REXBD_ERROR("vfs error walking {}: {}", pack_root.string(),
             walk_ec.message());

  // Sorted so that a record name two archives share always resolves to the
  // same one, run to run, and so the stamp does not follow walk order.
  std::sort(found.begin(), found.end(),
            [](const Found &a, const Found &b) { return a.path < b.path; });

  archives_.reserve(found.size());
  archive_keys_.reserve(found.size());

  std::string material;
  AppendStamped(material, game_root.string());
  for (Found &archive : found) {
    AppendStamped(material, archive.key.str());
    AppendStamped(material, archive.size);
    AppendStamped(material, static_cast<u64>(archive.written));
    archives_.push_back(std::move(archive.path));
    archive_keys_.push_back(std::move(archive.key));
  }
  return XXH3_64bits(material.data(), material.size());
}

// Only the record names stay resident. The archive they came from is dropped
// and reopens if something reads it.
void ShippedPackMount::BuildIndex() {
  std::vector<char> blob;
  std::vector<Entry> entries;

  const auto intern = [&blob](std::string_view text) {
    const u32 offset = static_cast<u32>(blob.size());
    blob.insert(blob.end(), text.begin(), text.end());
    return std::pair<u32, u32>(offset, static_cast<u32>(text.size()));
  };
  const auto add = [&](std::string_view name, const IpkRecord_t &record,
                       u32 archive, u32 flags) {
    Entry entry;
    std::tie(entry.name_off, entry.name_len) = intern(name);
    entry.archive = archive;
    entry.offset = record.offset;
    entry.zsize = record.zsize;
    entry.size = record.size;
    entry.flags = flags | (record.compressed != 0 ? kEntryCompressed : 0);
    entries.push_back(entry);
  };

  for (u32 archive = 0; archive < archives_.size(); ++archive) {
    ForEachIPKRecord(archives_[archive], [&, archive](
                                             Key key,
                                             const IpkRecord_t &record) {
      const std::string_view name = key.str();

      // A leading bd_root\ / bd_pack_common\ segment names the point the
      // engine mounts the archive at, not part of the guest path.
      const auto seg = name.find('\\');
      if (seg != std::string_view::npos && name.starts_with("bd_"))
        add(name.substr(seg + 1), record, archive, kEntryAlias);

      if (auto alias = DownloadAlias(key))
        add(alias->str(), record, archive, kEntryAlias);

      add(name, record, archive, 0);
    });
  }

  const auto name_of = [&blob](const Entry &entry) {
    return std::string_view(blob.data() + entry.name_off, entry.name_len);
  };

  // Stable, so among equal names the first added wins: the earliest archive in
  // sorted order, and within one archive the earliest record.
  std::stable_sort(
      entries.begin(), entries.end(),
      [&](const Entry &a, const Entry &b) { return name_of(a) < name_of(b); });
  entries.erase(std::unique(entries.begin(), entries.end(),
                            [&](const Entry &a, const Entry &b) {
                              return name_of(a) == name_of(b);
                            }),
                entries.end());

  // The blob still holds every name that lost a collision, and it is about to
  // be written out, so it is rebuilt around what survived.
  std::vector<char> packed;
  packed.reserve(blob.size());
  for (Entry &entry : entries) {
    const std::string_view name = name_of(entry);
    entry.name_off = static_cast<u32>(packed.size());
    packed.insert(packed.end(), name.begin(), name.end());
  }

  entries_ = std::move(entries);
  blob_ = std::move(packed);
}

bool ShippedPackMount::LoadIndex(const std::filesystem::path &path, u64 stamp) {
  if (path.empty())
    return false;

  const auto read = rex::platform::ReadHostFile(path);
  if (!read)
    return false;
  const std::vector<u8> &file = *read;
  if (file.size() < sizeof(IndexHeader_t))
    return false;

  IndexHeader_t header;
  std::memcpy(&header, file.data(), sizeof(header));
  if (std::memcmp(header.magic, kIndexMagic, sizeof(kIndexMagic)) != 0 ||
      header.version != kIndexVersion || header.stamp != stamp)
    return false;

  const size_t entry_bytes = size_t{header.entry_count} * sizeof(Entry);
  if (file.size() != sizeof(header) + entry_bytes + header.blob_bytes)
    return false;

  std::vector<Entry> entries(header.entry_count);
  std::memcpy(entries.data(), file.data() + sizeof(header), entry_bytes);
  std::vector<char> blob(static_cast<size_t>(header.blob_bytes));
  std::memcpy(blob.data(), file.data() + sizeof(header) + entry_bytes,
              blob.size());

  // A lookup binary searches this on trust, so check what it trusts: every span
  // inside the blob, and names that ascend.
  std::string_view previous;
  for (size_t i = 0; i < entries.size(); ++i) {
    const Entry &entry = entries[i];
    if (size_t{entry.name_off} + entry.name_len > blob.size() ||
        entry.archive >= archives_.size())
      return false;

    const std::string_view name(blob.data() + entry.name_off, entry.name_len);
    if (i > 0 && name <= previous)
      return false;
    previous = name;
  }

  entries_ = std::move(entries);
  blob_ = std::move(blob);
  return true;
}

void ShippedPackMount::SaveIndex(const std::filesystem::path &path,
                                 u64 stamp) const {
  if (path.empty() || entries_.empty())
    return;

  IndexHeader_t header{};
  std::memcpy(header.magic, kIndexMagic, sizeof(kIndexMagic));
  header.version = kIndexVersion;
  header.entry_count = static_cast<u32>(entries_.size());
  header.stamp = stamp;
  header.blob_bytes = blob_.size();

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  // Written aside and renamed over, so a run that dies mid-write leaves the
  // last good index rather than half of this one.
  auto temp = path;
  temp += ".tmp";
  {
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(&header), sizeof(header));
    f.write(reinterpret_cast<const char *>(entries_.data()),
            static_cast<std::streamsize>(entries_.size() * sizeof(Entry)));
    f.write(blob_.data(), static_cast<std::streamsize>(blob_.size()));
    if (!f) {
      REXBD_WARN("vfs pack index not written: {}", temp.string());
      f.close();
      std::filesystem::remove(temp, ec);
      return;
    }
  }

  std::filesystem::rename(temp, path, ec);
  if (ec) {
    REXBD_WARN("vfs pack index not replaced: {}", ec.message());
    std::filesystem::remove(temp, ec);
  }
}

// The index carries the record, so this is the whole of it: no archive is
// opened to answer what a file weighs.
std::optional<u64> ShippedPackMount::Stat(const Key &key) const {
  const Entry *entry = Resolve(key);
  return entry ? std::optional<u64>(entry->size) : std::nullopt;
}

std::optional<std::vector<u8>> ShippedPackMount::Read(const Key &key) const {
  const Entry *entry = Resolve(key);
  if (!entry)
    return std::nullopt;

  std::ifstream f(archives_[entry->archive], std::ios::binary);
  if (!f)
    return std::vector<u8>{};
  f.seekg(entry->offset);
  std::vector<u8> packed(entry->zsize);
  f.read(reinterpret_cast<char *>(packed.data()), entry->zsize);
  if (static_cast<u32>(f.gcount()) != entry->zsize)
    return std::vector<u8>{};

  return Inflate(std::move(packed), entry->size,
                 (entry->flags & kEntryCompressed) != 0);
}

// Under the archive holding it, since a record name carries no usable path of
// its own. Aliases are skipped: they name a record already listed.
std::vector<Key> ShippedPackMount::Keys() const {
  std::vector<Key> keys;
  keys.reserve(entries_.size());
  for (const Entry &entry : entries_) {
    if ((entry.flags & kEntryAlias) != 0 ||
        entry.archive >= archive_keys_.size())
      continue;
    keys.push_back(archive_keys_[entry.archive] /
                   Key::FromRelative(Name(entry)));
  }
  return keys;
}

const ShippedPackMount::Entry *
ShippedPackMount::Find(std::string_view name) const {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), name,
                                   [this](const Entry &entry,
                                          std::string_view want) {
                                     return Name(entry) < want;
                                   });
  return it != entries_.end() && Name(*it) == name ? &*it : nullptr;
}

// Longest tail first, so a request that names a record outright beats one that
// only matches after its leading directories are dropped.
const ShippedPackMount::Entry *
ShippedPackMount::Resolve(const Key &key) const {
  const std::string_view path = key.str();
  for (size_t at = 0; at <= path.size();) {
    if (const Entry *entry = Find(path.substr(at)))
      return entry;
    const auto sep = path.find('\\', at);
    if (sep == std::string_view::npos)
      break;
    at = sep + 1;
  }
  return nullptr;
}

} // namespace bd::vfs
