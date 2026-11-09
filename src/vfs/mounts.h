/**
 * @file    vfs/mounts.h
 * @brief   The Mount kinds: generated content, loose host files, one IPK
 *          archive, and the whole shipped pack set.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include "vfs/file_system.h"

#include <rex/memory/mapped_memory.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace bd::vfs {

// Runs on every read, so a generated file always reflects live state.
using ContentProvider = std::function<std::vector<u8>()>;

// Stat has to run the provider to measure the result, so providers must stay
// cheap, since the guest stats a path before every read.
class GeneratedMount final : public Mount {
public:
  void Set(Key key, ContentProvider provider);

  std::optional<u64> Stat(const Key &key) const override;
  std::optional<std::vector<u8>> Read(const Key &key) const override;
  std::vector<Key> Keys() const override;
  MountKind Kind() const override { return MountKind::Generated; }
  size_t KeyCount() const override { return entries_.size(); }

private:
  std::unordered_map<Key, ContentProvider> entries_;
};

// Host files, keyed by guest path.
class LooseMount final : public Mount {
public:
  // Index every file under 'root', keyed by its path relative to it.
  static std::shared_ptr<LooseMount> Scan(const std::filesystem::path &root);

  void Set(Key key, std::filesystem::path path);

  std::optional<u64> Stat(const Key &key) const override;
  std::optional<std::vector<u8>> Read(const Key &key) const override;
  std::vector<Key> Keys() const override;
  MountKind Kind() const override { return MountKind::Loose; }
  size_t KeyCount() const override { return entries_.size(); }

private:
  std::unordered_map<Key, std::filesystem::path> entries_;
};

// One IPK1 archive. The record table is parsed once on open and entry bytes are
// inflated per read, so only the index stays resident.
//
// A record name says nothing about where the file lives: the engine resolves a
// packed file by basename alone (bdPackIndexSearch), so archives name records
// however they like, some rooted at the disc root, some at the point the engine
// mounts them, most flat. There is no tree to recover from them, so enumeration
// reports each record under 'key_prefix', the archive's own location, and both
// forms of a key read.
class IPKMount final : public Mount {
public:
  // Null if the file is missing or is not an IPK1 archive.
  static std::shared_ptr<IPKMount> Open(const std::filesystem::path &path,
                                        Key key_prefix = {});

  std::optional<u64> Stat(const Key &key) const override;
  std::optional<std::vector<u8>> Read(const Key &key) const override;
  std::vector<Key> Keys() const override;
  MountKind Kind() const override { return MountKind::Archive; }
  size_t KeyCount() const override { return entries_.size(); }

private:
  struct Record {
    u32 offset = 0;
    u32 zsize = 0;
    u32 size = 0;
    bool compressed = false;
  };

  const Record *Find(const Key &key) const;

  std::filesystem::path path_;
  Key prefix_;
  std::unordered_map<Key, Record> entries_;

  // Mapped for the mount's lifetime rather than reopened per read: Read is
  // const and runs on the IO threads, and a mapping serves them all with no
  // handle and no lock. Null when the file would not map, which reads instead.
  // It does hold the archive open, so a mount over a file about to be deleted
  // has to be dropped first.
  std::unique_ptr<rex::memory::MappedMemory> mapped_;
};

// Every .ipk the game ships, addressed by guest path.
//
// The engine only reads an archive it has registered, and it registers them
// along the route the player took, so entering somewhere unusual leaves whole
// subtrees unreadable, and a required file that does not resolve is a fatal
// load error. Hence Tier::Fallback.
//
// An archive names its records relative to its mount point, which neither the
// record nor the archive path states: chara\ipk\pc02.ipk holds ply\pc02\* for
// guest chara\ply\pc02\*, map\ipk\bg01_00.ipk holds map\town\bg01\* verbatim,
// and game_start.ipk puts the disc root under bd_root\. So the index is keyed
// by record name and a request matches the longest tail of itself that names
// one.
class ShippedPackMount final : public Mount {
public:
  // Every .ipk the game ships lives under <game_root>/pack, so the mount owns
  // that name: it is both where Scan looks and the guest directory enumeration
  // reports the archives under.
  //
  // The built index is written to 'index_cache' and reloaded from it while the
  // pack tree still matches. Reading the tables back out of the archives is
  // some 1700 file opens spread over gigabytes: tens of seconds whenever the OS
  // cache does not already hold them. An empty path rebuilds every time.
  static std::shared_ptr<ShippedPackMount>
  Scan(const std::filesystem::path &game_root,
       const std::filesystem::path &index_cache);

  std::optional<u64> Stat(const Key &key) const override;
  std::optional<std::vector<u8>> Read(const Key &key) const override;
  std::vector<Key> Keys() const override;
  MountKind Kind() const override { return MountKind::ShippedPack; }
  size_t KeyCount() const override { return entries_.size(); }

private:
  // A name is a span of blob_ rather than a string of its own, and it carries
  // the record outright rather than pointing at an archive to go ask, so the
  // index is two flat buffers that load as they were written: nothing to fix
  // up, no allocation per record, and a Stat that never touches a file.
  struct Entry {
    u32 name_off = 0;
    u32 name_len = 0;
    u32 archive = 0;
    u32 offset = 0;
    u32 zsize = 0;
    u32 size = 0;
    u32 flags = 0; // kEntryCompressed, kEntryAlias
  };
  static_assert(sizeof(Entry) == 0x1C);

  // Fills the archive list and returns the stamp for the pack tree. The
  // stamp decides whether a cached index still describes it.
  u64 ScanArchives(const std::filesystem::path &pack_root,
                   const std::filesystem::path &game_root);
  void BuildIndex();
  bool LoadIndex(const std::filesystem::path &path, u64 stamp);
  void SaveIndex(const std::filesystem::path &path, u64 stamp) const;

  std::string_view Name(const Entry &entry) const {
    return {blob_.data() + entry.name_off, entry.name_len};
  }

  const Entry *Find(std::string_view name) const;
  const Entry *Resolve(const Key &key) const;

  std::vector<std::filesystem::path> archives_;
  std::vector<Key> archive_keys_; // guest path of each archive
  std::vector<Entry> entries_;    // by name, ascending
  std::vector<char> blob_;
};

} // namespace bd::vfs
