/**
 * @file    installer/self_install.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/self_install.h"

#if defined(_WIN32)

#include <iterator>

#include <rex/filesystem.h>

#include "core/logging.h"
#include "core/program_files.h"

namespace bd::installer {

namespace fs = std::filesystem;

bool CopyProgramTo(const fs::path &install, std::string &error) {
  const fs::path here = rex::filesystem::GetExecutablePath().parent_path();
  std::error_code ec;
  if (!here.empty() && fs::equivalent(here, install, ec))
    return true;

  for (const char *rel : kProgramFiles) {
    const fs::path src = here / rel;
    const fs::path dst = install / rel;
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error = src.string() + " -> " + dst.string();
      return false;
    }
  }
  BD_INFO("Copied {} program files into {}", std::size(kProgramFiles),
          install.string());
  return true;
}

} // namespace bd::installer

#endif // defined(_WIN32)
