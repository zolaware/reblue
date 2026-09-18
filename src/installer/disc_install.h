/**
 * @file    installer/disc_install.h
 * @brief   GDFX disc image extraction into the local game data directory.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "installer/install_registry.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace rex::filesystem {
class DiscImageDevice;
}

namespace bd::installer {

struct InstallProgress {
  std::atomic<size_t> files_done{0};
  std::atomic<size_t> files_total{0};
  std::atomic<size_t> bytes_done{0};
  std::atomic<size_t> bytes_total{0};
  std::atomic<bool> complete{false};
  std::atomic<bool> failed{false};
  std::atomic<bool> canceled{false};

  std::mutex file_mutex;
  std::string current_file;

  std::mutex error_mutex;
  std::string error_message;

  std::string GetCurrentFile() {
    std::lock_guard lock(file_mutex);
    return current_file;
  }

  void SetCurrentFile(const std::string &f) {
    std::lock_guard lock(file_mutex);
    current_file = f;
  }

  std::string GetError() {
    std::lock_guard lock(error_mutex);
    return error_message;
  }

  void SetError(const std::string &e) {
    std::lock_guard lock(error_mutex);
    error_message = e;
  }
};

std::unique_ptr<rex::filesystem::DiscImageDevice>
OpenDiscImage(const std::filesystem::path &iso_path);

// Confirms the disc in a 3-disc set via 'bd_disc_<N>.xml' at the disc root.
bool ValidateDisc(rex::filesystem::DiscImageDevice &disc, int disc_number);

// Cheap identity (no full hash): file size + bd_disc_N marker size.
std::string DiscFingerprint(const std::filesystem::path &iso_path,
                            rex::filesystem::DiscImageDevice &disc,
                            int disc_number);

struct DiscLanguages {
  std::set<std::string> ui; // [Language] codes, drives the wizard "lights"
  std::set<std::string>
      all; // union of [Language]+[Voice]+[BGM], always has "us"
};

// Reads bd_boot.ini from a disc and returns its declared language codes,
// lowercased. Absent/empty bd_boot.ini yields ui={}, all={"us"}.
DiscLanguages ParseDiscLanguages(rex::filesystem::DiscImageDevice &disc);

class Installer {
public:
  // Extracts into game_data_dest (no subdir) and writes reblue_install.marker
  // on success. repair=true verifies files already on disk (size match) and
  // copies only the missing ones, preserving an existing install. false does a
  // full extract.
  static std::thread
  RunAsync(const std::array<std::filesystem::path, kDiscCount> &iso_paths,
           const std::filesystem::path &game_data_dest, bool repair,
           InstallProgress &progress,
           const std::array<std::filesystem::path, kDiscCount>
               &korean_iso_paths = {});
};

} // namespace bd::installer
