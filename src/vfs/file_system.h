/**
 * @file    vfs/file_system.h
 * @brief   Ordered mount overlay over the engine's own file IO.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vfs/key.h"

namespace bd::vfs {

// Rows appended to shipped database tables, keyed by the table's path.
using DbRows = std::unordered_map<Key, std::vector<std::string>>;

enum class MountKind : u8 { Generated, Loose, Archive, ShippedPack };

const char *ToString(MountKind kind);

// Where a mount sits relative to the engine's own disc and .ipk IO: Overlay
// mounts answer before it is asked, Fallback mounts only once it has failed.
enum class Tier : u8 { Overlay, Fallback };

// A mount is immutable once registered: populate it, then hand it to
// FileSystem::Add, which swaps it in under the registry lock. That is why no
// mount carries a lock of its own.
class Mount {
public:
  virtual ~Mount() = default;

  // The guest stats far more paths than it reads, so this answers from the
  // mount's index rather than by decompressing or reading bytes where it can.
  virtual std::optional<u64> Stat(const Key &key) const = 0;

  // Nullopt when the mount does not hold 'key'. Membership and content come
  // back together so a mount that must materialize bytes to answer at all does
  // it once per read, not twice.
  virtual std::optional<std::vector<u8>> Read(const Key &key) const = 0;

  // For enumeration only. Serving a path never goes through it, so a mount is
  // free to make this its expensive call.
  virtual std::vector<Key> Keys() const = 0;

  virtual MountKind Kind() const = 0;
  virtual size_t KeyCount() const = 0;
};

struct StatResult {
  std::string mount;
  MountKind kind;
  u64 size;
};

struct ReadResult {
  std::string mount;
  MountKind kind;
  std::vector<u8> bytes;
};

// One name directly below a directory. Mounts hold flat keys, so a name is a
// directory here exactly when some key continues past it.
struct DirEntry {
  std::string name;
  bool is_dir = false;
};

// Highest priority wins, and ties break toward the most recently added, as mod
// load order needs. A miss in the Overlay tier means the file is not ours and
// the caller runs the engine's own IO, which reaches the disc and the
// registered .ipk packs. A miss in the Fallback tier means it exists nowhere.
//
// A patch holds a place in the same ordering without serving anything on its
// own: its rows extend whatever the entries below it serve for that key, or
// the disc file and then the Fallback tier when none of them does. A patch
// under a mount that replaces the whole file is shadowed by it.
class FileSystem {
public:
  // Replaces any mount already registered under 'name'. A priority below
  // kPriorityEngine puts the mount in the Fallback tier.
  void Add(std::string name, int priority, std::shared_ptr<Mount> mount);
  void AddPatch(std::string name, int priority, DbRows rows);
  void Remove(std::string_view name);

  // Where the engine's own IO reads loose files from, so a patched table no
  // mount serves still has a base to extend.
  void SetDiscRoot(std::filesystem::path root);

  std::optional<StatResult> Stat(const Key &key,
                                 Tier tier = Tier::Overlay) const;
  std::optional<ReadResult> Read(const Key &key,
                                 Tier tier = Tier::Overlay) const;

  // Names one level below 'dir' (empty for the root), pooled across every
  // mount of both tiers and sorted. A name held by two mounts appears once.
  std::vector<DirEntry> List(const Key &dir) const;

private:
  struct Entry {
    std::string name;
    int priority = 0;
    u64 seq = 0;
    std::shared_ptr<Mount> mount;
    std::shared_ptr<const DbRows> patch;
  };

  struct Composed {
    u64 revision = 0;
    ReadResult result;
  };

  void Insert(Entry entry);

  // Mounts are walked outside the lock so a slow inflate does not serialize
  // every other IO thread. The shared_ptr copies keep a mount alive through a
  // concurrent Remove.
  std::vector<Entry> Snapshot(Tier tier) const;

  std::optional<ReadResult> ReadUnpatched(const Key &key, Tier tier) const;
  std::optional<ReadResult> ReadDisc(const Key &key) const;

  // Answering List from Keys() would walk every key of every mount per
  // directory, so the whole tree is indexed once and rebuilt only when the
  // mount set changes.
  void RebuildIndex() const;

  mutable std::mutex mutex_;
  std::vector<Entry> mounts_; // priority desc, then seq desc
  u64 next_seq_ = 0;
  std::filesystem::path disc_root_;

  // revision_ starts ahead of indexed_revision_ so the first List builds.
  std::atomic<u64> revision_{1};
  mutable std::mutex index_mutex_;
  mutable std::unordered_map<Key, std::map<std::string, bool>> index_;
  mutable u64 indexed_revision_ = 0;

  // A patched table is composed once per mount set, since the engine stats it
  // before every read and each stat has to measure the result.
  mutable std::mutex composed_mutex_;
  mutable std::unordered_map<Key, Composed> composed_;
};

// Mods outrank DLC so a mod can override a file a DLC pack adds. The engine's
// own IO is not a mount but holds a place in the same ordering: above it a
// mount overrides what the game ships, below it a mount only fills in what the
// engine cannot reach.
inline constexpr int kPriorityGenerated = 300;
inline constexpr int kPriorityMod = 200;
inline constexpr int kPriorityContent = 150;
inline constexpr int kPriorityDLC = 100;
inline constexpr int kPriorityEngine = 50;
inline constexpr int kPriorityShippedPack = 25;

} // namespace bd::vfs
