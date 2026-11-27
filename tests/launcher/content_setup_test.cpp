// SPDX-License-Identifier: BSD-3-Clause
#include "launcher/content_setup.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "core/windows_lean.h"
#include <shellapi.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace bd::launcher;

void Check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void Write(const fs::path &path, const std::string &value = "fixture") {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << value;
}
template <typename F> void Fails(F fn, const std::string &message) {
  try { fn(); } catch (const std::exception &error) {
    Check(std::string(error.what()).find(message) != std::string::npos, error.what());
    return;
  }
  throw std::runtime_error("Expected failure: " + message);
}

struct Fixture {
  fs::path root = fs::temp_directory_path() /
      ("reblue-launcher-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::path images = root / fs::path(u8"Disc images Ελληνικά 日本語");
  fs::path install = root / "installation";
  std::vector<double> progress;
  bool owner = true;
  Fixture() {
    for (int n = 1; n <= 3; ++n)
      Write(images / ("arbitrary-name-" + std::to_string(n) + ".ISO"), std::to_string(n));
    Write(install / "profiles/default/saves/player.sav", "my save");
    Write(install / "profiles/default/reblue.toml", "my settings");
  }
  // Another process's fixture, left in place.
  explicit Fixture(const fs::path &existing)
      : root(existing), images(root / fs::path(u8"Disc images Ελληνικά 日本語")),
        install(root / "installation"), owner(false) {}
  ~Fixture() { if (owner) { std::error_code ignored; fs::remove_all(root, ignored); } }
  Inspector inspector = [](const fs::path &path) {
    char disc = 0; std::ifstream(path).get(disc);
    std::array<bool, 3> result{};
    if (disc >= '1' && disc <= '3') result[disc - '1'] = true;
    return result;
  };
  Extractor extractor = [](const Sources &, const fs::path &game, const Progress &progress) {
    for (const char *name : {"default.xex", "bd_boot.ini", "bd_disc_1.xml", "bd_disc_2.xml",
                             "bd_disc_3.xml", "reblue_install.marker"})
      Write(game / name);
    progress(0.5, "Extracting");
  };
  Sources Discs() const {
    Sources discs;
    for (size_t i = 0; i < discs.size(); ++i)
      discs[i] = images / ("arbitrary-name-" + std::to_string(i + 1) + ".ISO");
    return discs;
  }
  void PrepareNow() {
    Prepare(Discs(), install, inspector, extractor,
            [&](double value, const std::string &) { progress.push_back(value); });
  }
};

// Setup in this process that writes part of the game, then waits to be killed.
[[noreturn]] void RunStalledSetup(const fs::path &root) {
  try {
    Fixture f(root);
    const auto extract = f.extractor;
    f.extractor = [&](const Sources &sources, const fs::path &game, const Progress &progress) {
      extract(sources, game, progress);
      Write(game / "stalled");
      for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    };
    f.PrepareNow();
  } catch (const std::exception &error) {
    std::cerr << "Stalled setup failed: " << error.what() << '\n';
  }
  std::_Exit(3);
}

#ifdef _WIN32
using Child = HANDLE;
Child StartStalledSetup(const fs::path &root) {
  std::wstring self(32768, L'\0');
  self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
  std::wstring command = L"\"" + self + L"\" --stalled-setup \"" + root.wstring() + L"\"";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &process))
    throw std::runtime_error("Cannot start the stalled setup");
  CloseHandle(process.hThread);
  return process.hProcess;
}
void Kill(Child child) {
  TerminateProcess(child, 9);
  WaitForSingleObject(child, INFINITE);
  CloseHandle(child);
}
#else
using Child = pid_t;
Child StartStalledSetup(const fs::path &root) {
  std::cout.flush();
  const pid_t pid = fork();
  if (pid < 0)
    throw std::runtime_error("Cannot start the stalled setup");
  if (pid == 0)
    RunStalledSetup(root);
  return pid;
}
void Kill(Child child) {
  kill(child, SIGKILL);
  int status = 0;
  waitpid(child, &status, 0);
}
#endif

int main() {
#ifdef _WIN32
  // The fixture path can hold any character, so read the wide command line.
  int count = 0;
  if (const auto args = CommandLineToArgvW(GetCommandLineW(), &count); args && count == 3 &&
      std::wstring_view(args[1]) == L"--stalled-setup")
    RunStalledSetup(args[2]);
#endif
  int passed = 0;
  auto test = [&](const char *name, auto fn) {
    fn(); ++passed; std::cout << "PASS " << name << '\n';
  };
  try {
    test("no DLC selection requires no package validation", [] {
      Fixture f;
      Check(ValidateDLC({}, f.install, [](const fs::path &) -> std::string {
        throw std::runtime_error("Optional selection invoked validator");
      }).empty(), "Empty optional selection changed");
    });
    test("DLC sources keep Unicode paths and native validator names", [] {
      Fixture f; std::vector<fs::path> paths;
      for (int i = 1; i <= 3; ++i) {
        paths.push_back(f.images / fs::path(u8"DLC α") / std::to_string(i));
        Write(paths.back(), std::to_string(i));
      }
      const auto packs = ValidateDLC(paths, f.install, [](const fs::path &path) {
        std::string name; std::ifstream(path) >> name; return name;
      });
      Check(packs.size() == 3, "Missing DLC selection");
      for (size_t i = 0; i < packs.size(); ++i) {
        Check(packs[i].path == fs::canonical(paths[i]), "DLC source path changed");
        Check(packs[i].name == std::to_string(i + 1), "Native validation result lost");
        Check(fs::exists(paths[i]), "DLC source removed");
      }
      Check(!fs::exists(f.install / "dlc"), "Validation modified DLC destination");
    });
    test("missing DLC rejected before native validation", [] {
      Fixture f;
      Fails([&] { ValidateDLC({f.images / "missing"}, f.install, [](const fs::path &) {
        throw std::runtime_error("Validator must not run"); return std::string{};
      }); }, "not found");
    });
    test("DLC directory rejected", [] {
      Fixture f;
      Fails([&] { ValidateDLC({f.images}, f.install, [](const fs::path &) { return "pack"; }); }, "not found");
    });
    test("native DLC rejection preserves existing game and settings", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      Fails([&] { ValidateDLC({f.images / "arbitrary-name-1.ISO"}, f.install,
        [](const fs::path &) -> std::string { throw std::runtime_error("Wrong title or unsupported DLC"); }); }, "unsupported DLC");
      Check(fs::file_size(f.install / "game/keep") == 8, "Existing game changed");
      Check(fs::file_size(f.install / "profiles/default/reblue.toml") == 11, "Settings changed");
    });
    test("duplicate DLC source rejected", [] {
      Fixture f; const auto path = f.images / "arbitrary-name-1.ISO";
      Fails([&] { ValidateDLC({path, path}, f.install, [](const fs::path &) { return "pack"; }); }, "more than once");
    });
    test("different copies of the same DLC pack rejected", [] {
      Fixture f;
      Fails([&] { ValidateDLC({f.images / "arbitrary-name-1.ISO", f.images / "arbitrary-name-2.ISO"},
        f.install, [](const fs::path &) { return "same pack"; }); }, "more than once");
    });
    test("DLC source and installation overlap rejected", [] {
      Fixture f; const auto path = f.install / "source/package"; Write(path);
      Fails([&] { ValidateDLC({path}, f.install, [](const fs::path &) { return "pack"; }); }, "separate");
      Check(fs::exists(path), "Overlapping source removed");
    });
    test("explicit discs from separate folders are validated in slot order without sibling discovery", [] {
      Fixture f; Sources input{};
      for (size_t i = 0; i < input.size(); ++i) {
        input[i] = f.root / ("source " + std::to_string(i + 1)) / fs::path(u8"日本語 backup.ISO");
        Write(input[i], std::to_string(i + 1));
        Write(input[i].parent_path() / "unselected-duplicate.iso", std::to_string(i + 1));
      }
      const auto sources = ValidateDiscs(input, f.inspector);
      for (size_t i = 0; i < input.size(); ++i)
        Check(sources[i] == fs::canonical(input[i]), "Explicit slot changed");
    });
    test("explicit slots reject the wrong disc even when the correct sibling exists", [] {
      Fixture f; Sources input{f.images / "arbitrary-name-2.ISO", f.images / "arbitrary-name-1.ISO",
                               f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "DVD 1");
    });
    test("explicit slots reject the same file twice", [] {
      Fixture f; Sources input{f.images / "arbitrary-name-1.ISO", f.images / "arbitrary-name-1.ISO",
                               f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "same image");
    });
    test("explicit slots reject hard-linked aliases", [] {
      Fixture f; const auto alias = f.root / "alias.ISO";
      fs::create_hard_link(f.images / "arbitrary-name-1.ISO", alias);
      Sources input{f.images / "arbitrary-name-1.ISO", alias, f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "same image");
    });
    test("explicit slots require all three paths", [] {
      Fixture f; Sources input{f.images / "arbitrary-name-1.ISO", f.images / "arbitrary-name-2.ISO", {}};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "DVD 3");
    });
    test("explicit slots reject missing files", [] {
      Fixture f; Sources input{f.root / "missing.ISO", f.images / "arbitrary-name-2.ISO",
                               f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "DVD 1");
    });
    test("explicit slots reject directories", [] {
      Fixture f; Sources input{f.images, f.images / "arbitrary-name-2.ISO", f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "ISO file");
    });
    test("explicit slots reject another game", [] {
      Fixture f; Write(f.root / "another.ISO", "x");
      Sources input{f.root / "another.ISO", f.images / "arbitrary-name-2.ISO", f.images / "arbitrary-name-3.ISO"};
      Fails([&] { ValidateDiscs(input, f.inspector); }, "DVD 1");
    });
    test("explicit slots reject ambiguous disc markers", [] {
      Fixture f; Sources input{f.images / "arbitrary-name-1.ISO", f.images / "arbitrary-name-2.ISO",
                               f.images / "arbitrary-name-3.ISO"};
      Inspector ambiguous = [](const fs::path &) { return std::array<bool, 3>{true, true, false}; };
      Fails([&] { ValidateDiscs(input, ambiguous); }, "only Blue Dragon DVD 1");
    });
    test("explicit preparation passes the selected discs and preserves saves and settings", [] {
      Fixture f; Sources input{};
      for (size_t i = 0; i < input.size(); ++i) {
        input[i] = f.root / ("DVD " + std::to_string(i + 1)) / "backup.ISO";
        Write(input[i], std::to_string(i + 1));
      }
      const auto extract = f.extractor;
      Extractor checked = [&](const Sources &sources, const fs::path &game, const Progress &progress) {
        for (size_t i = 0; i < input.size(); ++i)
          Check(sources[i] == fs::canonical(input[i]), "Wrong explicit disc passed to installer");
        extract(sources, game, progress);
      };
      Prepare(input, f.install, f.inspector, checked, [&](double v, const std::string &) { f.progress.push_back(v); });
      Check(Owned(f.install / "game"), "Named setup did not publish");
      Check(fs::file_size(f.install / "profiles/default/saves/player.sav") == 7, "Save changed");
      Check(fs::file_size(f.install / "profiles/default/reblue.toml") == 11, "Settings changed");
      for (const auto &disc : input) Check(fs::file_size(disc) == 1, "Source changed");
      Check(f.progress.front() == 0 && f.progress.back() == 1, "Progress incomplete");
    });
    test("explicit preparation failure retains the previous managed installation", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      Sources input{f.images / "arbitrary-name-1.ISO", f.images / "arbitrary-name-2.ISO",
                    f.images / "arbitrary-name-3.ISO"};
      Extractor fail = [](const Sources &, const fs::path &game, const Progress &) {
        Write(game / "partial"); throw std::runtime_error("injected named failure");
      };
      Fails([&] { Prepare(input, f.install, f.inspector, fail, [](double, const std::string &) {}); }, "named failure");
      Check(fs::file_size(f.install / "game/keep") == 8, "Previous game changed");
      Check(!fs::exists(f.install / ".reblue-preparing"), "Failed staging remains");
    });
    test("explicit preparation rejects overlap with any disc folder", [] {
      Fixture f; Sources input{};
      for (size_t i = 0; i < input.size(); ++i) {
        input[i] = f.root / ("source " + std::to_string(i + 1)) / "backup.ISO";
        Write(input[i], std::to_string(i + 1));
      }
      f.install = input[1].parent_path() / "installation";
      Fails([&] { Prepare(input, f.install, f.inspector, f.extractor, [](double, const std::string &) {}); }, "separate");
      Check(!fs::exists(f.install), "Disc folder changed");
    });
    test("missing disc rejected", [] {
      Fixture f; fs::remove(f.images / "arbitrary-name-2.ISO");
      Fails([&] { f.PrepareNow(); }, "DVD 2");
      Check(!fs::exists(f.install / "game"), "Incomplete game published");
    });
    test("preparation publishes and preserves saves, preferences and sources", [] {
      Fixture f; f.PrepareNow();
      Check(Owned(f.install / "game"), "Missing owner");
      Check(PreparedGame(f.install) == f.install / "game", "Wrong game path");
      Check(fs::file_size(f.images / "arbitrary-name-1.ISO") == 1, "Source changed");
      Check(fs::file_size(f.install / "profiles/default/saves/player.sav") == 7, "Save changed");
      Check(fs::file_size(f.install / "profiles/default/reblue.toml") == 11, "Settings changed");
      Check(f.progress.front() == 0 && f.progress.back() == 1, "Progress incomplete");
      Check(!fs::exists(f.install / ".reblue-preparing"), "Staging remains");
    });
    test("owned game can be replaced", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/old.bin"); f.PrepareNow();
      Check(!fs::exists(f.install / "game/old.bin"), "Stale files remain");
    });
    test("failed extraction keeps the previous preparation", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      f.extractor = [](const Sources &, const fs::path &game, const Progress &) {
        Write(game / "partial"); throw std::runtime_error("injected copy failure");
      };
      Fails([&] { f.PrepareNow(); }, "injected copy failure");
      Check(fs::file_size(f.install / "game/keep") == 8, "Previous game changed");
      Check(!fs::exists(f.install / ".reblue-preparing"), "Failed staging remains");
    });
    test("missing required output is never published", [] {
      Fixture f;
      f.extractor = [](const Sources &, const fs::path &game, const Progress &) { Write(game / "default.xex"); };
      Fails([&] { f.PrepareNow(); }, "bd_boot.ini");
      Check(!fs::exists(f.install / "game"), "Partial output published");
    });
    test("unowned installation is preserved", [] {
      Fixture f; Write(f.install / "game/keep");
      Fails([&] { f.PrepareNow(); }, "not managed");
      Check(fs::exists(f.install / "game/keep"), "Unowned file removed");
    });
    test("a leftover setup is removed and the next setup carries on", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      Write(f.install / ".reblue-preparing/new/game/partial");
      RecoverSetup(f.install);
      Check(!fs::exists(f.install / ".reblue-preparing"), "Leftover setup remains");
      Check(fs::file_size(f.install / "game/keep") == 8, "Published game changed");
      Write(f.install / ".reblue-preparing/new/game/partial"); f.PrepareNow();
      Check(IsPrepared(f.install) && !fs::exists(f.install / ".reblue-preparing"), "Setup did not carry on");
    });
    test("a game moved aside by a stopped setup is put back", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      fs::create_directories(f.install / ".reblue-preparing/new/game");
      fs::rename(f.install / "game", f.install / ".reblue-preparing/old");
      RecoverSetup(f.install);
      Check(fs::file_size(f.install / "game/keep") == 8 && IsPrepared(f.install), "Previous game not put back");
      Check(!fs::exists(f.install / ".reblue-preparing"), "Leftover setup remains");
      // Prepare recovers too: a failed extraction afterwards keeps the old game.
      fs::create_directories(f.install / ".reblue-preparing");
      fs::rename(f.install / "game", f.install / ".reblue-preparing/old");
      f.extractor = [](const Sources &, const fs::path &, const Progress &) {
        throw std::runtime_error("injected copy failure");
      };
      Fails([&] { f.PrepareNow(); }, "injected copy failure");
      Check(fs::file_size(f.install / "game/keep") == 8, "Previous game lost");
    });
    test("a moved-aside game is dropped once the new game was published", [] {
      Fixture f; f.PrepareNow();
      Write(f.install / ".reblue-preparing/old/stale");
      RecoverSetup(f.install);
      Check(IsPrepared(f.install) && !fs::exists(f.install / ".reblue-preparing"), "Published game not kept");
    });
    test("setup killed mid-extraction runs again", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      auto child = StartStalledSetup(f.root);
      const auto stalled = f.install / ".reblue-preparing/new/game/stalled";
      for (int i = 0; i < 600 && !fs::exists(stalled); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!fs::exists(stalled)) { Kill(child); throw std::runtime_error("Stalled setup never started"); }
      // While it runs, its transaction is never touched.
      Fails([&] { f.PrepareNow(); }, "running");
      Fails([&] { RecoverSetup(f.install); }, "running");
      Check(fs::exists(stalled), "Running setup disturbed");
      Kill(child);
      Check(fs::exists(stalled), "Kill left no unfinished setup behind");
      f.PrepareNow();
      Check(IsPrepared(f.install), "Setup after the kill did not publish");
      Check(!fs::exists(f.install / "game/keep"), "Setup after the kill did not replace the game");
      Check(!fs::exists(f.install / ".reblue-preparing"), "Killed setup remains");
      Check(fs::file_size(f.install / "profiles/default/saves/player.sav") == 7, "Save changed");
    });
    test("source and destination overlap rejected", [] {
      Fixture f; f.install = f.images / "nested";
      Fails([&] { f.PrepareNow(); }, "separate");
      Check(!fs::exists(f.install), "Source tree changed");
    });
    test("source nested in install rejected", [] {
      Fixture f; f.install = f.root;
      Fails([&] { f.PrepareNow(); }, "separate");
    });
    test("direct launch requires preparation", [] {
      Fixture f; Fails([&] { PreparedGame(f.install); }, "Run content preparation");
    });
    test("direct launch rejects unowned content", [] {
      Fixture f; Write(f.install / "game/default.xex");
      Fails([&] { PreparedGame(f.install); }, "not managed");
    });
    test("removed prepared executable rejected", [] {
      Fixture f; f.PrepareNow(); fs::remove(f.install / "game/default.xex");
      Fails([&] { PreparedGame(f.install); }, "Run content preparation");
      Check(!IsPrepared(f.install), "Removed executable still counts as set up");
    });
    test("only a published managed build counts as set up", [] {
      Fixture f; Check(!IsPrepared(f.install), "Empty install counts as set up");
      Write(f.install / "game/default.xex"); Check(!IsPrepared(f.install), "Unowned content counts as set up");
    });
    test("a published build counts as set up", [] {
      Fixture f; f.PrepareNow(); Check(IsPrepared(f.install), "Published build not recognised");
      Check(Current(f.install / "game"), "Published build lacks the content version");
    });
    test("a build of another content version is not set up but is replaced", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/old.bin");
      Write(f.install / "game" / kOwner, "reblue-content-v0\n");
      Check(Owned(f.install / "game") && !Current(f.install / "game"), "Older version not recognised as ours");
      Check(!IsPrepared(f.install), "Older content version counts as set up");
      Fails([&] { PreparedGame(f.install); }, "set up again");
      f.PrepareNow();
      Check(IsPrepared(f.install) && !fs::exists(f.install / "game/old.bin"), "Older build not replaced");
    });
    test("a marker without a content version is not set up", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game" / kOwner, "");
      Check(!IsPrepared(f.install), "Marker without a version counts as set up");
    });
#ifndef _WIN32
    test("publication failure restores the previous game directory", [] {
      Fixture f; f.PrepareNow(); Write(f.install / "game/keep", "previous");
      const auto extract = f.extractor;
      f.extractor = [&](const Sources &sources, const fs::path &game, const Progress &progress) {
        extract(sources, game, progress);
        // Moving the old game remains possible, but publishing the staged
        // game fails because its source parent cannot be modified.
        fs::permissions(game.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
      };
      Fails([&] { f.PrepareNow(); }, "");
      fs::permissions(f.install / ".reblue-preparing/new", fs::perms::owner_all);
      Check(fs::file_size(f.install / "game/keep") == 8, "Previous game not restored");
      Check(Owned(f.install / "game"), "Previous ownership lost");
    });
    test("symlinked destination rejected", [] {
      Fixture f; fs::create_directories(f.root / "other");
      fs::create_directory_symlink(f.root / "other", f.install / "game");
      Fails([&] { f.PrepareNow(); }, "not managed");
      Check(fs::is_empty(f.root / "other"), "Linked target changed");
    });
#endif
    std::cout << passed << " integration tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
  }
}
