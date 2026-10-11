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
#include "installer/boot_languages.h"
#include "vfs/vfs.h"

namespace bd::installer {

namespace fs = std::filesystem;

namespace {

std::string DiscMarker(int disc_number) {
  return "bd_disc_" + std::to_string(disc_number) + ".xml";
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

using DiscRoots = std::array<rex::filesystem::Entry *, kDiscCount>;

bool OpenRoots(const std::array<fs::path, kDiscCount> &sources,
               std::map<fs::path, std::unique_ptr<DiscImage>> &images,
               DiscRoots &roots, InstallProgress &progress) {
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
      return false;
    }
  }
  return true;
}

struct Selected {
  std::set<std::string> text;
  std::set<std::string> voice;
  bool movies = true;

  std::set<std::string> Codes() const {
    std::set<std::string> out(text.begin(), text.end());
    out.insert(voice.begin(), voice.end());
    return out;
  }
};

Selected Resolve(const InstallSelection &selection) {
  Selected out;
  out.movies = selection.movies;
  for (const auto &lang : selection.languages) {
    if (lang.text)
      out.text.insert(lang.code);
    if (lang.voice)
      out.voice.insert(lang.code);
  }
  return out;
}

std::string ForwardSlashes(const std::string &path) {
  std::string out = path;
  for (auto &ch : out) {
    if (ch == '\\')
      ch = '/';
  }
  return out;
}

constexpr std::string_view kMovieDirs[] = {"movie/", "map/movie/"};
constexpr std::string_view kPackPrefix = "pack/packmem_";
constexpr std::string_view kVoiceDirs[] = {"snd_memory_", "snd_stream_"};

bool IsMovieRow(const std::string &path) {
  const std::string norm = ForwardSlashes(path);
  for (auto dir : kMovieDirs) {
    if (norm.rfind(dir, 0) == 0)
      return true;
  }
  return false;
}

bool Wanted(const std::string &path, const Selected &selection) {
  const std::string norm = ForwardSlashes(path);

  for (auto dir : kMovieDirs) {
    if (norm.rfind(dir, 0) == 0)
      return selection.movies;
  }

  if (norm.rfind(kPackPrefix, 0) == 0 && norm.ends_with(kIPKExt)) {
    const size_t start = kPackPrefix.size();
    const size_t len = norm.size() - start - kIPKExt.size();
    return selection.text.count(norm.substr(start, len)) != 0;
  }

  const std::string head = norm.substr(0, norm.find('/'));
  for (auto dir : kVoiceDirs) {
    if (head.rfind(dir, 0) == 0)
      return selection.voice.count(head.substr(dir.size())) != 0;
  }

  return true;
}

struct PlanItem {
  std::string path;
  rex::filesystem::Entry *entry;
  size_t size;
  bool needs_copy;
};

struct InstallPlan {
  fs::path dest;
  bool skip_present = false;

  std::vector<PlanItem> items;
  size_t total_bytes = 0;
  size_t hits_per_disc[kDiscCount] = {};
  std::set<std::string> seen_lang;

  bool WillCopy(const std::string &path, rex::filesystem::Entry *entry) const {
    if (!entry)
      return false;
    if (!skip_present)
      return true;
    std::error_code ec;
    const auto present = dest / fs::path(path);
    if (!fs::exists(present, ec) || fs::file_size(present, ec) != entry->size())
      return true;
    return IsDamagedIPK(path, present);
  }

  void Add(const std::string &path, rex::filesystem::Entry *entry, int disc,
           bool force = false) {
    const size_t size = entry ? entry->size() : 0;
    const bool needs_copy = force ? entry != nullptr : WillCopy(path, entry);
    if (needs_copy)
      total_bytes += size;
    if (disc >= 0)
      ++hits_per_disc[disc];
    items.push_back({path, entry, size, needs_copy});
  }

  void AddMovies(const DiscRoots &roots,
                 const std::vector<std::string> &manifest) {
    for (const auto &path : manifest) {
      if (IsUnsafePath(path) || !IsMovieRow(path))
        continue;
      for (int di = 0; di < kDiscCount; ++di) {
        auto *e = roots[di]->ResolvePath(path);
        if (e == nullptr)
          continue;
        Add(path, e, di, true);
        break;
      }
    }
  }

  void AddLanguage(const DiscRoots &roots, const std::string &lang, bool text,
                   bool voice) {
    const std::string packmem = "pack/packmem_" + lang + ".ipk";
    const std::string lang_dirs[] = {"snd_memory_" + lang,
                                     "snd_stream_" + lang};
    for (int di = 0; di < kDiscCount; ++di) {
      auto *root = roots[di];

      if (text) {
        if (auto *e = root->ResolvePath(packmem);
            e != nullptr && seen_lang.insert(packmem).second) {
          Add(packmem, e, di);
        }
      }

      if (!voice)
        continue;

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
          Add(rel, entry, di);
        }
      }
    }
  }
};

bool RunCopyLoop(const InstallPlan &plan, const fs::path &game_data_dest,
                 InstallProgress &progress, size_t &skipped) {
  for (const auto &item : plan.items) {
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
    if (!ExtractOne(item.entry, dest_path, progress))
      return false;

    if (IsDamagedIPK(item.path, dest_path)) {
      BD_ERROR("Damaged archive on the install source: {}", item.path);
      progress.SetError(i18n::Fmt("installer.error.damaged_source", item.path));
      progress.failed.store(true);
      return false;
    }

    progress.files_done.fetch_add(1);
    progress.bytes_done.fetch_add(item.size);
  }

  return !progress.failed.load() && !progress.canceled.load();
}

BootLanguages DiscUnion(const DiscRoots &roots) {
  BootLanguages out = BootLanguages::FromDisc(*roots[0]);
  for (int i = 1; i < kDiscCount; ++i) {
    const BootLanguages other = BootLanguages::FromDisc(*roots[i]);
    out.Add(other, other.All());
  }
  return out;
}

bool FlushBootIni(const BootLanguages &langs, const fs::path &game_data_dest,
                  InstallProgress &progress) {
  const auto ini_path = game_data_dest / "bd_boot.ini";
  if (langs.Flush(ini_path))
    return true;
  BD_ERROR("Failed to update '{}'", ini_path.string());
  progress.SetError(i18n::Fmt("installer.error.boot_ini", ini_path.string()));
  progress.failed.store(true);
  return false;
}

void RebuildPackIndex(const fs::path &game_data_dest,
                      InstallProgress &progress) {
  progress.SetCurrentFile("pack index");
  vfs::VFS::BuildPackIndex(game_data_dest,
                           bd::CacheRootFor(game_data_dest.parent_path()));
}

} // namespace

std::thread
Installer::RunAsync(const std::array<fs::path, kDiscCount> &sources,
                    const fs::path &game_data_dest, bool repair,
                    const InstallSelection &selection,
                    InstallProgress &progress) {
  return std::thread([sources, game_data_dest, repair, selection, &progress]() {
    std::map<fs::path, std::unique_ptr<DiscImage>> images;
    DiscRoots roots{};
    if (!OpenRoots(sources, images, roots, progress)) {
      progress.complete.store(true);
      return;
    }

    std::set<std::string> available;
    for (auto *root : roots) {
      const std::set<std::string> langs = BootLanguages::FromDisc(*root).All();
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

    Selected chosen = Resolve(selection);
    if (chosen.text.empty() && chosen.voice.empty()) {
      chosen.text = available;
      chosen.voice = available;
    }
    {
      std::string text_list;
      for (const auto &l : chosen.text)
        text_list += l + " ";
      std::string voice_list;
      for (const auto &l : chosen.voice)
        voice_list += l + " ";
      BD_INFO("Installer: text [{}], voice [{}], movies {}", text_list,
              voice_list, chosen.movies ? "yes" : "no");
    }

    const auto manifest = LoadManifest();
    if (manifest.empty()) {
      progress.SetError(i18n::Text("installer.error.manifest_missing"));
      progress.failed.store(true);
      progress.complete.store(true);
      return;
    }

    InstallPlan plan;
    plan.dest = game_data_dest;
    plan.skip_present = repair;
    plan.items.reserve(manifest.size());
    size_t missing = 0;

    for (const auto &path : manifest) {
      if (IsUnsafePath(path)) {
        BD_ERROR("Manifest contains unsafe path: {}", path);
        progress.SetError(i18n::Fmt("installer.error.manifest_bad", path));
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      if (!Wanted(path, chosen))
        continue;
      rex::filesystem::Entry *entry = nullptr;
      int disc = -1;
      for (int i = 0; i < kDiscCount; ++i) {
        if (auto *e = roots[i]->ResolvePath(path); e != nullptr) {
          entry = e;
          disc = i;
          break;
        }
      }
      if (!entry) {
        BD_WARN("MISSING: {} (not on any disc)", path);
        ++missing;
      }
      plan.Add(path, entry, disc);
    }

    for (const auto &lang : available) {
      const bool text = chosen.text.count(lang) != 0;
      const bool voice = chosen.voice.count(lang) != 0;
      if (text || voice)
        plan.AddLanguage(roots, lang, text, voice);
    }

    progress.files_total.store(plan.items.size());
    progress.bytes_total.store(plan.total_bytes);

    BD_INFO("Install plan ({}): {} files, {} bytes to copy",
            repair ? "repair" : "full", plan.items.size(), plan.total_bytes);
    BD_INFO("  DVD1: {} files, DVD2: {} files, DVD3: {} files, missing: {}",
            plan.hits_per_disc[0], plan.hits_per_disc[1], plan.hits_per_disc[2],
            missing);

    std::error_code ec;
    fs::create_directories(game_data_dest, ec);

    size_t skipped = 0;
    if (!RunCopyLoop(plan, game_data_dest, progress, skipped)) {
      progress.complete.store(true);
      return;
    }

    const BootLanguages disc = DiscUnion(roots);
    BootLanguages installed = disc;
    installed.Keep(chosen.text, chosen.voice);
    installed.SetMovie(chosen.movies ? disc.Voice()
                                     : std::vector<std::string>());
    if (!FlushBootIni(installed, game_data_dest, progress)) {
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

    RebuildPackIndex(game_data_dest, progress);

    if (repair) {
      BD_INFO("Repair complete: {} files already present, {} missing on discs.",
              skipped, missing);
    } else {
      BD_INFO("Installation complete ({} files missing - see log).", missing);
    }
    progress.complete.store(true);
  });
}

std::thread Installer::AddLanguagesAsync(
    const std::array<fs::path, kDiscCount> &sources,
    const fs::path &game_data_dest, const InstallSelection &selection,
    InstallProgress &progress) {
  return std::thread([sources, game_data_dest, selection, &progress]() {
    std::map<fs::path, std::unique_ptr<DiscImage>> images;
    DiscRoots roots{};
    if (!OpenRoots(sources, images, roots, progress)) {
      progress.complete.store(true);
      return;
    }

    const Selected chosen = Resolve(selection);

    InstallPlan plan;
    plan.dest = game_data_dest;
    plan.skip_present = true;
    for (const auto &lang : selection.languages) {
      if (lang.text || lang.voice)
        plan.AddLanguage(roots, lang.code, lang.text, lang.voice);
    }
    if (chosen.movies) {
      const auto manifest = LoadManifest();
      if (manifest.empty()) {
        progress.SetError(i18n::Text("installer.error.manifest_missing"));
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      plan.AddMovies(roots, manifest);
    }

    progress.files_total.store(plan.items.size());
    progress.bytes_total.store(plan.total_bytes);

    std::string joined;
    for (const auto &lang : selection.languages) {
      if (!lang.text && !lang.voice)
        continue;
      if (!joined.empty())
        joined += ' ';
      joined += lang.code;
      if (!lang.voice)
        joined += "(text)";
      else if (!lang.text)
        joined += "(voice)";
    }
    if (chosen.movies) {
      if (!joined.empty())
        joined += ' ';
      joined += "movies";
    }
    BD_INFO("Language install plan: {} ({} files, {} bytes to copy)", joined,
            plan.items.size(), plan.total_bytes);

    size_t skipped = 0;
    if (!RunCopyLoop(plan, game_data_dest, progress, skipped)) {
      progress.complete.store(true);
      return;
    }

    BootLanguages installed =
        BootLanguages::FromFile(game_data_dest / "bd_boot.ini");
    const BootLanguages disc = DiscUnion(roots);
    BootLanguages picked = disc;
    picked.Keep(chosen.text, chosen.voice);
    installed.Add(picked, chosen.Codes());
    if (chosen.movies)
      installed.SetMovie(disc.Voice());
    if (!FlushBootIni(installed, game_data_dest, progress)) {
      progress.complete.store(true);
      return;
    }

    RebuildPackIndex(game_data_dest, progress);

    BD_INFO("Language install complete: {}", joined);
    progress.complete.store(true);
  });
}

std::thread Installer::RemoveLanguageAsync(const fs::path &game_data_dest,
                                           const std::string &lang,
                                           InstallProgress &progress) {
  return std::thread([game_data_dest, lang, &progress]() {
    const std::array<fs::path, 3> targets = {
        game_data_dest / "pack" / ("packmem_" + lang + ".ipk"),
        game_data_dest / ("snd_memory_" + lang),
        game_data_dest / ("snd_stream_" + lang)};

    progress.files_total.store(targets.size());

    for (const auto &target : targets) {
      progress.SetCurrentFile(target.filename().string());

      std::error_code probe;
      std::error_code ec;
      if (fs::is_directory(target, probe))
        fs::remove_all(target, ec);
      else
        fs::remove(target, ec);

      if (ec) {
        BD_ERROR("Failed to delete '{}': {}", target.string(), ec.message());
        progress.SetError(ec.message());
        progress.failed.store(true);
        progress.complete.store(true);
        return;
      }
      progress.files_done.fetch_add(1);
    }

    BootLanguages installed =
        BootLanguages::FromFile(game_data_dest / "bd_boot.ini");
    installed.Remove(lang);
    if (!FlushBootIni(installed, game_data_dest, progress)) {
      progress.complete.store(true);
      return;
    }

    RebuildPackIndex(game_data_dest, progress);

    BD_INFO("Language removal complete: {}", lang);
    progress.complete.store(true);
  });
}

} // namespace bd::installer
