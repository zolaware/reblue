#include "installer.h"

#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/filesystem/entry.h>
#include <rex/memory/mapped_memory.h>

#include <fstream>
#include <string_view>
#include <vector>

#include "bdengine/common/logging.h"
#include "installer_manifest.h"

namespace reblue {

namespace fs = std::filesystem;

std::unique_ptr<rex::filesystem::DiscImageDevice> OpenDiscImage(const fs::path& iso_path) {
  auto disc = std::make_unique<rex::filesystem::DiscImageDevice>("", iso_path);
  if (!disc->Initialize()) {
    return nullptr;
  }
  return disc;
}

bool ValidateDisc(rex::filesystem::DiscImageDevice& disc, int disc_number) {
  const std::string marker = "bd_disc_" + std::to_string(disc_number) + ".xml";
  return disc.ResolvePath(marker) != nullptr;
}

std::string DiscFingerprint(const fs::path& iso_path,
                            rex::filesystem::DiscImageDevice& disc,
                            int disc_number) {
  std::error_code ec;
  const auto file_size = fs::file_size(iso_path, ec);
  const std::string marker = "bd_disc_" + std::to_string(disc_number) + ".xml";
  size_t marker_size = 0;
  if (auto* entry = disc.ResolvePath(marker); entry != nullptr) {
    marker_size = entry->size();
  }
  return std::to_string(ec ? 0 : file_size) + ":" + std::to_string(marker_size);
}

namespace {

// Parses the embedded manifest blob (one relative path per line, LF or CRLF)
// into a sorted, deduplicated list of forward-slash paths.
std::vector<std::string> LoadManifest() {
  std::string_view blob(reinterpret_cast<const char*>(g_installer_manifest_data),
                        g_installer_manifest_size);
  std::vector<std::string> out;
  out.reserve(8192);
  size_t start = 0;
  for (size_t i = 0; i <= blob.size(); ++i) {
    const bool eol = (i == blob.size() || blob[i] == '\n');
    if (!eol) continue;
    size_t end = i;
    if (end > start && blob[end - 1] == '\r') --end;
    if (end > start) {
      out.emplace_back(blob.substr(start, end - start));
    }
    start = i + 1;
  }
  return out;
}

// True if any path component equals "..", or if the path is absolute / drive-
// qualified. Defence against a manifest that somehow contains an escape.
bool IsUnsafePath(const std::string& path) {
  if (path.empty()) return true;
  if (path.find('\0') != std::string::npos) return true;
  if (path.front() == '/' || path.front() == '\\') return true;
  if (path.size() >= 2 && path[1] == ':') return true;
  fs::path p(path);
  for (const auto& part : p) {
    if (part.string() == "..") return true;
  }
  return false;
}

struct DiscHandle {
  std::unique_ptr<rex::filesystem::DiscImageDevice> device;
  const char* label;
};

bool ExtractOne(rex::filesystem::Entry* entry,
                const fs::path& dest_path,
                InstallProgress& progress) {
  auto parent = dest_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    fs::create_directories(parent, ec);
  }

  const size_t size = entry->size();
  auto mapped = static_cast<rex::filesystem::DiscImageEntry*>(entry)->OpenMapped(
      rex::memory::MappedMemory::Mode::kRead, 0, 0);
  if (!mapped) {
    BD_ERROR("OpenMapped failed for '{}'", entry->path());
    progress.SetError("Failed to read: " + entry->path());
    progress.failed.store(true);
    return false;
  }

  std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    BD_ERROR("Failed to create '{}'", dest_path.string());
    progress.SetError("Failed to create: " + dest_path.string());
    progress.failed.store(true);
    return false;
  }
  out.write(reinterpret_cast<const char*>(mapped->data()),
            static_cast<std::streamsize>(size));
  if (!out) {
    BD_ERROR("Failed to write '{}'", dest_path.string());
    progress.SetError("Failed to write: " + dest_path.string());
    progress.failed.store(true);
    return false;
  }
  return true;
}

}  // namespace

std::thread Installer::RunAsync(const fs::path& iso1_path,
                                const fs::path& iso2_path,
                                const fs::path& iso3_path,
                                const fs::path& game_data_dest,
                                InstallProgress& progress) {
  return std::thread([iso1_path, iso2_path, iso3_path, game_data_dest, &progress]() {
    std::vector<DiscHandle> discs;
    discs.push_back({OpenDiscImage(iso1_path), "DVD1"});
    discs.push_back({OpenDiscImage(iso2_path), "DVD2"});
    discs.push_back({OpenDiscImage(iso3_path), "DVD3"});
    for (const auto& d : discs) {
      if (!d.device) {
        progress.SetError("Failed to open one or more disc images.");
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
    }

    const auto manifest = LoadManifest();
    if (manifest.empty()) {
      progress.SetError("Install manifest is empty.");
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    // Pre-resolve everything so we know the total byte count up front and
    // get a predictable progress bar.
    struct PlanItem {
      std::string path;
      rex::filesystem::Entry* entry;  // null = missing on every disc
      size_t size;
    };
    std::vector<PlanItem> plan;
    plan.reserve(manifest.size());
    size_t total_bytes = 0;
    size_t missing = 0;
    size_t hits_per_disc[3] = {0, 0, 0};
    for (const auto& path : manifest) {
      if (IsUnsafePath(path)) {
        BD_ERROR("Manifest contains unsafe path: {}", path);
        progress.SetError("Bad manifest entry: " + path);
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      // Track which disc served each entry so we can log a summary and, in
      // the future, refine manifest ordering for cache locality.
      rex::filesystem::Entry* entry = nullptr;
      for (size_t i = 0; i < discs.size(); ++i) {
        if (auto* e = discs[i].device->ResolvePath(path); e != nullptr) {
          entry = e;
          ++hits_per_disc[i];
          break;
        }
      }
      const size_t size = entry ? entry->size() : 0;
      if (!entry) {
        BD_WARN("MISSING: {} (not on any disc)", path);
        ++missing;
      }
      total_bytes += size;
      plan.push_back({path, entry, size});
    }

    progress.files_total.store(plan.size());
    progress.bytes_total.store(total_bytes);

    BD_INFO("Install plan: {} files, {} bytes total", plan.size(), total_bytes);
    BD_INFO("  DVD1: {} files, DVD2: {} files, DVD3: {} files, missing: {}",
            hits_per_disc[0], hits_per_disc[1], hits_per_disc[2], missing);

    std::error_code ec;
    fs::create_directories(game_data_dest, ec);

    for (const auto& item : plan) {
      if (progress.cancelled.load()) break;
      if (progress.failed.load()) break;

      progress.SetCurrentFile(item.path);

      if (item.entry == nullptr) {
        // Logged above as MISSING. Skip without failing - manifest refinement
        // is tracked separately.
        progress.files_done.fetch_add(1);
        continue;
      }

      const auto dest_path = game_data_dest / fs::path(item.path);
      if (!ExtractOne(item.entry, dest_path, progress)) {
        progress.complete.store(true);
        return;
      }

      progress.files_done.fetch_add(1);
      progress.bytes_done.fetch_add(item.size);
    }

    if (progress.failed.load() || progress.cancelled.load()) {
      progress.complete.store(true);
      return;
    }

    const auto marker_path = game_data_dest / "reblue_install.marker";
    std::ofstream marker(marker_path, std::ios::trunc);
    marker << "installed";
    if (!marker) {
      BD_ERROR("Failed to write install marker at '{}'", marker_path.string());
      progress.SetError("Failed to write install marker - check disk space / permissions.");
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    BD_INFO("Installation complete ({} files missing - see log).", missing);
    progress.complete.store(true);
  });
}

}  // namespace reblue
