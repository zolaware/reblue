#include "platform/cloud_saves.h"

#include "core/logging.h"
#include "core/settings.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && defined(REBLUE_ICLOUD_CONTAINER)

#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
#include "platform/ios_support.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace bd::platform::cloud_saves {
namespace {

namespace fs = std::filesystem;

constexpr auto kTolerance = std::chrono::seconds(2);
constexpr size_t kBackupsKept = 10;

std::mutex g_mutex;
fs::path g_local;
fs::path g_cloud;
bool g_enabled = false;
std::string g_last_sync;

bool IsPlaceholder(const fs::path &path) {
  const auto name = path.filename().string();
  return name.size() > 7 && name.front() == '.' && name.ends_with(".icloud");
}

std::optional<fs::file_time_type> SlotTime(const fs::path &slot) {
  std::error_code ec;
  std::optional<fs::file_time_type> save_file_time;
  std::optional<fs::file_time_type> any_time;
  for (const auto &entry : fs::directory_iterator(slot, ec)) {
    if (!entry.is_regular_file(ec))
      continue;
    if (IsPlaceholder(entry.path()))
      return std::nullopt;
    const auto t = entry.last_write_time(ec);
    if (ec)
      continue;
    if (!any_time || t > *any_time)
      any_time = t;
    if (entry.path().filename() == "savegame.dat")
      save_file_time = t;
  }
  return save_file_time ? save_file_time : any_time;
}

std::vector<std::string> SlotNames(const fs::path &root) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(root, ec)) {
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec) && !name.empty() && name.front() != '.')
      names.push_back(name);
  }
  return names;
}

bool CopySlot(const fs::path &source, const fs::path &destination,
              std::string &error) {
  std::error_code ec;
  const auto temporary = destination.parent_path() /
                         ("." + destination.filename().string() + ".sync-tmp");
  fs::remove_all(temporary, ec);
  fs::create_directories(temporary, ec);
  if (ec) {
    error = "create " + temporary.string() + ": " + ec.message();
    return false;
  }
  for (const auto &entry : fs::directory_iterator(source, ec)) {
    if (!entry.is_regular_file(ec) || IsPlaceholder(entry.path()))
      continue;
    const auto target = temporary / entry.path().filename();
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing,
                  ec);
    if (ec) {
      error = "copy " + entry.path().string() + ": " + ec.message();
      fs::remove_all(temporary, ec);
      return false;
    }
    fs::last_write_time(target, entry.last_write_time(ec), ec);
  }
  if (ec) {
    error = "read " + source.string() + ": " + ec.message();
    fs::remove_all(temporary, ec);
    return false;
  }
  fs::remove_all(destination, ec);
  fs::rename(temporary, destination, ec);
  if (ec) {
    error = "publish " + destination.string() + ": " + ec.message();
    return false;
  }
  return true;
}

void BackupLocalSlot(const fs::path &slot) {
  std::error_code ec;
  const auto backups = g_local / ".cloud-backup";
  fs::create_directories(backups, ec);
  const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  const auto target =
      backups / (slot.filename().string() + "-" + std::to_string(stamp));
  fs::rename(slot, target, ec);
  if (ec) {
    BD_WARN("[icloud] could not back up {}: {}", slot.string(), ec.message());
  } else {
    BD_INFO("[icloud] backed up replaced local slot to {}", target.string());
  }

  const auto stamp_of = [](const fs::path &p) {
    const auto name = p.filename().string();
    const auto dash = name.rfind('-');
    return dash == std::string::npos ? 0ll
                                     : std::atoll(name.c_str() + dash + 1);
  };
  std::vector<fs::path> entries;
  for (const auto &entry : fs::directory_iterator(backups, ec))
    entries.push_back(entry.path());
  if (entries.size() <= kBackupsKept)
    return;
  std::sort(entries.begin(), entries.end(), [&](const fs::path &a,
                                                const fs::path &b) {
    return stamp_of(a) < stamp_of(b);
  });
  for (size_t i = 0; i + kBackupsKept < entries.size(); ++i)
    fs::remove_all(entries[i], ec);
}

fs::path ResolveCloudRoot() {
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  const auto docs = ios::UbiquityDocuments(REBLUE_ICLOUD_CONTAINER);
  return docs.empty() ? fs::path{} : docs / "Saves";
#else
  std::string folder = REBLUE_ICLOUD_CONTAINER;
  if (folder.empty())
    return {};
  std::replace(folder.begin(), folder.end(), '.', '~');
  const char *home = std::getenv("HOME");
  if (!home || !*home)
    return {};
  const auto container =
      fs::path(home) / "Library" / "Mobile Documents" / folder;
  std::error_code ec;
  if (!fs::is_directory(container, ec))
    return {};
  return container / "Documents" / "Saves";
#endif
}

std::string Describe(int pulled, int pushed) {
  const auto now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char stamp[32] = {};
  std::tm local{};
  localtime_r(&now, &local);
  std::strftime(stamp, sizeof(stamp), "%H:%M", &local);
  if (pulled == 0 && pushed == 0)
    return std::string("up to date at ") + stamp;
  return std::to_string(pulled) + " from iCloud, " + std::to_string(pushed) +
         " to iCloud at " + stamp;
}

} // namespace

void Start(const fs::path &local_saves) {
  std::lock_guard lock(g_mutex);
  g_local = local_saves;
  g_enabled = bd::Settings::Get().ICloudSaves();
  if (!g_enabled) {
    g_last_sync = "off";
    BD_INFO("[icloud] saves sync disabled via bd_icloud_saves");
    return;
  }
  g_cloud = ResolveCloudRoot();
  if (g_cloud.empty()) {
    g_last_sync = "iCloud unavailable";
    BD_INFO("[icloud] container {} unavailable (signed out or not created yet)",
            REBLUE_ICLOUD_CONTAINER);
    return;
  }
  std::error_code ec;
  fs::create_directories(g_cloud, ec);
  fs::create_directories(g_local, ec);
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  ios::DownloadUbiquitousItems(g_cloud, std::chrono::seconds(15));
#endif

  int pulled = 0;
  int pushed = 0;
  std::vector<std::string> names = SlotNames(g_local);
  for (auto &name : SlotNames(g_cloud)) {
    if (std::find(names.begin(), names.end(), name) == names.end())
      names.push_back(std::move(name));
  }
  for (const auto &name : names) {
    const auto local = g_local / name;
    const auto cloud = g_cloud / name;
    const auto local_time =
        fs::is_directory(local, ec) ? SlotTime(local) : std::nullopt;
    const auto cloud_time =
        fs::is_directory(cloud, ec) ? SlotTime(cloud) : std::nullopt;
    std::string error;
    if (cloud_time && (!local_time || *cloud_time > *local_time + kTolerance)) {
      if (local_time)
        BackupLocalSlot(local);
      if (CopySlot(cloud, local, error))
        ++pulled;
      else
        BD_WARN("[icloud] pull '{}' failed: {}", name, error);
    } else if (local_time &&
               (!cloud_time || *local_time > *cloud_time + kTolerance)) {
      if (CopySlot(local, cloud, error))
        ++pushed;
      else
        BD_WARN("[icloud] push '{}' failed: {}", name, error);
    }
  }
  g_last_sync = Describe(pulled, pushed);
  BD_INFO("[icloud] {} ({} slot(s), cloud folder {})", g_last_sync,
          names.size(), g_cloud.string());
}

void PushSlot(const fs::path &slot_dir) {
  if (slot_dir.empty())
    return;
  {
    std::lock_guard lock(g_mutex);
    if (!g_enabled || g_cloud.empty())
      return;
  }
  std::thread([slot_dir] {
    std::lock_guard lock(g_mutex);
    std::string error;
    if (CopySlot(slot_dir, g_cloud / slot_dir.filename(), error)) {
      g_last_sync = Describe(0, 1);
      BD_INFO("[icloud] pushed slot '{}'", slot_dir.filename().string());
    } else {
      BD_WARN("[icloud] push '{}' failed: {}", slot_dir.filename().string(),
              error);
    }
  }).detach();
}

Status GetStatus() {
  std::lock_guard lock(g_mutex);
  return {true, g_enabled, !g_cloud.empty(), g_last_sync};
}

} // namespace bd::platform::cloud_saves

#else

namespace bd::platform::cloud_saves {
void Start(const std::filesystem::path &) {}
void PushSlot(const std::filesystem::path &) {}
Status GetStatus() { return {}; }
} // namespace bd::platform::cloud_saves

#endif
