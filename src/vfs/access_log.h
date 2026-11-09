/**
 * @file    vfs/access_log.h
 * @brief   File access telemetry: which path was served, and from where.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bd::vfs {

// Where a file request was answered from. Mod/Virtual/DLC are overlay mounts,
// IPK/Disk are the engine's own IO, and Pack is the shipped pack fallback that
// runs once the engine's IO has failed.
enum class FileSource : u8 { Mod, Virtual, DLC, IPK, Disk, Pack, Missing };

// Cumulative per-file stats, persisted across sessions in the summary CSV.
struct FileAccessEntry {
  std::string pack; // first path component
  std::string inner_path;
  u64 access_count = 0;
  u64 override_count = 0;
  u64 miss_count = 0;
};

struct DetailEntry {
  std::string datetime; // local time, YYYY-MM-DD HH:MM:SS
  std::string pack;
  std::string inner_path;
  FileSource source;
};

// Two CSVs: file_access_summary.csv carries cumulative totals across sessions
// and is rewritten whole on each flush, and mod_access_logs/detail_*.csv is one
// append-only file per session. Recording is off unless bd_mod_log is set, but
// the summary is loaded regardless so enabling it mid-session cannot overwrite
// earlier totals with this session's counts alone.
//
// Recording runs on the engine's IO path, the main thread included, so it only
// appends in memory. A writer thread flushes on an interval.
class AccessLog {
public:
  // disc_root decides Disk vs IPK for engine-served rows.
  void Init(const std::filesystem::path &disc_root);

  // Stops the writer and flushes what is left.
  void Shutdown();

  void Record(const std::string &key, FileSource source);

  // The engine answered, so the file is either loose on disc or inside one of
  // the shipped .ipk packs. Only the former is visible from here.
  void RecordEngineHit(const std::string &key);

  void Flush();

private:
  void LoadSummaryLocked();
  void PrepareDetailPathLocked();
  void PruneOldDetailLogsLocked();
  void WriterLoop(std::stop_token stop);

  std::filesystem::path disc_root_;
  std::filesystem::path log_dir_;

  std::mutex write_mutex_; // serializes flushes, taken before mutex_
  std::mutex mutex_;       // guards entries_ + pending_detail_
  std::unordered_map<std::string, FileAccessEntry> entries_;
  std::vector<DetailEntry> pending_detail_;
  bool summary_dirty_ = false;                // entries_ changed since a flush
  std::filesystem::path session_detail_path_; // this run's detail file
  bool session_detail_capped_ = false;        // size cap hit this session

  std::condition_variable_any wake_;
  std::jthread writer_;
};

} // namespace bd::vfs
