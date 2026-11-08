// SPDX-License-Identifier: BSD-3-Clause
#include "launcher/recomplauncher.h"
#include "launcher/content_setup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifdef REBLUE_BUILD_INSTALLER
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/filesystem/entry.h>

#include "core/xcontent.h"
#include "installer/disc_install.h"
#include "platform/reboot.h"
#include "vfs/dlc_catalog.h"
#endif

#ifdef _WIN32
#include "core/windows_lean.h"
#include <fcntl.h>
#include <io.h>
#endif

namespace bd::launcher {
namespace {
bool external_launch = false;

std::string Utf8(const fs::path &path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

fs::path FromUtf8(const std::string &value) {
  return fs::path(std::u8string(value.begin(), value.end()));
}

void Report(double amount, const std::string &message) {
  std::cout << "progress " << amount << ' ' << message << std::endl;
}

#ifdef REBLUE_BUILD_INSTALLER
void Extract(const Sources &sources, const fs::path &destination,
             const Progress &progress) {
  installer::InstallProgress state;
  installer::InstallSelection selection; // All available languages and movies.
  auto worker = installer::Installer::RunAsync(sources, destination, false,
                                              selection, state, true);
  try {
    size_t previous = 0;
    while (!state.complete.load()) {
      const auto total = state.bytes_total.load();
      const auto done = state.bytes_done.load();
      if (total && done != previous) {
        progress(0.05 + 0.90 * std::min(1.0, static_cast<double>(done) / total),
                 "Copying Blue Dragon game files");
        previous = done;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (...) {
    state.canceled.store(true);
    worker.join();
    throw;
  }
  worker.join();
  if (state.failed.load() || state.canceled.load())
    throw std::runtime_error(state.GetError().empty() ? "Game extraction failed." : state.GetError());
}
#endif
} // namespace

bool ExternalLaunch() { return external_launch; }


void AttachStandardStreams() {
#ifdef _WIN32
  AttachConsole(ATTACH_PARENT_PROCESS);
  for (auto pair : {std::pair{STD_OUTPUT_HANDLE, stdout}, std::pair{STD_ERROR_HANDLE, stderr}}) {
    const auto handle = GetStdHandle(pair.first);
    HANDLE duplicate = nullptr;
    if (handle && handle != INVALID_HANDLE_VALUE &&
        DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(),
                        &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(duplicate), _O_TEXT);
      if (fd >= 0) {
        _dup2(fd, _fileno(pair.second));
        _close(fd);
      } else {
        CloseHandle(duplicate);
      }
    }
  }
#endif
}

std::optional<int> Configure(std::vector<std::string> &args) {
  if (args.size() < 2 || (args[1] != "--prepare-content" && args[1] != "--recomplauncher-play"))
    return std::nullopt;
  AttachStandardStreams();
  const bool prepare = args[1] == "--prepare-content";
  fs::path source, install;
  Sources discs{};
  std::vector<fs::path> dlc;
  try {
    for (size_t i = 2; i < args.size(); ++i) {
      const auto &flag = args[i];
      const bool path_flag = flag == "--install-dir" || (prepare &&
          (flag == "--game-dir" || flag == "--disc1" || flag == "--disc2" || flag == "--disc3"));
      if (path_flag && (i + 1 == args.size() || args[i + 1].empty() || args[i + 1].starts_with("--"))) {
        std::cerr << "Missing path for: " << flag << std::endl;
        return 2;
      }
      if (prepare && flag.starts_with("--dlc=") && flag.size() > 6)
        dlc.push_back(FromUtf8(flag.substr(6)));
      else if (flag == "--install-dir" && install.empty() && i + 1 < args.size())
        install = FromUtf8(args[++i]);
      else if (prepare && flag == "--game-dir" && source.empty() && i + 1 < args.size())
        source = FromUtf8(args[++i]);
      else if (prepare && (flag == "--disc1" || flag == "--disc2" || flag == "--disc3") &&
               discs[flag.back() - '1'].empty() && i + 1 < args.size())
        discs[flag.back() - '1'] = FromUtf8(args[++i]);
      else {
        std::cerr << "Invalid or duplicate argument: " << flag << std::endl;
        return 2;
      }
    }
    const bool any_disc = std::any_of(discs.begin(), discs.end(), [](const auto &p) { return !p.empty(); });
    const bool all_discs = std::all_of(discs.begin(), discs.end(), [](const auto &p) { return !p.empty(); });
    if (install.empty() || (prepare && ((source.empty() && !all_discs) || (!source.empty() && any_disc)))) {
      std::cerr << "Expected --install-dir <folder>" << (prepare ?
          " and either --game-dir <image or folder> or all of --disc1, --disc2 and --disc3 <ISO>." : ".") << std::endl;
      return 2;
    }
    if (prepare) {
#ifdef REBLUE_BUILD_INSTALLER
      // Initialize only logging/cvar defaults, never SDL, saved settings or GPU.
      char name[] = "reblue";
      char *argv[] = {name};
      rex::cvar::Init(1, argv);
      rex::InitLoggingEarly();
      if (!platform::AcquireInstanceLock())
        throw std::runtime_error("re:Blue is already running. Close it before preparing content.");
      // Validate every selected DLC before the disc transaction changes anything.
      // Keep package sources outside the managed installation, as with disc ISOs.
      const auto packages = ValidateDLC(dlc, install, [](const fs::path &path) {
        const auto validation = vfs::DLCCatalog::Validate(path);
        if (!validation.ok)
          throw std::runtime_error("Invalid DLC '" + Utf8(path) + "': " + validation.error);
        return validation.display_name;
      });
      const auto progress = [&](double amount, const std::string &message) {
        Report(dlc.empty() ? amount : amount * 0.9,
               !dlc.empty() && amount == 1 ? "Game files ready" : message);
      };
      if (any_disc)
        Prepare(discs, install, InspectDiscSource, Extract, progress);
      else
        Prepare(source, install, InspectDiscSource, Extract, progress);
      if (!dlc.empty()) {
        // A local catalog avoids initializing VFS or binding profile enable state.
        vfs::DLCCatalog catalog;
        catalog.Init(fs::absolute(install) / "dlc");
        catalog.Reload();
        for (size_t i = 0; i < dlc.size(); ++i) {
          bool installed = false;
          for (size_t j = 0; j < catalog.Count(); ++j) {
            const auto &pack = catalog.At(j);
            installed |= pack.display_name == packages[i].name &&
                fs::is_regular_file(pack.download_dir / "DL_Param.ipk") &&
                fs::is_regular_file(pack.root.parent_path() / ".headers" / (pack.name + ".header"));
          }
          Report(0.9 + 0.1 * i / dlc.size(), "Installing DLC: " + packages[i].name);
          if (!installed && !catalog.Install(packages[i].path))
            throw std::runtime_error("Could not install DLC: " + packages[i].name +
                                     ". Game files and previously installed packs were kept; retry setup.");
        }
        Report(1, "Ready to play");
      }
      return 0;
#else
      throw std::runtime_error("This build does not include the disc installer.");
#endif
    }
    const auto game = PreparedGame(install);
    args = {args[0], "--game_data_root=" + Utf8(game)};
    external_launch = true;
    return std::nullopt;
  } catch (const std::exception &error) {
    if (prepare) {
      // The setup host consumes progress lines and may discard stderr.
      std::string message = error.what();
      std::replace(message.begin(), message.end(), '\n', ' ');
      std::replace(message.begin(), message.end(), '\r', ' ');
      Report(0, "Error: " + message);
    }
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
} // namespace bd::launcher
