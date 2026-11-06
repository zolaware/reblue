// SPDX-License-Identifier: BSD-3-Clause
#include "launcher/content_setup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

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
  Fixture() {
    for (int n = 1; n <= 3; ++n)
      Write(images / ("arbitrary-name-" + std::to_string(n) + ".ISO"), std::to_string(n));
    Write(install / "profiles/default/saves/player.sav", "my save");
    Write(install / "profiles/default/reblue.toml", "my settings");
  }
  ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
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
  void PrepareNow() {
    Prepare(images, install, inspector, extractor,
            [&](double value, const std::string &) { progress.push_back(value); });
  }
};

int main() {
  int passed = 0;
  auto test = [&](const char *name, auto fn) {
    fn(); ++passed; std::cout << "PASS " << name << '\n';
  };
  try {
    test("three discs discovered by contents, with unicode and uppercase ISO names", [] {
      Fixture f; auto discs = FindDiscs(f.images, f.inspector);
      Check(discs[0].filename() == "arbitrary-name-1.ISO", "DVD1 not detected");
      Check(discs[2].filename() == "arbitrary-name-3.ISO", "DVD3 not detected");
    });
    test("a selected ISO resolves its sibling discs", [] {
      Fixture f; auto discs = FindDiscs(f.images / "arbitrary-name-1.ISO", f.inspector);
      Check(!discs[2].empty(), "DVD3 not found");
    });
    test("missing disc rejected", [] {
      Fixture f; fs::remove(f.images / "arbitrary-name-2.ISO");
      Fails([&] { f.PrepareNow(); }, "DVD 2");
      Check(!fs::exists(f.install / "game"), "Incomplete game published");
    });
    test("duplicate disc rejected", [] {
      Fixture f; Write(f.images / "duplicate.iso", "1");
      Fails([&] { f.PrepareNow(); }, "More than one");
    });
    test("unrelated ISO ignored", [] {
      Fixture f; Write(f.images / "unrelated.iso", "x"); f.PrepareNow();
    });
    test("preparation publishes and preserves saves, preferences and sources", [] {
      Fixture f; f.PrepareNow();
      Check(Owned(f.install / "game"), "Missing owner");
      Check(PreparedGame(f.install) == f.install / "game", "Wrong game path");
      Check(fs::file_size(f.images / "arbitrary-name-1.ISO") == 1, "Source changed");
      Check(fs::file_size(f.install / "profiles/default/saves/player.sav") == 7, "Save changed");
      Check(fs::file_size(f.install / "profiles/default/reblue.toml") == 11, "Settings changed");
      Check(f.progress.front() == 0 && f.progress.back() == 1, "Progress incomplete");
      Check(!fs::exists(f.install / ".recomplauncher-preparing"), "Staging remains");
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
      Check(!fs::exists(f.install / ".recomplauncher-preparing"), "Failed staging remains");
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
    test("interrupted staging is preserved", [] {
      Fixture f; Write(f.install / ".recomplauncher-preparing/recovery");
      Fails([&] { f.PrepareNow(); }, "interrupted");
      Check(fs::exists(f.install / ".recomplauncher-preparing/recovery"), "Recovery removed");
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
      fs::permissions(f.install / ".recomplauncher-preparing/new", fs::perms::owner_all);
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
