#pragma once

#include <filesystem>
#include <string>

namespace bd::platform::cloud_saves {

struct Status {
  bool supported = false;
  bool enabled = false;
  bool active = false;
  std::string last_sync;
};

// Reconcile local save slots with iCloud Drive. Call before mounting SaveStore.
void Start(const std::filesystem::path &local_saves);

// Push one save slot folder to iCloud Drive on a background thread.
void PushSlot(const std::filesystem::path &slot_dir);

Status GetStatus();

} // namespace bd::platform::cloud_saves
