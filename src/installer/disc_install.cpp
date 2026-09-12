/**
 * @file    installer/disc_install.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/disc_install.h"

#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/system/xcontent.h>

#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "core/app_root.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/xcontent.h"
#include "embedded.h"
#include "vfs/vfs.h"

namespace bd::installer {

namespace fs = std::filesystem;

namespace {

std::string DiscMarker(int disc_number) {
  return "bd_disc_" + std::to_string(disc_number) + ".xml";
}

constexpr std::array<std::string_view, 10> kKnownLangCodes = {
    "us", "jp", "de", "fr", "es", "it", "kr", "tw", "cn", "po"};

bool IsKnownLang(std::string_view code) {
  for (auto known : kKnownLangCodes) {
    if (known == code)
      return true;
  }
  return false;
}

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

bool ValidateDisc(rex::filesystem::Entry &root, int disc_number) {
  return root.ResolvePath(DiscMarker(disc_number)) != nullptr;
}

std::string DiscFingerprint(size_t content_size, rex::filesystem::Entry &root,
                            int disc_number) {
  size_t marker_size = 0;
  if (auto *entry = root.ResolvePath(DiscMarker(disc_number)); entry != nullptr)
    marker_size = entry->size();
  return std::to_string(content_size) + ":" + std::to_string(marker_size);
}

std::set<std::string> ParseDiscLanguages(rex::filesystem::Entry &root) {
  std::set<std::string> out;

  auto *entry = root.ResolvePath("bd_boot.ini");
  if (!entry)
    return out;
  const std::string text = ReadEntry(*entry);

  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i != text.size() && text[i] != '\n')
      continue;
    std::string_view line = std::string_view(text).substr(start, i - start);
    start = i + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    if (line.find("[Language]") == std::string_view::npos &&
        line.find("[Voice]") == std::string_view::npos &&
        line.find("[BGM]") == std::string_view::npos)
      continue;

    const size_t rb = line.find(']');
    if (rb == std::string_view::npos)
      continue;
    AddLangCodes(line.substr(rb + 1), out);
  }
  return out;
}

DiscImage::DiscImage(std::unique_ptr<rex::filesystem::Device> device,
                     size_t content_size)
    : device_(std::move(device)), content_size_(content_size) {
  auto *root = device_->ResolvePath("");
  if (!root)
    return;
  for (int n = 1; n <= kDiscCount; ++n) {
    if (ValidateDisc(*root, n)) {
      roots_[n - 1] = root;
      continue;
    }
    auto *sub = root->ResolvePath("Disc" + std::to_string(n));
    if (sub && ValidateDisc(*sub, n))
      roots_[n - 1] = sub;
  }
}

DiscImage::~DiscImage() = default;

std::unique_ptr<DiscImage> DiscImage::Open(const fs::path &file) {
  std::error_code ec;
  if (!fs::is_regular_file(file, ec))
    return nullptr;

  auto disc = std::make_unique<rex::filesystem::DiscImageDevice>("", file);
  if (disc->Initialize())
    return std::make_unique<DiscImage>(std::move(disc),
                                       fs::file_size(file, ec));

  auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(file);
  if (!header || !header->header.is_magic_valid())
    return nullptr;
  if (static_cast<rex::system::XContentType>(header->metadata.content_type) !=
          rex::system::XContentType::kGamesOnDemand ||
      header->metadata.execution_info.title_id != kBlueDragonTitleId)
    return nullptr;
  auto container =
      std::make_unique<rex::filesystem::StfsContainerDevice>("", file);
  if (!container->Initialize())
    return nullptr;
  return std::make_unique<DiscImage>(
      std::move(container),
      static_cast<size_t>(header->metadata.data_file_size));
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
  if (CopyEntry(*entry, dest_path))
    return true;
  progress.SetError(i18n::Fmt("installer.error.copy_failed", entry->path()));
  progress.failed.store(true);
  return false;
}

constexpr std::string_view kIPKExt = ".ipk";
constexpr std::string_view kIPKMagic = "IPK1";

bool IsDamagedIPK(const std::string &relative_path, const fs::path &file) {
  if (!relative_path.ends_with(kIPKExt))
    return false;
  char magic[kIPKMagic.size()] = {};
  std::ifstream in(file, std::ios::binary);
  in.read(magic, sizeof(magic));
  return in.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
         std::string_view(magic, sizeof(magic)) != kIPKMagic;
}

} // namespace

std::thread
Installer::RunAsync(const std::array<fs::path, kDiscCount> &sources,
                    const fs::path &game_data_dest, bool repair,
                    InstallProgress &progress) {
  return std::thread([sources, game_data_dest, repair, &progress]() {
    std::map<fs::path, std::unique_ptr<DiscImage>> images;
    std::array<rex::filesystem::Entry *, kDiscCount> roots{};
    for (int i = 0; i < kDiscCount; ++i) {
      auto &image = images[sources[i]];
      if (!image)
        image = DiscImage::Open(sources[i]);
      if (image)
        roots[i] = image->Root(i + 1);
      if (!roots[i]) {
        progress.SetError(i18n::Fmt("installer.error.open_source",
                                    sources[i].filename().string(), i + 1));
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
    }

    std::set<std::string> available;
    for (auto *root : roots) {
      const std::set<std::string> langs = ParseDiscLanguages(*root);
      available.insert(langs.begin(), langs.end());
    }
    if (available.empty())
      available.insert("us");
    {
      std::string joined;
      for (const auto &l : available)
        joined += l + " ";
      BD_INFO("Installer: languages available on discs: {}", joined);
    }

    const auto manifest = LoadManifest();
    if (manifest.empty()) {
      progress.SetError(i18n::Text("installer.error.manifest_missing"));
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    struct PlanItem {
      std::string path;
      rex::filesystem::Entry *entry;
      size_t size;
      bool needs_copy;
    };
    std::vector<PlanItem> plan;
    plan.reserve(manifest.size());
    size_t total_bytes = 0;
    size_t missing = 0;
    size_t hits_per_disc[kDiscCount] = {};

    auto will_copy = [&](const std::string &path,
                         rex::filesystem::Entry *entry) -> bool {
      if (!entry)
        return false;
      if (!repair)
        return true;
      std::error_code ec;
      const auto dest = game_data_dest / fs::path(path);
      if (!fs::exists(dest, ec) || fs::file_size(dest, ec) != entry->size())
        return true;
      return IsDamagedIPK(path, dest);
    };
    for (const auto &path : manifest) {
      if (IsUnsafePath(path)) {
        BD_ERROR("Manifest contains unsafe path: {}", path);
        progress.SetError(i18n::Fmt("installer.error.manifest_bad", path));
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      rex::filesystem::Entry *entry = nullptr;
      for (size_t i = 0; i < roots.size(); ++i) {
        if (auto *e = roots[i]->ResolvePath(path); e != nullptr) {
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

    std::set<std::string> seen_lang;
    for (const auto &lang : available) {
      const std::string packmem = "pack/packmem_" + lang + ".ipk";
      const std::string lang_dirs[] = {"snd_memory_" + lang,
                                       "snd_stream_" + lang};
      for (size_t di = 0; di < roots.size(); ++di) {
        auto *root = roots[di];

        if (auto *e = root->ResolvePath(packmem);
            e != nullptr && seen_lang.insert(packmem).second) {
          const bool needs_copy = will_copy(packmem, e);
          if (needs_copy)
            total_bytes += e->size();
          plan.push_back({packmem, e, e->size(), needs_copy});
          ++hits_per_disc[di];
        }

        for (const auto &dir_name : lang_dirs) {
          auto *d = root->ResolvePath(dir_name);
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

      if (IsDamagedIPK(item.path, dest_path)) {
        BD_ERROR("Damaged archive on the install source: {}", item.path);
        progress.SetError(
            i18n::Fmt("installer.error.damaged_source", item.path));
        progress.failed.store(true);
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

    const auto marker_path = game_data_dest / "reblue_install.marker";
    std::ofstream marker(marker_path, std::ios::trunc);
    marker << "installed";
    if (!marker) {
      BD_ERROR("Failed to write install marker at '{}'", marker_path.string());
      progress.SetError(i18n::Fmt("installer.error.finish_failed",
                                  game_data_dest.string()));
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
