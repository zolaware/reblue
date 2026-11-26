// SPDX-License-Identifier: BSD-3-Clause
// Optional real-disc check: no graphics device, extraction or source writes.
#include "launcher/content_setup.h"
#include "launcher/recomplauncher.h"
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  try {
    bd::launcher::Sources input;
    for (size_t i = 0; i < input.size(); ++i) {
      const std::string path(argv[i + 1]);
      input[i] = std::filesystem::path(std::u8string(path.begin(), path.end()));
    }
    const auto sources = bd::launcher::ValidateDiscs(input, bd::launcher::InspectDiscSource);
    for (size_t i = 0; i < sources.size(); ++i)
      std::cout << "DVD " << i + 1 << ": supported Blue Dragon XEX and disc marker found\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
