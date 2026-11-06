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
    for (auto args : {std::vector<std::string>{"reblue", "--prepare-content"},
                      {"reblue", "--recomplauncher-play"},
                      {"reblue", "--prepare-content", "--unexpected"},
                      {"reblue", "--recomplauncher-play", "--install-dir", "a", "--install-dir", "b"}})
      Check(Configure(args) == 2, "Invalid external arguments must return 2");
    std::vector<std::string> normal{"reblue", "--profile", "another-profile"};
    const auto original = normal;
    Check(!Configure(normal) && normal == original, "Native arguments were changed");
    const auto bytes = root.u8string();
    const std::string path(bytes.begin(), bytes.end());
    std::vector<std::string> missing{"reblue", "--recomplauncher-play", "--install-dir", path};
    Check(Configure(missing) == 1, "Missing preparation must return 1");
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
    Check(ExternalLaunch(), "External launch state not set");
    fs::remove_all(root);
    std::cout << "7 command-line integration cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored; fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n'; return 1;
  }
}
