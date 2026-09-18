/**
 * @file    installer/disc_install.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/disc_install.h"

#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/filesystem/entry.h>
#include <rex/memory/mapped_memory.h>

#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "core/app_root.h"
#include "core/logging.h"
#include "embedded.h"
#include "installer/korean_import.h"
#include "vfs/vfs.h"

namespace bd::installer {

namespace fs = std::filesystem;

std::unique_ptr<rex::filesystem::DiscImageDevice>
OpenDiscImage(const fs::path &iso_path) {
  auto disc = std::make_unique<rex::filesystem::DiscImageDevice>("", iso_path);
  if (!disc->Initialize()) {
    return nullptr;
  }
  return disc;
}

bool ValidateDisc(rex::filesystem::DiscImageDevice &disc, int disc_number) {
  const std::string marker = "bd_disc_" + std::to_string(disc_number) + ".xml";
  return disc.ResolvePath(marker) != nullptr;
}

std::string DiscFingerprint(const fs::path &iso_path,
                            rex::filesystem::DiscImageDevice &disc,
                            int disc_number) {
  std::error_code ec;
  const auto file_size = fs::file_size(iso_path, ec);
  const std::string marker = "bd_disc_" + std::to_string(disc_number) + ".xml";
  size_t marker_size = 0;
  if (auto *entry = disc.ResolvePath(marker); entry != nullptr) {
    marker_size = entry->size();
  }
  return std::to_string(ec ? 0 : file_size) + ":" + std::to_string(marker_size);
}

namespace {
constexpr std::array<std::string_view, 10> kKnownLangCodes = {
    "us", "jp", "de", "fr", "es", "it", "kr", "tw", "cn", "po"};

bool IsKnownLang(std::string_view code) {
  for (auto known : kKnownLangCodes) {
    if (known == code)
      return true;
  }
  return false;
}

// Splits the codes after a bd_boot.ini section tag (e.g. "US DE ES") into dst.
void AddLangCodes(std::string_view rest, std::set<std::string> &dst) {
  size_t p = 0;
  while (p < rest.size()) {
    while (p < rest.size() && std::isspace(static_cast<unsigned char>(rest[p])))
      ++p;
    size_t q = p;
    while (q < rest.size() &&
           !std::isspace(static_cast<unsigned char>(rest[q])))
      ++q;
    if (q > p) {
      std::string token(rest.substr(p, q - p));
      for (auto &ch : token)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (IsKnownLang(token))
        dst.insert(token);
    }
    p = q;
  }
}
} // namespace

DiscLanguages ParseDiscLanguages(rex::filesystem::DiscImageDevice &disc) {
  DiscLanguages out;
  out.all.insert("us");

  auto *entry = disc.ResolvePath("bd_boot.ini");
  if (!entry)
    return out;
  auto mapped =
      static_cast<rex::filesystem::DiscImageEntry *>(entry)->OpenMapped(
          rex::memory::MappedMemory::Mode::kRead, 0, 0);
  if (!mapped)
    return out;

  std::string_view text(reinterpret_cast<const char *>(mapped->data()),
                        entry->size());
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n')
      continue;
    std::string_view line = text.substr(start, i - start);
    start = i + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    const bool is_lang = line.find("[Language]") != std::string_view::npos;
    const bool is_voice = line.find("[Voice]") != std::string_view::npos;
    const bool is_bgm = line.find("[BGM]") != std::string_view::npos;
    if (!is_lang && !is_voice && !is_bgm)
      continue;

    const size_t rb = line.find(']');
    if (rb == std::string_view::npos)
      continue;
    std::string_view rest = line.substr(rb + 1);

    if (is_lang)
      AddLangCodes(rest, out.ui);
    AddLangCodes(rest,
                 out.all); // [Language]/[Voice]/[BGM] all contribute to .all
  }
  return out;
}

namespace {

std::vector<std::string> LoadManifest() {
  const std::string_view blob = bd::Embedded("installer/manifest.txt").text();
  std::vector<std::string> out;
  out.reserve(8192);
  size_t start = 0;
  for (size_t i = 0; i <= blob.size(); ++i) {
    const bool eol = (i == blob.size() || blob[i] == '\n');
    if (!eol)
      continue;
    size_t end = i;
    if (end > start && blob[end - 1] == '\r')
      --end;
    if (end > start) {
      out.emplace_back(blob.substr(start, end - start));
    }
    start = i + 1;
  }
  return out;
}

// Rejects "..", absolute, or drive-qualified paths (manifest escape guard).
bool IsUnsafePath(const std::string &path) {
  if (path.empty())
    return true;
  if (path.find('\0') != std::string::npos)
    return true;
  if (path.front() == '/' || path.front() == '\\')
    return true;
  if (path.size() >= 2 && path[1] == ':')
    return true;
  fs::path p(path);
  for (const auto &part : p) {
    if (part.string() == "..")
      return true;
  }
  return false;
}

// Recursively appends (disc-relative path, entry) for every file under 'dir'.
// 'prefix' is the disc-relative path of 'dir' itself (e.g. "snd_memory_fr").
void CollectDiscFiles(
    rex::filesystem::Entry *dir, const std::string &prefix,
    std::vector<std::pair<std::string, rex::filesystem::Entry *>> &out) {
  for (const auto &child : dir->children()) {
    std::string child_path = prefix + "/" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory) {
      CollectDiscFiles(child.get(), child_path, out);
    } else {
      out.emplace_back(std::move(child_path), child.get());
    }
  }
}

bool ExtractOne(rex::filesystem::Entry *entry, const fs::path &dest_path,
                InstallProgress &progress) {
  auto parent = dest_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    fs::create_directories(parent, ec);
  }

  const size_t size = entry->size();
  auto mapped =
      static_cast<rex::filesystem::DiscImageEntry *>(entry)->OpenMapped(
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
  out.write(reinterpret_cast<const char *>(mapped->data()),
            static_cast<std::streamsize>(size));
  if (!out) {
    BD_ERROR("Failed to write '{}'", dest_path.string());
    progress.SetError("Failed to write: " + dest_path.string());
    progress.failed.store(true);
    return false;
  }
  return true;
}

} // namespace

std::thread
Installer::RunAsync(const std::array<fs::path, kDiscCount> &iso_paths,
                    const fs::path &game_data_dest, bool repair,
                    InstallProgress &progress,
                    const std::array<fs::path, kDiscCount> &korean_iso_paths) {
  return std::thread(
      [iso_paths, game_data_dest, repair, korean_iso_paths, &progress]() {
    std::vector<std::unique_ptr<rex::filesystem::DiscImageDevice>> discs;
    for (const auto &iso_path : iso_paths)
      discs.push_back(OpenDiscImage(iso_path));
    for (const auto &d : discs) {
      if (!d) {
        progress.SetError("Failed to open one or more disc images.");
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
    }

    // Languages come from bd_boot.ini (the game's Language Set), not from
    // scanning the discs' folder layout. The per-language files are walked from
    // the discs below.
    std::set<std::string> available;
    for (const auto &d : discs) {
      DiscLanguages langs = ParseDiscLanguages(*d);
      available.insert(langs.all.begin(), langs.all.end());
    }
    {
      std::string joined;
      for (const auto &l : available)
        joined += l + " ";
      BD_INFO("Installer: languages available on discs: {}", joined);
    }

    const auto manifest = LoadManifest();
    if (manifest.empty()) {
      progress.SetError("Install manifest is empty.");
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    // Pre-resolve everything up front for an accurate total byte count.
    struct PlanItem {
      std::string path;
      rex::filesystem::Entry *entry; // null = missing on every disc
      size_t size;
      bool needs_copy; // false = missing entry, or already on disk (repair)
    };
    std::vector<PlanItem> plan;
    plan.reserve(manifest.size());
    size_t total_bytes = 0;
    size_t missing = 0;
    size_t hits_per_disc[kDiscCount] = {};

    // Repair skips any file already on disk whose size matches the disc entry:
    // this verifies the existing install and limits the copy to missing files.
    auto will_copy = [&](const std::string &path,
                         rex::filesystem::Entry *entry) -> bool {
      if (!entry)
        return false;
      if (!repair)
        return true;
      std::error_code ec;
      const auto dest = game_data_dest / fs::path(path);
      return !(fs::exists(dest, ec) &&
               fs::file_size(dest, ec) == entry->size());
    };
    for (const auto &path : manifest) {
      if (IsUnsafePath(path)) {
        BD_ERROR("Manifest contains unsafe path: {}", path);
        progress.SetError("Bad manifest entry: " + path);
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      rex::filesystem::Entry *entry = nullptr;
      for (size_t i = 0; i < discs.size(); ++i) {
        if (auto *e = discs[i]->ResolvePath(path); e != nullptr) {
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
      const bool needs_copy = will_copy(path, entry);
      if (needs_copy)
        total_bytes += size;
      plan.push_back({path, entry, size, needs_copy});
    }

    // Language assets: the set of languages comes from bd_boot.ini
    // ('available') and the files come from walking each language's directories
    // on the discs. The manifest lists no language family, so base and language
    // paths are disjoint and only cross-disc duplicates need de-duping.
    std::set<std::string> seen_lang;
    for (const auto &lang : available) {
      const std::string packmem = "pack/packmem_" + lang + ".ipk";
      const std::string lang_dirs[] = {"snd_memory_" + lang,
                                       "snd_stream_" + lang};
      for (size_t di = 0; di < discs.size(); ++di) {
        auto *dev = discs[di].get();

        if (auto *e = dev->ResolvePath(packmem);
            e != nullptr && seen_lang.insert(packmem).second) {
          const bool needs_copy = will_copy(packmem, e);
          if (needs_copy)
            total_bytes += e->size();
          plan.push_back({packmem, e, e->size(), needs_copy});
          ++hits_per_disc[di];
        }

        for (const auto &dir_name : lang_dirs) {
          auto *d = dev->ResolvePath(dir_name);
          if (d == nullptr ||
              !(d->attributes() & rex::filesystem::kFileAttributeDirectory)) {
            continue;
          }
          std::vector<std::pair<std::string, rex::filesystem::Entry *>> files;
          CollectDiscFiles(d, dir_name, files);
          for (auto &[rel, entry] : files) {
            if (!seen_lang.insert(rel).second)
              continue;
            const bool needs_copy = will_copy(rel, entry);
            if (needs_copy)
              total_bytes += entry->size();
            plan.push_back({rel, entry, entry->size(), needs_copy});
            ++hits_per_disc[di];
          }
        }
      }
    }

    progress.files_total.store(plan.size());
    progress.bytes_total.store(total_bytes);

    BD_INFO("Install plan ({}): {} files, {} bytes to copy",
            repair ? "repair" : "full", plan.size(), total_bytes);
    BD_INFO("  DVD1: {} files, DVD2: {} files, DVD3: {} files, missing: {}",
            hits_per_disc[0], hits_per_disc[1], hits_per_disc[2], missing);

    std::error_code ec;
    fs::create_directories(game_data_dest, ec);

    size_t skipped = 0;
    for (const auto &item : plan) {
      if (progress.canceled.load())
        break;
      if (progress.failed.load())
        break;

      if (!item.needs_copy) {
        if (item.entry != nullptr)
          ++skipped;
        progress.files_done.fetch_add(1);
        continue;
      }

      progress.SetCurrentFile(item.path);

      const auto dest_path = game_data_dest / fs::path(item.path);
      if (!ExtractOne(item.entry, dest_path, progress)) {
        progress.complete.store(true);
        return;
      }

      progress.files_done.fetch_add(1);
      progress.bytes_done.fetch_add(item.size);
    }

    if (progress.failed.load() || progress.canceled.load()) {
      progress.complete.store(true);
      return;
    }

    const bool any_korean =
        std::any_of(korean_iso_paths.begin(), korean_iso_paths.end(),
                    [](const fs::path &p) { return !p.empty(); });
    const bool all_korean =
        std::all_of(korean_iso_paths.begin(), korean_iso_paths.end(),
                    [](const fs::path &p) { return !p.empty(); });
    if (any_korean && !all_korean) {
      progress.SetError(
          "Korean retail import requires DVD 1, DVD 2 and DVD 3.");
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }
    if (all_korean &&
        !ImportKoreanRetailData(korean_iso_paths, game_data_dest, progress)) {
      progress.complete.store(true);
      return;
    }

    const auto marker_path = game_data_dest / "reblue_install.marker";
    std::ofstream marker(marker_path, std::ios::trunc);
    marker << "installed";
    if (!marker) {
      BD_ERROR("Failed to write install marker at '{}'", marker_path.string());
      progress.SetError(
          "Failed to write install marker - check disk space / permissions.");
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    progress.SetCurrentFile("pack index");
    vfs::VFS::BuildPackIndex(game_data_dest,
                             bd::CacheRootFor(game_data_dest.parent_path()));

    if (repair) {
      BD_INFO("Repair complete: {} files already present, {} missing on discs.",
              skipped, missing);
    } else {
      BD_INFO("Installation complete ({} files missing - see log).", missing);
    }
    progress.complete.store(true);
  });
}

} // namespace bd::installer
