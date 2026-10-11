/**
 * @file    platform/file_dialog.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/file_dialog.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <vector>

#include "core/encoding.h"
#include "core/logging.h"

namespace bd::platform {
namespace {

struct DialogResult {
  std::atomic<bool> done{false};
  std::vector<std::filesystem::path> paths;
  std::string error_message;
};

void SDLCALL DialogCallback(void *userdata, const char *const *filelist,
                            int /*filter*/) {
  auto *result = static_cast<DialogResult *>(userdata);
  if (!filelist) {
    // filelist == NULL indicates backend error (missing xdg-desktop-portal,
    // zenity, etc.), distinct from user cancel.
    result->error_message = SDL_GetError();
  } else {
    for (const char *const *it = filelist; *it != nullptr; ++it)
      result->paths.emplace_back(*it);
  }
  result->done.store(true, std::memory_order_release);
}

// SDL filter patterns are extension lists ("iso;zip"), not wildcards. Convert
// the Win32-style "*.iso;*.zip". "*.*" or "*" anywhere means all files.
std::string ToSdlPattern(const wchar_t *pattern) {
  const std::string src = bd::WideToUtf8(pattern);
  std::string out;
  size_t start = 0;
  while (start <= src.size()) {
    const size_t sep = src.find(';', start);
    std::string part = src.substr(
        start, sep == std::string::npos ? std::string::npos : sep - start);
    if (part == "*.*" || part == "*")
      return "*";
    if (part.rfind("*.", 0) == 0)
      part = part.substr(2);
    if (!part.empty()) {
      if (!out.empty())
        out += ';';
      out += part;
    }
    if (sep == std::string::npos)
      break;
    start = sep + 1;
  }
  return out.empty() ? "*" : out;
}

std::vector<std::filesystem::path>
RunDialog(const wchar_t *title, bool pick_folder, bool many,
          std::span<const FileFilter> filters) {
  DialogResult result;

  // Backing strings must outlive the dialog call, so build them fully before
  // taking c_str() pointers.
  const std::string title_utf8 = bd::WideToUtf8(title);
  std::vector<std::string> names;
  std::vector<std::string> patterns;
  names.reserve(filters.size());
  patterns.reserve(filters.size());
  for (const auto &f : filters) {
    names.push_back(bd::WideToUtf8(f.name));
    patterns.push_back(ToSdlPattern(f.pattern));
  }
  std::vector<SDL_DialogFileFilter> sdl_filters;
  sdl_filters.reserve(filters.size());
  for (size_t i = 0; i < filters.size(); ++i) {
    sdl_filters.push_back({names[i].c_str(), patterns[i].c_str()});
  }

  SDL_PropertiesID props = SDL_CreateProperties();
  if (props == 0) {
    BD_WARN("SDL_CreateProperties failed: {}", SDL_GetError());
    return {};
  }
  SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                        title_utf8.c_str());
  if (many) {
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, true);
  }
  if (!sdl_filters.empty()) {
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           sdl_filters.data());
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          static_cast<Sint64>(sdl_filters.size()));
  }
  SDL_ShowFileDialogWithProperties(pick_folder ? SDL_FILEDIALOG_OPENFOLDER
                                               : SDL_FILEDIALOG_OPENFILE,
                                   &DialogCallback, &result, props);
  SDL_DestroyProperties(props);

  // The dialog is asynchronous and its callback is delivered during event
  // processing, so pump until it completes and the blocking API contract holds.
  while (!result.done.load(std::memory_order_acquire)) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  if (!result.error_message.empty()) {
    BD_WARN("SDL file dialog failed: {}", result.error_message);
  }
  return std::move(result.paths);
}

} // namespace

std::optional<std::filesystem::path>
ShowOpenFileDialog(const wchar_t *title, std::span<const FileFilter> filters) {
  auto paths = RunDialog(title, /*pick_folder=*/false, /*many=*/false, filters);
  if (paths.empty())
    return std::nullopt;
  return std::move(paths.front());
}

std::vector<std::filesystem::path>
ShowOpenFilesDialog(const wchar_t *title, std::span<const FileFilter> filters) {
  return RunDialog(title, /*pick_folder=*/false, /*many=*/true, filters);
}

std::optional<std::filesystem::path>
ShowOpenFolderDialog(const wchar_t *title) {
  auto paths = RunDialog(title, /*pick_folder=*/true, /*many=*/false, {});
  if (paths.empty())
    return std::nullopt;
  return std::move(paths.front());
}

} // namespace bd::platform
