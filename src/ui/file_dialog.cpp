#include "ui/file_dialog.h"

#include <rex/platform.h>

#if REX_PLATFORM_WIN32

#include <shobjidl.h>
#include <wrl/client.h>

#include <vector>

#include "bdengine/common/logging.h"

namespace reblue::ui {
namespace {

using Microsoft::WRL::ComPtr;

// RAII wrapper for CoInitializeEx on the calling thread. Falls back to MTA if
// the thread has already chosen that mode, and only calls CoUninitialize if
// we were the ones who succeeded in initialising.
class ComScope {
 public:
  ComScope() {
    hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (hr_ == RPC_E_CHANGED_MODE) {
      hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }
    if (FAILED(hr_) && hr_ != RPC_E_CHANGED_MODE) {
      BD_ERROR("CoInitializeEx failed: 0x{:08X}", static_cast<uint32_t>(hr_));
    }
  }
  ~ComScope() {
    if (SUCCEEDED(hr_)) CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;
  bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT hr_;
};

std::optional<std::filesystem::path> ShowDialogImpl(
    const wchar_t* title, bool pick_folder,
    std::span<const FileFilter> filters) {
  ComScope com;
  if (!com.ok()) return std::nullopt;

  ComPtr<IFileOpenDialog> dialog;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) {
    BD_ERROR("CoCreateInstance(FileOpenDialog) failed: 0x{:08X}",
             static_cast<uint32_t>(hr));
    return std::nullopt;
  }

  DWORD options = 0;
  dialog->GetOptions(&options);
  options |= FOS_FORCEFILESYSTEM;
  if (pick_folder) {
    options |= FOS_PICKFOLDERS;
  } else {
    options |= FOS_FILEMUSTEXIST;
  }
  dialog->SetOptions(options);
  dialog->SetTitle(title);

  std::vector<COMDLG_FILTERSPEC> spec;
  spec.reserve(filters.size());
  for (const auto& f : filters) spec.push_back({f.name, f.pattern});
  if (!spec.empty()) {
    dialog->SetFileTypes(static_cast<UINT>(spec.size()), spec.data());
  }

  hr = dialog->Show(nullptr);
  if (FAILED(hr)) return std::nullopt;  // user cancel is FAILED here

  ComPtr<IShellItem> result;
  if (FAILED(dialog->GetResult(&result))) return std::nullopt;

  PWSTR path_str = nullptr;
  if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path_str))) {
    return std::nullopt;
  }
  std::filesystem::path path(path_str);
  CoTaskMemFree(path_str);
  return path;
}

}  // namespace

std::optional<std::filesystem::path> ShowOpenFileDialog(
    const wchar_t* title, std::span<const FileFilter> filters) {
  return ShowDialogImpl(title, /*pick_folder=*/false, filters);
}

std::optional<std::filesystem::path> ShowOpenFolderDialog(const wchar_t* title) {
  return ShowDialogImpl(title, /*pick_folder=*/true, {});
}

}  // namespace reblue::ui

#else

namespace reblue::ui {
std::optional<std::filesystem::path> ShowOpenFileDialog(const wchar_t*,
                                                        std::span<const FileFilter>) {
  return std::nullopt;
}
std::optional<std::filesystem::path> ShowOpenFolderDialog(const wchar_t*) { return std::nullopt; }
}  // namespace reblue::ui

#endif
