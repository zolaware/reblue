/**
 * @file    vfs/access_log.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/access_log.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include <rex/logging.h>

#include "core/logging.h"
#include "core/time_util.h"
#include "vfs/settings.h"

namespace bd::vfs {

namespace {

std::pair<std::string, std::string> SplitPackPath(const std::string &path) {
  auto sep = path.find('\\');
  if (sep == std::string::npos)
    return {path, ""};
  return {path.substr(0, sep), path.substr(sep + 1)};
}

const char *FileSourceStr(FileSource s) {
  switch (s) {
  case FileSource::Mod:
    return "mod";
  case FileSource::Virtual:
    return "virtual";
  case FileSource::DLC:
    return "dlc";
  case FileSource::IPK:
    return "ipk";
  case FileSource::Disk:
    return "disk";
  case FileSource::Pack:
    return "pack";
  case FileSource::Missing:
    return "missing";
  }
  return "unknown";
}

std::string NowLocalTimestamp() {
  auto tt =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local_tm = bd::LocalTime(tt);
  char buf[20];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
  return buf;
}

// Filename-safe local timestamp: YYYYMMDD_HHMMSS_mmm. Sorts chronologically,
// and the millisecond suffix avoids same-second collisions between launches.
std::string NowFileTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::tm local_tm = bd::LocalTime(tt);
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local_tm);
  return std::format("{}_{:03}", buf, ms.count());
}

} // namespace

// Under the lock: a thread that reached the IO hooks before the runtime was up
// can be recording while another thread runs this.
void AccessLog::Init(const std::filesystem::path &disc_root) {
  std::lock_guard lock(mutex_);

  disc_root_ = disc_root;

  log_dir_ = rex::LoggingConfig().log_dir;

  PruneOldDetailLogsLocked();
  LoadSummaryLocked();
}

// Restore cumulative counts from previous sessions.
void AccessLog::LoadSummaryLocked() {
  std::ifstream ifs(log_dir_ / "file_access_summary.csv");
  if (!ifs)
    return;

  std::string line;
  std::getline(ifs, line);

  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;

    // pack,inner_path,total_accesses,total_overrides[,total_misses]
    std::istringstream ss(line);
    std::string pack, inner, accesses_str, overrides_str, misses_str;
    if (!std::getline(ss, pack, ','))
      continue;
    if (!std::getline(ss, inner, ','))
      continue;
    if (!std::getline(ss, accesses_str, ','))
      continue;
    if (!std::getline(ss, overrides_str, ','))
      continue;
    std::getline(ss, misses_str, ','); // optional, absent in old summaries

    auto key = pack + "\\" + inner;
    auto &entry = entries_[key];
    entry.pack = std::move(pack);
    entry.inner_path = std::move(inner);
    entry.access_count = std::strtoull(accesses_str.c_str(), nullptr, 10);
    entry.override_count = std::strtoull(overrides_str.c_str(), nullptr, 10);
    entry.miss_count =
        misses_str.empty() ? 0 : std::strtoull(misses_str.c_str(), nullptr, 10);
  }

  if (!entries_.empty())
    BD_TRACE("[mods] restored {} entries from previous summary",
             entries_.size());
}

void AccessLog::Record(const std::string &key, FileSource source) {
  if (!Settings::Get().ModLog())
    return;

  bool should_flush = false;
  {
    std::lock_guard lock(mutex_);

    auto [pack, inner] = SplitPackPath(key);

    auto &entry = entries_[key];
    if (entry.access_count == 0) {
      entry.pack = pack;
      entry.inner_path = inner;
    }
    entry.access_count++;
    if (source == FileSource::Mod)
      entry.override_count++;
    if (source == FileSource::Missing)
      entry.miss_count++;

    pending_detail_.push_back(
        {NowLocalTimestamp(), std::move(pack), std::move(inner), source});

    should_flush = (pending_detail_.size() % 100 == 0);
  }

  if (should_flush)
    Flush();
}

void AccessLog::RecordEngineHit(const std::string &key) {
  if (!Settings::Get().ModLog())
    return;

  std::filesystem::path disc_root;
  {
    std::lock_guard lock(mutex_);
    disc_root = disc_root_;
  }

  std::error_code ec;
  Record(key, std::filesystem::exists(disc_root / key, ec) ? FileSource::Disk
                                                           : FileSource::IPK);
}

void AccessLog::PruneOldDetailLogsLocked() {
  i32 keep = Settings::Get().ModLogKeep();
  if (keep < 1)
    keep = 1;

  auto dir = log_dir_ / "mod_access_logs";
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec))
    return;

  std::vector<std::filesystem::path> files;
  for (auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file())
      continue;
    auto name = entry.path().filename().string();
    if (name.starts_with("detail_") && entry.path().extension() == ".csv")
      files.push_back(entry.path());
  }

  if (files.size() <= static_cast<size_t>(keep))
    return;

  std::sort(files.begin(), files.end()); // ascending name == chronological
  size_t to_delete = files.size() - static_cast<size_t>(keep);
  for (size_t i = 0; i < to_delete; ++i) {
    std::error_code dec;
    std::filesystem::remove(files[i], dec);
    if (dec)
      BD_WARN("[mods] failed to prune {}: {}", files[i].string(),
              dec.message());
  }
  BD_INFO("[mods] pruned {} old access-log file(s), keeping newest {}",
          to_delete, keep);
}

// Lazily pick this session's detail file on first write so disabled logging
// never creates an empty file and all of a run's rows go in one file.
void AccessLog::PrepareDetailPathLocked() {
  if (!session_detail_path_.empty() || session_detail_capped_)
    return;

  auto dir = log_dir_ / "mod_access_logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    BD_ERROR("[mods] failed to create {}: {}", dir.string(), ec.message());
    session_detail_capped_ =
        true; // stop retrying (and re-logging) this session
    return;
  }
  session_detail_path_ = dir / ("detail_" + NowFileTimestamp() + ".csv");
}

void AccessLog::Flush() {
  std::lock_guard lock(mutex_);

  if (entries_.empty() && pending_detail_.empty())
    return;

  std::error_code dir_ec;
  std::filesystem::create_directories(log_dir_, dir_ec);
  if (dir_ec) {
    BD_ERROR("[mods] failed to create log dir {}: {}", log_dir_.string(),
             dir_ec.message());
    session_detail_capped_ = true; // unwritable: don't accumulate in RAM
    pending_detail_.clear();
    return;
  }

  // Summary CSV: truncate + rewrite with cumulative totals.
  if (!entries_.empty()) {
    auto summary_path = log_dir_ / "file_access_summary.csv";
    std::ofstream ofs(summary_path, std::ios::trunc);
    if (!ofs) {
      BD_ERROR("[mods] failed to write summary to {}", summary_path.string());
    } else {
      ofs << "pack,inner_path,total_accesses,total_overrides,total_misses\n";

      std::vector<const FileAccessEntry *> sorted;
      sorted.reserve(entries_.size());
      for (const auto &[key, entry] : entries_)
        sorted.push_back(&entry);
      std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) {
        if (a->pack != b->pack)
          return a->pack < b->pack;
        return a->inner_path < b->inner_path;
      });

      for (const auto *e : sorted) {
        ofs << e->pack << "," << e->inner_path << "," << e->access_count << ","
            << e->override_count << "," << e->miss_count << "\n";
      }

      BD_TRACE("[mods] wrote summary ({} entries) to {}", sorted.size(),
               summary_path.string());
    }
  }

  // Detail CSV: append-only with timestamps, one file per session.
  if (pending_detail_.empty())
    return;

  PrepareDetailPathLocked();
  if (session_detail_capped_ || session_detail_path_.empty()) {
    pending_detail_.clear(); // capped or unwritable: don't accumulate in RAM
    return;
  }

  const auto &detail_path = session_detail_path_;

  // Per-session size guard turns the count-based retention ceiling hard: worst
  // case on disk is (bd_mod_log_keep + 1) * bd_mod_log_max_mb (the retained
  // sessions plus this active one).
  i32 max_mb = Settings::Get().ModLogMaxMB();
  if (max_mb > 0) {
    std::error_code sec;
    u64 cur = std::filesystem::exists(detail_path, sec)
                  ? std::filesystem::file_size(detail_path, sec)
                  : 0;
    if (!sec && cur >= static_cast<u64>(max_mb) * 1024ull * 1024ull) {
      BD_WARN("[mods] detail log hit {} MB cap, stopping detail capture for "
              "this session ({})",
              max_mb, detail_path.string());
      session_detail_capped_ = true;
      pending_detail_.clear();
      return;
    }
  }

  std::error_code hec;
  bool needs_header = !std::filesystem::exists(detail_path, hec) ||
                      std::filesystem::file_size(detail_path, hec) == 0;

  std::ofstream ofs(detail_path, std::ios::app);
  if (!ofs) {
    BD_ERROR("[mods] failed to write detail log to {}", detail_path.string());
    pending_detail_.clear(); // drop rather than retain unboundedly
    return;
  }

  if (needs_header)
    ofs << "datetime,pack,inner_path,source\n";
  for (const auto &d : pending_detail_) {
    ofs << d.datetime << "," << d.pack << "," << d.inner_path << ","
        << FileSourceStr(d.source) << "\n";
  }

  BD_TRACE("[mods] appended {} detail entries to {}", pending_detail_.size(),
           detail_path.string());
  pending_detail_.clear();
}

} // namespace bd::vfs
