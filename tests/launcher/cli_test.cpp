// SPDX-License-Identifier: BSD-3-Clause
// Tests the real argument adapter in an installer-disabled build, without SDL.
#include "launcher/content_setup.h"
#include "launcher/recomplauncher.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bd::launcher;

void Check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  const auto root = fs::temp_directory_path() /
      ("reblue-cli-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    int cases = 0;
    for (auto args : {std::vector<std::string>{"reblue", "--prepare-content"},
                      {"reblue", "--recomplauncher-play"},
                      {"reblue", "--prepare-content", "--unexpected"},
                      {"reblue", "--recomplauncher-play", "--install-dir", "a", "--install-dir", "b"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "a", "--disc2", "b"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "a", "--disc2", "b", "--disc3"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "a", "--disc2", "b", "--disc3", "c", "--disc2", "d"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--game-dir", "folder", "--disc1", "a", "--disc2", "b", "--disc3", "c"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "", "--disc2", "b", "--disc3", "c"},
                      {"reblue", "--recomplauncher-play", "--install-dir", "out", "--disc1", "a"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc4", "a"},
                      {"reblue", "--prepare-content", "--disc1", "a", "--disc2", "b", "--disc3", "c"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "", "--disc1", "a", "--disc2", "b", "--disc3", "c"},
                      {"reblue", "--prepare-content", "--install-dir", "out", "--disc1", "--disc2", "b", "--disc3", "c"},
                      {"reblue", "--prepare-content", "--game-dir", "folder", "--install-dir", "out", "--dlc="},
                      {"reblue", "--prepare-content", "--game-dir", "folder", "--install-dir", "out", "--dlc", "pack"},
                      {"reblue", "--recomplauncher-play", "--install-dir", "out", "--dlc=pack"}}) {
      Check(Configure(args) == 2, "Invalid external arguments must return 2");
      ++cases;
    }
    std::vector<std::string> normal{"reblue", "--profile", "another-profile"};
    const auto original = normal;
    Check(!Configure(normal) && normal == original, "Native arguments were changed"); ++cases;
    const auto bytes = root.u8string();
    const std::string path(bytes.begin(), bytes.end());
    std::vector<std::string> legacy{"reblue", "--prepare-content", "--game-dir", path, "--install-dir", path + "/install"};
    Check(Configure(legacy) == 1, "Valid discovery arguments should reach installer-disabled error, not argument rejection"); ++cases;
    std::vector<std::string> named{"reblue", "--prepare-content", "--disc1", path + "/DVD 1 α.ISO",
        "--disc2", path + "/another/DVD 2.ISO", "--disc3", path + "/third/DVD 3.ISO", "--install-dir", path + "/install"};
    const auto named_original = named;
    Check(Configure(named) == 1 && named == named_original,
          "Valid named disc arguments should reach installer-disabled error unchanged"); ++cases;
    for (int count = 1; count <= 3; ++count) {
      named.push_back("--dlc=" + path + "/DLC package " + std::to_string(count) + " α");
      const auto with_dlc = named;
      Check(Configure(named) == 1 && named == with_dlc,
            "Optional DLC paths must reach the installer unchanged"); ++cases;
    }
    std::vector<std::string> missing{"reblue", "--recomplauncher-play", "--install-dir", path};
    Check(Configure(missing) == 1, "Missing preparation must return 1"); ++cases;
    const auto game = root / "game";
    fs::create_directories(game);
    std::ofstream(game / kOwner) << "reblue-recomplauncher-v1\n";
    std::ofstream(game / "default.xex") << "fixture";
    std::ofstream(game / "reblue_install.marker") << "installed";
    std::vector<std::string> play{"reblue", "--recomplauncher-play", "--install-dir", path};
    Check(!Configure(play), "Prepared play must continue native boot");
    const auto game_bytes = game.u8string();
    Check(play == std::vector<std::string>{"reblue", "--game_data_root=" +
               std::string(game_bytes.begin(), game_bytes.end())}, "Unexpected launch overrides");
    Check(ExternalLaunch(), "External launch state not set"); ++cases;
    fs::remove_all(root);
    std::cout << cases << " command-line integration cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored; fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n'; return 1;
  }
}
