#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace reblue {

struct InstallConfig {
  // Single install root. Game files live under {install_root}/game,
  // user/DLC files under {install_root}/user.
  std::filesystem::path install_root;
  std::string iso1_fingerprint;
  std::string iso2_fingerprint;
  std::string iso3_fingerprint;

  std::filesystem::path game_data_path() const { return install_root / "game"; }
  std::filesystem::path user_data_path() const { return install_root / "user"; }
};

// Reads HKCU\Software\Zolaware\reblue\Install. Returns nullopt if the key is
// missing or the recorded install_root/game/default.xex is absent.
std::optional<InstallConfig> ReadInstallRegistry();

// Writes InstallRoot and the three disc fingerprints.
bool WriteInstallRegistry(const InstallConfig& config);

// Deletes the Install subkey. Returns true on success (or if absent).
bool ClearInstallRegistry();

}  // namespace reblue
