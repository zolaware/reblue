/**
 * @file    core/xcontent.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/xcontent.h"

#include <rex/filesystem/file_system.h>
#include <rex/filesystem/stfs.h>

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
bool ReadChunks(const rex::filesystem::IContainerImage &container,
                const rex::filesystem::ContainerNode &node, Sink &&sink) {
  std::vector<u8> buffer(
      std::min(kChunkSize, std::max<size_t>(node.info.size, 1)));
  u64 offset = 0;
  while (offset < node.info.size) {
    const size_t to_read =
        static_cast<size_t>(std::min<u64>(node.info.size - offset, buffer.size()));
    auto read = container.Read(node, offset,
                               std::span<u8>(buffer.data(), to_read));
    if (!read || *read != to_read) {
      REXBD_ERROR("xcontent cannot read entry: {}", container.GetNodePath(node));
      return false;
    }
    if (!sink(buffer.data(), *read))
      return false;
    offset += *read;
  }
  return true;
}

} // namespace

bool CopyEntry(const rex::filesystem::IContainerImage &container,
               const rex::filesystem::ContainerNode &node,
               const std::filesystem::path &dest) {
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) {
    REXBD_ERROR("xcontent cannot write: {}", dest.string());
    return false;
  }
  const bool ok = ReadChunks(container, node, [&](const u8 *data, size_t size) {
    out.write(reinterpret_cast<const char *>(data),
              static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
  });
  out.close();
  return ok && out.good();
}

std::string ReadEntry(const rex::filesystem::IContainerImage &container,
                      const rex::filesystem::ContainerNode &node) {
  std::string text;
  text.reserve(node.info.size);
  const bool ok = ReadChunks(container, node, [&](const u8 *data, size_t size) {
    text.append(reinterpret_cast<const char *>(data), size);
    return true;
  });
  return ok ? text : std::string();
}

const rex::filesystem::ContainerNode *
FindEntry(const rex::filesystem::IContainerImage &container,
          const rex::filesystem::ContainerNode &root, std::string_view path) {
  const std::string prefix = container.GetNodePath(root);
  if (prefix.empty())
    return container.Find(path);
  return container.Find(prefix + "\\" + std::string(path));
}

bool IsDirectory(const rex::filesystem::ContainerNode &node) {
  return rex::HasFlags(node.info.attributes,
                       rex::filesystem::FileAttributes::kDirectory);
}

} // namespace bd
