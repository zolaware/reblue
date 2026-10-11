/**
 * @file    platform/appimage_update.cpp
 * @copyright Copyright (c) 2026 Rien Gupta <rgupta9@scu.edu>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/appimage_update.h"

#if !defined(_WIN32) && !defined(__APPLE__)

#include <array>
#include <fstream>
#include <optional>

#include <rex/types.h>

namespace bd::platform {
namespace {

namespace fs = std::filesystem;

constexpr size_t kELFHeaderBytes = 20;
constexpr size_t kELFClass = 4;
constexpr size_t kELFData = 5;
constexpr size_t kAppImageMagic = 8;
constexpr size_t kELFMachine = 18;

constexpr u8 kELFClass32 = 1;
constexpr u8 kELFClass64 = 2;
constexpr u8 kELFDataLittle = 1;
constexpr u8 kELFDataBig = 2;

struct AppImageHeader {
  u8 elf_class = 0;
  u8 byte_order = 0;
  u16 machine = 0;
};

fs::path WithSuffix(const fs::path &path, const char *suffix) {
  fs::path out = path;
  out += suffix;
  return out;
}

std::optional<AppImageHeader> ReadHeader(const fs::path &path,
                                         std::string &error) {
  std::error_code ec;
  if (fs::symlink_status(path, ec).type() != fs::file_type::regular) {
    error = ec ? "cannot inspect " + path.string() + ": " + ec.message()
               : path.string() + " is not a regular file";
    return std::nullopt;
  }

  std::ifstream file(path, std::ios::binary);
  std::array<u8, kELFHeaderBytes> bytes{};
  file.read(reinterpret_cast<char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
    error = "cannot read the AppImage header from " + path.string();
    return std::nullopt;
  }

  if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
      bytes[3] != 'F' || bytes[kAppImageMagic] != 'A' ||
      bytes[kAppImageMagic + 1] != 'I' || bytes[kAppImageMagic + 2] != 0x02) {
    error = path.string() + " is not a Type 2 AppImage";
    return std::nullopt;
  }

  const u8 elf_class = bytes[kELFClass];
  const u8 byte_order = bytes[kELFData];
  if ((elf_class != kELFClass32 && elf_class != kELFClass64) ||
      (byte_order != kELFDataLittle && byte_order != kELFDataBig)) {
    error = path.string() + " has an invalid ELF header";
    return std::nullopt;
  }

  const u8 lo = bytes[kELFMachine];
  const u8 hi = bytes[kELFMachine + 1];
  const u16 machine = byte_order == kELFDataLittle
                          ? static_cast<u16>(lo | (hi << 8))
                          : static_cast<u16>((lo << 8) | hi);
  if (machine == 0) {
    error = path.string() + " names no ELF architecture";
    return std::nullopt;
  }
  return AppImageHeader{elf_class, byte_order, machine};
}

bool SameArchitecture(const fs::path &current, const fs::path &incoming,
                      std::string &error) {
  const auto old_header = ReadHeader(current, error);
  if (!old_header)
    return false;
  const auto new_header = ReadHeader(incoming, error);
  if (!new_header)
    return false;
  if (old_header->elf_class != new_header->elf_class ||
      old_header->byte_order != new_header->byte_order ||
      old_header->machine != new_header->machine) {
    error = "the downloaded AppImage architecture does not match the running "
            "AppImage";
    return false;
  }
  return true;
}

} // namespace

bool IsType2AppImage(const fs::path &path) {
  std::string error;
  return ReadHeader(path, error).has_value();
}

bool ReplaceAppImage(const fs::path &current, const fs::path &incoming,
                     std::string &error) {
  error.clear();
  if (!SameArchitecture(current, incoming, error))
    return false;

  std::error_code ec;
  const fs::perms permissions = fs::status(current, ec).permissions();
  if (ec) {
    error = "cannot read AppImage permissions: " + ec.message();
    return false;
  }
  fs::permissions(incoming, permissions & fs::perms::all,
                  fs::perm_options::replace, ec);
  if (ec) {
    error = "cannot make the downloaded AppImage executable: " + ec.message();
    return false;
  }

  const fs::path replaced = WithSuffix(current, ".replaced");
  fs::remove(replaced, ec);
  if (ec) {
    error = "cannot remove the previous AppImage backup: " + ec.message();
    return false;
  }

  // A hard link keeps the old inode without a second AppImage's worth of disk.
  // FAT-like filesystems have no hard links, so those get the copy.
  fs::create_hard_link(current, replaced, ec);
  if (ec) {
    ec.clear();
    fs::copy_file(current, replaced, fs::copy_options::none, ec);
    if (ec) {
      error = "cannot retain the running AppImage: " + ec.message();
      return false;
    }
  }

  // The running process keeps its old inode mapped, so renaming over the
  // pathname is safe while it runs.
  fs::rename(incoming, current, ec);
  if (ec) {
    const std::string rename_error = ec.message();
    std::error_code drop;
    fs::remove(replaced, drop);
    error = "cannot replace the running AppImage: " + rename_error;
    return false;
  }
  return true;
}

bool ClearReplacedAppImage(const fs::path &current, std::string &error) {
  error.clear();
  std::error_code ec;
  fs::remove(WithSuffix(current, ".replaced"), ec);
  if (!ec)
    return true;
  error = "cannot remove the previous AppImage: " + ec.message();
  return false;
}

} // namespace bd::platform

#endif
