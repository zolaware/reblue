/**
 * @file    platform/desktop_shortcut.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/desktop_shortcut.h"

#if defined(_WIN32)
#include "core/windows_lean.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <string>

#include "core/encoding.h"

namespace bd::platform {
namespace {

// A shortcut is named by its file, so a character Windows reserves in a path
// cannot reach one. A colon is the case that matters: 're:Blue.lnk' names an
// alternate data stream on a file called 're'.
std::wstring ShortcutFileName(std::string_view name) {
  std::wstring out = bd::Utf8ToWide(name);
  std::erase_if(out, [](wchar_t c) {
    return c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
           c == L'\\' || c == L'|' || c == L'?' || c == L'*' || c < 32;
  });
  return out;
}

// S_FALSE still took a reference and has to be released. Only
// RPC_E_CHANGED_MODE leaves nothing to tear down.
class ComScope {
public:
  ComScope() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    owned_ = SUCCEEDED(hr);
    ok_ = owned_ || hr == RPC_E_CHANGED_MODE;
  }
  ~ComScope() {
    if (owned_)
      CoUninitialize();
  }
  ComScope(const ComScope &) = delete;
  ComScope &operator=(const ComScope &) = delete;
  explicit operator bool() const { return ok_; }

private:
  bool ok_ = false;
  bool owned_ = false;
};

bool DesktopShortcutPath(std::string_view name, std::filesystem::path &out,
                         std::string &error) {
  const std::wstring file = ShortcutFileName(name);
  if (file.empty()) {
    error = "shortcut name is empty once path characters are removed";
    return false;
  }
  PWSTR desktop = nullptr;
  const HRESULT hr =
      SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop);
  if (FAILED(hr)) {
    error = "SHGetKnownFolderPath(FOLDERID_Desktop) failed";
    return false;
  }
  out = std::filesystem::path(desktop) / (file + L".lnk");
  CoTaskMemFree(desktop);
  return true;
}

bool WriteShortcut(const std::filesystem::path &target,
                   const std::filesystem::path &shortcut_path,
                   std::string &error) {
  IShellLinkW *link = nullptr;
  HRESULT hr =
      CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IShellLinkW, reinterpret_cast<void **>(&link));
  if (FAILED(hr)) {
    error = "CoCreateInstance(CLSID_ShellLink) failed";
    return false;
  }

  hr = link->SetPath(target.c_str());
  if (FAILED(hr)) {
    link->Release();
    error = "IShellLinkW::SetPath failed";
    return false;
  }
  hr = link->SetWorkingDirectory(target.parent_path().c_str());
  if (FAILED(hr)) {
    link->Release();
    error = "IShellLinkW::SetWorkingDirectory failed";
    return false;
  }

  IPersistFile *persist_file = nullptr;
  hr = link->QueryInterface(IID_IPersistFile,
                            reinterpret_cast<void **>(&persist_file));
  if (FAILED(hr)) {
    link->Release();
    error = "QueryInterface(IID_IPersistFile) failed";
    return false;
  }

  hr = persist_file->Save(shortcut_path.c_str(), TRUE);
  persist_file->Release();
  link->Release();
  if (FAILED(hr)) {
    error = "IPersistFile::Save failed";
    return false;
  }
  return true;
}

} // namespace

bool CreateDesktopShortcut(const std::filesystem::path &target,
                           std::string_view name, std::string &error) {
  const ComScope com;
  if (!com) {
    error = "CoInitializeEx failed";
    return false;
  }
  std::filesystem::path shortcut_path;
  if (!DesktopShortcutPath(name, shortcut_path, error))
    return false;
  return WriteShortcut(target, shortcut_path, error);
}

} // namespace bd::platform

#else

namespace bd::platform {

bool CreateDesktopShortcut(const std::filesystem::path &, std::string_view,
                           std::string &error) {
  error = "Desktop shortcuts are not supported on this platform";
  return false;
}

} // namespace bd::platform

#endif
