/**
 * @file    installer/disc_install.h
 * @brief   Disc image and Games on Demand extraction into the game data dir.
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
#include <vector>

namespace rex::filesystem {
class Device;
class Entry;
} // namespace rex::filesystem

namespace bd::installer {

inline constexpr const char *kDiscLabels[kDiscCount] = {"DVD 1", "DVD 2",
                                                        "DVD 3"};

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

bool ValidateDisc(rex::filesystem::Entry &root, int disc_number);

std::string DiscFingerprint(size_t content_size, rex::filesystem::Entry &root,
                            int disc_number);

class DiscImage {
public:
  static std::unique_ptr<DiscImage> Open(const std::filesystem::path &file);

  DiscImage(std::unique_ptr<rex::filesystem::Device> device,
            size_t content_size);
  ~DiscImage();

  rex::filesystem::Entry *Root(int disc_number) const {
    return roots_[disc_number - 1];
  }
  size_t ContentSize() const { return content_size_; }

private:
  std::unique_ptr<rex::filesystem::Device> device_;
  std::array<rex::filesystem::Entry *, kDiscCount> roots_{};
  size_t content_size_ = 0;
};

struct LanguageChoice {
  std::string code;
  bool text = false;
  bool voice = false;
};

struct InstallSelection {
  std::vector<LanguageChoice> languages;
  bool movies = true;
};

class Installer {
public:
  static std::thread
  RunAsync(const std::array<std::filesystem::path, kDiscCount> &sources,
           const std::filesystem::path &game_data_dest, bool repair,
           const InstallSelection &selection, InstallProgress &progress);

  static std::thread AddLanguagesAsync(
      const std::array<std::filesystem::path, kDiscCount> &sources,
      const std::filesystem::path &game_data_dest,
      const InstallSelection &selection, InstallProgress &progress);

  static std::thread
  RemoveLanguageAsync(const std::filesystem::path &game_data_dest,
                      const std::string &lang, InstallProgress &progress);
};

} // namespace bd::installer
