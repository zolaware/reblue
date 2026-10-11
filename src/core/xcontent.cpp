/**
 * @file    core/xcontent.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/xcontent.h"

#include <rex/filesystem.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

#include <algorithm>
#include <fstream>
#include <span>
#include <system_error>
#include <vector>

#include "core/logging.h"

namespace bd {
namespace {

constexpr size_t kChunkSize = 1 << 20;

template <typename Sink>
bool ReadChunks(rex::filesystem::Entry &entry, Sink &&sink) {
  rex::filesystem::File *file = nullptr;
  if (entry.Open(rex::filesystem::FileAccess::kFileReadData, &file) != 0 ||
      !file) {
    BD_ERROR("[xcontent] cannot open entry: {}", entry.path());
    return false;
  }
  std::vector<u8> buffer(
      std::min(kChunkSize, std::max<size_t>(entry.size(), 1)));
  size_t remaining = entry.size();
  size_t offset = 0;
  bool ok = true;
  while (remaining > 0) {
    size_t bytes_read = 0;
    const size_t to_read = std::min(remaining, buffer.size());
    file->ReadSync(std::span<u8>(buffer.data(), to_read), offset, &bytes_read);
    if (bytes_read != to_read || !sink(buffer.data(), bytes_read)) {
      ok = false;
      break;
    }
    offset += bytes_read;
    remaining -= bytes_read;
  }
  file->Destroy();
  return ok;
}

} // namespace

bool CopyEntry(rex::filesystem::Entry &entry,
               const std::filesystem::path &dest) {
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) {
    BD_ERROR("[xcontent] cannot write: {}", dest.string());
    return false;
  }
  const bool ok = ReadChunks(entry, [&](const u8 *data, size_t size) {
    out.write(reinterpret_cast<const char *>(data),
              static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
  });
  out.close();
  return ok && out.good();
}

std::string ReadEntry(rex::filesystem::Entry &entry) {
  std::string text;
  text.reserve(entry.size());
  const bool ok = ReadChunks(entry, [&](const u8 *data, size_t size) {
    text.append(reinterpret_cast<const char *>(data), size);
    return true;
  });
  return ok ? text : std::string();
}

} // namespace bd
