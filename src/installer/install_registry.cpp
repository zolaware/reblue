/**
 * @file    installer/install_registry.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/install_registry.h"

#include <filesystem>
#include <fstream>

#include <rex/filesystem.h>
#include <rex/types.h>
#include <toml++/toml.h>

#include "core/app_root.h"
#include "core/build_info.h"
#include "core/encoding.h"
#include "core/logging.h"

#if defined(_WIN32)
#include "core/windows_lean.h"
#endif

namespace bd::installer {
namespace {

namespace fs = std::filesystem;

// Every field added since schema 1 has a usable default, so an older record
// comes forward as it stands. A newer one cannot: this build has no idea what
// it left out, and leaving schema_version as written is what tells the caller.
void Migrate(InstallConfig &cfg) {
  const int stored = cfg.schema_version;
  if (stored > kInstallSchemaVersion) {
    BD_WARN("Install record schema {} was written by a newer build", stored);
    return;
  }
  cfg.schema_version = kInstallSchemaVersion;
  if (stored != kInstallSchemaVersion)
    BD_INFO("Install record migrated from schema {} to {}", stored,
            kInstallSchemaVersion);
}

bool HasGameFiles(const InstallConfig &cfg) {
  const auto default_xex = cfg.game_data_path() / "default.xex";
  std::error_code ec;
  if (fs::exists(default_xex, ec))
    return true;
  BD_WARN("Install record ({}) present but {} missing, treating as uninstalled",
          ToString(cfg.connector), default_xex.string());
  return false;
}

std::optional<InstallConfig> Finish(InstallConfig cfg) {
  if (cfg.install_root.empty())
    return std::nullopt;
  Migrate(cfg);
  if (!HasGameFiles(cfg))
    return std::nullopt;
  return cfg;
}

fs::path PortableRecordPath() {
  return rex::filesystem::GetExecutableFolder() / kPortableRecordName;
}

std::string FingerprintKey(int index) {
  return "disc" + std::to_string(index + 1) + "_fingerprint";
}

toml::table Serialize(const InstallConfig &cfg, bool with_root) {
  toml::table t;
  if (with_root)
    t.insert("install_root", cfg.install_root.string());
  for (int i = 0; i < kDiscCount; ++i)
    t.insert(FingerprintKey(i), cfg.iso_fingerprints[i]);
  t.insert("schema_version", static_cast<i64>(kInstallSchemaVersion));
  t.insert("app_version", std::string(REBLUE_VERSION_STRING));
  return t;
}

std::optional<InstallConfig> ReadRecordFile(const fs::path &path,
                                            InstallConnector connector) {
  std::error_code ec;
  if (path.empty() || !fs::exists(path, ec))
    return std::nullopt;

  InstallConfig cfg;
  cfg.connector = connector;
  try {
    toml::table t = toml::parse_file(path.string());
    cfg.install_root = t["install_root"].value_or(std::string{});
    for (int i = 0; i < kDiscCount; ++i)
      cfg.iso_fingerprints[i] = t[FingerprintKey(i)].value_or(std::string{});
    cfg.schema_version = t["schema_version"].value_or(0);
    cfg.app_version = t["app_version"].value_or(std::string{});
  } catch (const toml::parse_error &e) {
    BD_WARN("Install record {} unreadable: {}", path.string(), e.what());
    return std::nullopt;
  }
  if (connector == InstallConnector::kPortableFile)
    cfg.install_root = path.parent_path();
  return Finish(std::move(cfg));
}

bool WriteRecordFile(const fs::path &path, const toml::table &t) {
  if (path.empty()) {
    BD_ERROR("Install record: neither XDG_CONFIG_HOME nor HOME is set");
    return false;
  }
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    BD_ERROR("Install record: cannot create {}: {}",
             path.parent_path().string(), ec.message());
    return false;
  }

  const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      BD_ERROR("Install record: cannot write {}", tmp.string());
      return false;
    }
    out << t << '\n';
    out.flush();
    if (!out.good()) {
      BD_ERROR("Install record: write to {} failed", tmp.string());
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    BD_ERROR("Install record: rename to {} failed: {}", path.string(),
             ec.message());
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

bool RemoveRecordFile(const fs::path &path) {
  if (path.empty())
    return true;
  std::error_code ec;
  fs::remove(path, ec);
  return !fs::exists(path, ec);
}

#if defined(_WIN32)

constexpr wchar_t kInstallKey[] = L"Software\\Zolaware\\reblue\\Install";

std::optional<std::wstring> ReadString(HKEY key, const wchar_t *name) {
  DWORD type = 0;
  DWORD size = 0;
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &size) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  std::wstring out(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, out.data(),
                   &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  // RegGetValueW counts the terminating null in 'size'.
  while (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return out;
}

bool WriteString(HKEY key, const wchar_t *name, const std::wstring &value) {
  auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE *>(value.c_str()),
                        bytes) == ERROR_SUCCESS;
}

struct KeyGuard {
  HKEY k;
  ~KeyGuard() {
    if (k)
      RegCloseKey(k);
  }
};

std::optional<InstallConfig> ReadRegistry() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kInstallKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  KeyGuard guard{key};

  auto root_w = ReadString(key, L"InstallRoot");
  if (!root_w || root_w->empty())
    return std::nullopt;

  InstallConfig cfg;
  cfg.install_root = *root_w;

  auto read_fp = [&](const wchar_t *name) -> std::string {
    auto w = ReadString(key, name);
    return w ? bd::WideToUtf8(*w) : std::string{};
  };
  for (int i = 0; i < kDiscCount; ++i)
    cfg.iso_fingerprints[i] =
        read_fp((L"Disc" + std::to_wstring(i + 1) + L"Fingerprint").c_str());

  if (auto sv = ReadString(key, L"SchemaVersion")) {
    try {
      cfg.schema_version = std::stoi(*sv);
    } catch (...) {
      cfg.schema_version = 0;
    }
  }

  if (auto v = ReadString(key, L"AppVersion"))
    cfg.app_version = bd::WideToUtf8(*v);
  cfg.connector = InstallConnector::kRegistry;
  return Finish(std::move(cfg));
}

bool ClearRegistry() {
  LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, kInstallKey);
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
    return true;
  BD_ERROR("RegDeleteTreeW failed: {}", status);
  return false;
}

bool WriteRegistry(const InstallConfig &config) {
  HKEY key = nullptr;
  LONG create_status = RegCreateKeyExW(HKEY_CURRENT_USER, kInstallKey, 0,
                                       nullptr, REG_OPTION_NON_VOLATILE,
                                       KEY_WRITE, nullptr, &key, nullptr);
  if (create_status != ERROR_SUCCESS) {
    BD_ERROR("RegCreateKeyExW failed: {}", create_status);
    return false;
  }
  {
    KeyGuard guard{key};

    bool ok = true;
    ok &= WriteString(key, L"InstallRoot", config.install_root.wstring());
    for (int i = 0; i < kDiscCount; ++i)
      ok &= WriteString(
          key, (L"Disc" + std::to_wstring(i + 1) + L"Fingerprint").c_str(),
          bd::Utf8ToWide(config.iso_fingerprints[i]));
    ok &= WriteString(key, L"SchemaVersion",
                      std::to_wstring(kInstallSchemaVersion));
    ok &= WriteString(key, L"AppVersion",
                      bd::Utf8ToWide(REBLUE_VERSION_STRING));
    if (ok)
      return true;
    BD_ERROR("Failed to write one or more values to install record");
  }
  // Partial write: clear so InstallConfig::Read sees nullopt and re-runs the
  // installer.
  ClearRegistry();
  return false;
}

#else

fs::path ConfigRecordPath() {
  const auto base = bd::UserConfigFolder();
  return base.empty() ? fs::path{} : base / "install.toml";
}

#endif

} // namespace

const char *ToString(InstallConnector connector) {
  switch (connector) {
  case InstallConnector::kPortableFile:
    return "portable file";
  case InstallConnector::kRegistry:
    return "registry";
  case InstallConnector::kConfigFile:
    return "config file";
  }
  return "unknown";
}

std::optional<InstallConfig> InstallConfig::Read() {
  if (auto cfg = ReadRecordFile(PortableRecordPath(),
                                InstallConnector::kPortableFile))
    return cfg;
#if defined(_WIN32)
  return ReadRegistry();
#else
  return ReadRecordFile(ConfigRecordPath(), InstallConnector::kConfigFile);
#endif
}

bool InstallConfig::Write() const {
  switch (connector) {
  case InstallConnector::kPortableFile:
    return WriteRecordFile(PortableRecordPath(), Serialize(*this, false));
#if defined(_WIN32)
  case InstallConnector::kRegistry:
    return WriteRegistry(*this);
#else
  case InstallConnector::kConfigFile:
    return WriteRecordFile(ConfigRecordPath(), Serialize(*this, true));
#endif
  default:
    BD_ERROR("Install connector '{}' is not available on this platform",
             ToString(connector));
    return false;
  }
}

bool InstallConfig::Clear(InstallConnector connector) {
  switch (connector) {
  case InstallConnector::kPortableFile:
    return RemoveRecordFile(PortableRecordPath());
#if defined(_WIN32)
  case InstallConnector::kRegistry:
    return ClearRegistry();
#else
  case InstallConnector::kConfigFile:
    return RemoveRecordFile(ConfigRecordPath());
#endif
  default:
    return true;
  }
}

} // namespace bd::installer
