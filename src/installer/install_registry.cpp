#include "installer/install_registry.h"

#include <rex/platform.h>

#include "bdengine/common/logging.h"

#if REX_PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace reblue {
namespace {

constexpr wchar_t kInstallKey[] = L"Software\\Zolaware\\reblue\\Install";

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
  return out;
}

std::string WideToUtf8(const std::wstring& s) {
  if (s.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr,
                      nullptr);
  return out;
}

std::optional<std::wstring> ReadString(HKEY key, const wchar_t* name) {
  DWORD type = 0;
  DWORD size = 0;
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  std::wstring out(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, out.data(), &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  // RegGetValueW includes the terminating null in `size` - trim it.
  while (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
  auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
}

}  // namespace

std::optional<InstallConfig> ReadInstallRegistry() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kInstallKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  struct KeyGuard { HKEY k; ~KeyGuard() { if (k) RegCloseKey(k); } } guard{key};

  auto root_w = ReadString(key, L"InstallRoot");
  if (!root_w || root_w->empty()) return std::nullopt;

  InstallConfig cfg;
  cfg.install_root = *root_w;

  auto read_fp = [&](const wchar_t* name) -> std::string {
    auto w = ReadString(key, name);
    return w ? WideToUtf8(*w) : std::string{};
  };
  cfg.iso1_fingerprint = read_fp(L"Disc1Fingerprint");
  cfg.iso2_fingerprint = read_fp(L"Disc2Fingerprint");
  cfg.iso3_fingerprint = read_fp(L"Disc3Fingerprint");

  const auto default_xex = cfg.game_data_path() / "default.xex";
  if (!std::filesystem::exists(default_xex)) {
    BD_WARN("Install registry present but {} missing - treating as uninstalled",
            default_xex.string());
    return std::nullopt;
  }

  return cfg;
}

bool WriteInstallRegistry(const InstallConfig& config) {
  HKEY key = nullptr;
  LONG create_status = RegCreateKeyExW(HKEY_CURRENT_USER, kInstallKey, 0, nullptr,
                                       REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
                                       nullptr);
  if (create_status != ERROR_SUCCESS) {
    BD_ERROR("RegCreateKeyExW failed: {}", create_status);
    return false;
  }
  {
    struct KeyGuard { HKEY k; ~KeyGuard() { if (k) RegCloseKey(k); } } guard{key};

    bool ok = true;
    ok &= WriteString(key, L"InstallRoot", config.install_root.wstring());
    ok &= WriteString(key, L"Disc1Fingerprint", Utf8ToWide(config.iso1_fingerprint));
    ok &= WriteString(key, L"Disc2Fingerprint", Utf8ToWide(config.iso2_fingerprint));
    ok &= WriteString(key, L"Disc3Fingerprint", Utf8ToWide(config.iso3_fingerprint));
    if (ok) return true;
    BD_ERROR("Failed to write one or more values to install registry");
  }
  // Partial write - leave the key in a consistent (absent) state so
  // ReadInstallRegistry returns nullopt and the installer re-runs.
  ClearInstallRegistry();
  return false;
}

bool ClearInstallRegistry() {
  LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, kInstallKey);
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) return true;
  BD_ERROR("RegDeleteTreeW failed: {}", status);
  return false;
}

}  // namespace reblue

#else  // non-Windows

namespace reblue {
std::optional<InstallConfig> ReadInstallRegistry() { return std::nullopt; }
bool WriteInstallRegistry(const InstallConfig&) { return false; }
bool ClearInstallRegistry() { return false; }
}  // namespace reblue

#endif
