#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace rex::filesystem {
class DiscImageDevice;
}

namespace reblue {

struct InstallProgress {
  std::atomic<size_t> files_done{0};
  std::atomic<size_t> files_total{0};
  std::atomic<size_t> bytes_done{0};
  std::atomic<size_t> bytes_total{0};
  std::atomic<bool> complete{false};
  std::atomic<bool> failed{false};
  std::atomic<bool> cancelled{false};

  std::mutex file_mutex;
  std::string current_file;

  std::mutex error_mutex;
  std::string error_message;

  std::string GetCurrentFile() {
    std::lock_guard lock(file_mutex);
    return current_file;
  }

  void SetCurrentFile(const std::string& f) {
    std::lock_guard lock(file_mutex);
    current_file = f;
  }

  std::string GetError() {
    std::lock_guard lock(error_mutex);
    return error_message;
  }

  void SetError(const std::string& e) {
    std::lock_guard lock(error_mutex);
    error_message = e;
  }
};

// Opens an Xbox 360 GDFX disc image using the SDK. Returns nullptr on failure.
std::unique_ptr<rex::filesystem::DiscImageDevice> OpenDiscImage(
    const std::filesystem::path& iso_path);

// Confirms the image is the expected disc in a 3-disc set by checking for
// `bd_disc_<N>.xml` at the disc root.
bool ValidateDisc(rex::filesystem::DiscImageDevice& disc, int disc_number);

// Cheap identity string for a disc image (no full hash): file size + bd_disc_N
// marker size. Stable across re-opens of the same file.
std::string DiscFingerprint(const std::filesystem::path& iso_path,
                            rex::filesystem::DiscImageDevice& disc,
                            int disc_number);

class Installer {
 public:
  // Extracts the game data directly into `game_data_dest` (no subdir). Writes
  // a reblue_install.marker file in the same directory on success.
  static std::thread RunAsync(const std::filesystem::path& iso1_path,
                              const std::filesystem::path& iso2_path,
                              const std::filesystem::path& iso3_path,
                              const std::filesystem::path& game_data_dest,
                              InstallProgress& progress);
};

}  // namespace reblue
