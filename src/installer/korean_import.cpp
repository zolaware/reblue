/**
 * @file    installer/korean_import.cpp
 * @brief   Korean retail data import without replacing the NTSC-U executable.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            Additional Korean retail import work (c) 2026 dj5927.
 * @license   BSD 3-Clause License
 */
#include "installer/korean_import.h"

#include <rex/filesystem/entry.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/memory/mapped_memory.h>
#include <rex/types.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "core/logging.h"
#include "installer/disc_install.h"
#include "vfs/file_system.h"
#include "vfs/mounts.h"

namespace bd::installer {
namespace fs = std::filesystem;
namespace {

constexpr size_t kSector = 0x800;
constexpr size_t kPackHeader = 12;
constexpr size_t kBody = kSector - kPackHeader;
constexpr std::array<u8, 4> kPackStart = {0x00, 0x00, 0x01, 0xBA};
constexpr std::array<u8, 4> kEndStart = {0x00, 0x00, 0x01, 0xB9};
constexpr std::array<u8, 4> kSystemStart = {0x00, 0x00, 0x01, 0xBB};
constexpr std::array<u8, 4> kPaddingStart = {0x00, 0x00, 0x01, 0xBE};
constexpr std::string_view kSofdecLabel = "SofdecStream";
constexpr u8 kC1 = 0xC1;
constexpr u8 kC2 = 0xC2;
constexpr u8 kC3 = 0xC3;
constexpr u8 kVideo = 0xE0;

using Sector = std::array<u8, kSector>;
using Body = std::array<u8, kBody>;

struct DiscFile {
  std::string relative;
  rex::filesystem::Entry *entry = nullptr;
};

bool HasKorean(rex::filesystem::DiscImageDevice &disc) {
  const auto langs = ParseDiscLanguages(disc);
  return langs.ui.contains("kr");
}

std::vector<u8> ReadPrefix(rex::filesystem::Entry &entry, size_t wanted) {
  auto *disc_entry = dynamic_cast<rex::filesystem::DiscImageEntry *>(&entry);
  if (!disc_entry)
    return {};
  const size_t count = std::min(wanted, entry.size());
  auto mapped = disc_entry->OpenMapped(rex::memory::MappedMemory::Mode::kRead,
                                       0, count);
  if (!mapped)
    return {};
  std::vector<u8> out(count);
  std::memcpy(out.data(), mapped->data(), count);
  return out;
}

bool CopyEntry(rex::filesystem::Entry &entry, const fs::path &dest) {
  auto *disc_entry = dynamic_cast<rex::filesystem::DiscImageEntry *>(&entry);
  if (!disc_entry)
    return false;
  auto mapped = disc_entry->OpenMapped(rex::memory::MappedMemory::Mode::kRead,
                                       0, 0);
  if (!mapped)
    return false;
  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);
  if (ec)
    return false;
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char *>(mapped->data()),
            static_cast<std::streamsize>(entry.size()));
  return static_cast<bool>(out);
}

std::optional<size_t> FindLabel(std::span<const u8> bytes) {
  const auto it = std::search(bytes.begin(), bytes.end(), kSofdecLabel.begin(),
                              kSofdecLabel.end());
  if (it == bytes.end())
    return std::nullopt;
  return static_cast<size_t>(it - bytes.begin());
}

int EntrySfdAudioCount(rex::filesystem::Entry &entry) {
  const auto head = ReadPrefix(entry, kSector * 4);
  const auto label = FindLabel(head);
  if (!label || *label + 0x91 >= head.size())
    return -1;
  return head[*label + 0x91];
}

bool ReadSector(std::ifstream &in, Sector &sector) {
  in.read(reinterpret_cast<char *>(sector.data()), sector.size());
  return static_cast<size_t>(in.gcount()) == sector.size();
}

bool Starts(std::span<const u8> bytes, size_t at,
            const std::array<u8, 4> &magic) {
  return at + magic.size() <= bytes.size() &&
         std::equal(magic.begin(), magic.end(), bytes.begin() + at);
}

std::optional<u8> StreamId(const Sector &sector) {
  if (!Starts(sector, 0, kPackStart) || sector[12] != 0x00 ||
      sector[13] != 0x00 || sector[14] != 0x01)
    return std::nullopt;
  return sector[15];
}

bool IsEnd(const Sector &sector) { return Starts(sector, 0, kEndStart); }

Body BodyOf(const Sector &sector) {
  Body out{};
  std::copy_n(sector.data() + kPackHeader, kBody, out.data());
  return out;
}

bool PatchBodySid(Body &body, u8 sid) {
  if (body[0] != 0 || body[1] != 0 || body[2] != 1)
    return false;
  body[3] = sid;
  return true;
}

void PutBE16(u8 *p, u16 value) {
  p[0] = static_cast<u8>((value >> 8) & 0xFF);
  p[1] = static_cast<u8>(value & 0xFF);
}

void PutLE32(u8 *p, u32 value) {
  p[0] = static_cast<u8>(value & 0xFF);
  p[1] = static_cast<u8>((value >> 8) & 0xFF);
  p[2] = static_cast<u8>((value >> 16) & 0xFF);
  p[3] = static_cast<u8>((value >> 24) & 0xFF);
}

std::array<u8, 5> MpegTimestamp(u64 value, u8 hi) {
  return {
      static_cast<u8>(((((value >> 30) & 7) << 1) | 1) | (hi & 0xF0)),
      static_cast<u8>((value >> 22) & 0xFF),
      static_cast<u8>((((value >> 15) & 0x7F) << 1) | 1),
      static_cast<u8>((value >> 7) & 0xFF),
      static_cast<u8>(((value & 0x7F) << 1) | 1),
  };
}

std::array<u8, kPackHeader> MakePackHeader(u64 pack_index,
                                           long double exact_rate,
                                           u32 mux_rate) {
  std::array<u8, kPackHeader> out{};
  std::copy(kPackStart.begin(), kPackStart.end(), out.begin());
  const long double numerator =
      90000.0L * static_cast<long double>(pack_index * kSector + 9);
  const u64 scr = static_cast<u64>(std::llround(numerator / exact_rate));
  const auto ts = MpegTimestamp(scr, 0x20);
  std::copy(ts.begin(), ts.end(), out.begin() + 4);
  out[9] = static_cast<u8>(((mux_rate >> 15) & 0x7F) | 0x80);
  out[10] = static_cast<u8>((mux_rate >> 7) & 0xFF);
  out[11] = static_cast<u8>(((mux_rate & 0x7F) << 1) | 1);
  return out;
}

void PatchSystemRate(Sector &sector, u32 mux_rate) {
  if (!Starts(sector, 12, kSystemStart))
    return;
  sector[18] = static_cast<u8>(0x80 | ((mux_rate >> 15) & 0x7F));
  sector[19] = static_cast<u8>((mux_rate >> 7) & 0xFF);
  sector[20] = static_cast<u8>(((mux_rate & 0x7F) << 1) | 1);
}

bool WriteBody(std::ofstream &out, const Body &body, u64 pack_index,
               long double exact_rate, u32 mux_rate) {
  Sector sector{};
  const auto header = MakePackHeader(pack_index, exact_rate, mux_rate);
  std::copy(header.begin(), header.end(), sector.begin());
  std::copy(body.begin(), body.end(), sector.begin() + kPackHeader);
  PatchSystemRate(sector, mux_rate);
  out.write(reinterpret_cast<const char *>(sector.data()), sector.size());
  return static_cast<bool>(out);
}

std::optional<u32> VideoBitrate(const fs::path &source) {
  std::ifstream in(source, std::ios::binary);
  if (!in)
    return std::nullopt;
  Sector sector{};
  while (ReadSector(in, sector)) {
    if (StreamId(sector) != kVideo)
      continue;
    const size_t plen = (static_cast<size_t>(sector[16]) << 8) | sector[17];
    size_t i = 18;
    while (i < sector.size() && sector[i] == 0xFF)
      ++i;
    if (i < sector.size() && (sector[i] & 0xC0) == 0x40)
      i += 2;
    if (i < sector.size()) {
      const u8 control = sector[i];
      if ((control & 0xF0) == 0x20)
        i += 5;
      else if ((control & 0xF0) == 0x30)
        i += 10;
      else if (control == 0x0F)
        ++i;
    }
    const size_t end = std::min<size_t>(18 + plen, sector.size());
    if (i >= end)
      continue;
    for (size_t p = i; p + 11 <= end; ++p) {
      if (sector[p] != 0 || sector[p + 1] != 0 || sector[p + 2] != 1 ||
          sector[p + 3] != 0xB3)
        continue;
      const u8 *q = sector.data() + p + 4;
      const u32 value = (static_cast<u32>(q[4]) << 10) |
                        (static_cast<u32>(q[5]) << 2) |
                        ((q[6] & 0xC0) >> 6);
      return value * 400;
    }
  }
  return std::nullopt;
}

bool PrepareHeader(const fs::path &source, bool bdop,
                   std::array<Sector, 4> &header_sectors,
                   long double &exact_rate, u32 &mux_rate, std::string &error) {
  std::ifstream in(source, std::ios::binary);
  if (!in) {
    error = "could not open temporary Korean movie";
    return false;
  }
  for (auto &sector : header_sectors) {
    if (!ReadSector(in, sector)) {
      error = "Korean movie has a truncated Sofdec header";
      return false;
    }
  }
  std::vector<u8> header(kSector * 4);
  for (size_t i = 0; i < 4; ++i)
    std::copy(header_sectors[i].begin(), header_sectors[i].end(),
              header.begin() + i * kSector);
  const auto label = FindLabel(header);
  if (!label || *label + 0x160 + 5 * 0x40 > header.size()) {
    error = "Sofdec stream table was not found";
    return false;
  }
  if (header[*label + 0x90] != 4 || header[*label + 0x91] != 2) {
    error = "Korean regional movie is not the expected two-audio-stream layout";
    return false;
  }

  auto record_offset = [&](u8 wanted) -> std::optional<size_t> {
    for (size_t i = 0; i < header[*label + 0x90]; ++i) {
      const size_t off = *label + 0x160 + i * 0x40;
      if (header[off + 24] == wanted)
        return off;
    }
    return std::nullopt;
  };
  const auto c1 = record_offset(kC1);
  const auto c2 = record_offset(kC2);
  if (!c1 || !c2) {
    error = "Korean Sofdec C1/C2 stream records are missing";
    return false;
  }
  const size_t c3 = *label + 0x160 + 4 * 0x40;
  const size_t clone = bdop ? *c2 : *c1;
  std::copy_n(header.begin() + clone, 0x40, header.begin() + c3);
  header[c3 + 24] = kC3;
  header[*label + 0x90] = 5;
  header[*label + 0x91] = 3;

  const auto video_bps = VideoBitrate(source);
  if (!video_bps) {
    error = "MPEG video bitrate could not be read from Korean movie";
    return false;
  }
  const long double video = static_cast<long double>(*video_bps) / 8.0L *
                            2048.0L / 2018.0L;
  const long double aix = 1303680.0L / 8.0L * 2048.0L / 2016.0L;
  const long double sfa = 396900.0L / 8.0L * 2048.0L / 2016.0L;
  exact_rate = bdop ? video + aix * 2.0L + sfa : video + aix * 3.0L;
  mux_rate = static_cast<u32>(std::ceil(exact_rate / 50.0L));
  PutLE32(header.data() + *label + 0x94,
          static_cast<u32>(std::llround(exact_rate)));

  // Sector 0 contains the MPEG system header. The two-stream Korean file has
  // C1/C2 descriptors followed immediately by a padding packet. Add C3 and
  // shrink that padding packet by three bytes.
  if (!Starts(header, 12, kSystemStart) || header[16] != 0 ||
      header[17] != 12 || header[24] != kC1 || header[27] != kC2) {
    error = "unexpected Korean MPEG system-header layout";
    return false;
  }
  PutBE16(header.data() + 16, 15);
  header[21] = static_cast<u8>((header[21] & 0xC3) | (3 << 2));
  header[30] = kC3;
  header[31] = 0xC0;
  header[32] = 0x04;
  std::copy(kPaddingStart.begin(), kPaddingStart.end(), header.begin() + 33);
  PutBE16(header.data() + 37, static_cast<u16>(kSector - 39));
  header[39] = 0x0F;
  std::fill(header.begin() + 40, header.begin() + kSector, 0xFF);

  for (size_t i = 0; i < 4; ++i)
    std::copy_n(header.begin() + i * kSector, kSector,
                header_sectors[i].begin());
  return true;
}

bool VerifyConvertedSfd(const fs::path &source, const fs::path &converted) {
  std::ifstream src(source, std::ios::binary);
  std::ifstream dst(converted, std::ios::binary);
  if (!src || !dst)
    return false;
  Sector a{}, b{};
  for (int i = 0; i < 4; ++i) {
    if (!ReadSector(src, a) || !ReadSector(dst, b))
      return false;
  }

  auto next_body = [](std::ifstream &in, bool skip_c3,
                      Body &body) -> bool {
    Sector sector{};
    while (ReadSector(in, sector)) {
      if (IsEnd(sector))
        return false;
      if (skip_c3 && StreamId(sector) == kC3)
        continue;
      body = BodyOf(sector);
      return true;
    }
    return false;
  };

  Body sa{}, db{};
  while (true) {
    const bool hs = next_body(src, false, sa);
    const bool hd = next_body(dst, true, db);
    if (hs != hd)
      return false;
    if (!hs)
      break;
    if (sa != db)
      return false;
  }
  return true;
}

bool ConvertKoreanSfd(const fs::path &source, const fs::path &dest, bool bdop,
                      std::string &error) {
  std::array<Sector, 4> headers{};
  long double exact_rate = 0.0L;
  u32 mux_rate = 0;
  if (!PrepareHeader(source, bdop, headers, exact_rate, mux_rate, error))
    return false;

  std::ifstream first(source, std::ios::binary);
  if (!first) {
    error = "could not reopen Korean movie";
    return false;
  }
  Sector sector{};
  for (int i = 0; i < 4; ++i)
    if (!ReadSector(first, sector))
      return false;

  std::vector<Body> c1;
  std::vector<Body> c2;
  while (ReadSector(first, sector)) {
    if (IsEnd(sector))
      continue;
    const auto id = StreamId(sector);
    if (id == kC1)
      c1.push_back(BodyOf(sector));
    else if (id == kC2)
      c2.push_back(BodyOf(sector));
  }
  if (c1.empty() || c2.empty()) {
    error = "Korean movie has no C1/C2 media packets";
    return false;
  }

  fs::create_directories(dest.parent_path());
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "could not create converted Korean movie";
    return false;
  }
  u64 pack_index = 0;
  for (const auto &h : headers) {
    if (!WriteBody(out, BodyOf(h), pack_index++, exact_rate, mux_rate)) {
      error = "could not write converted Sofdec header";
      return false;
    }
  }

  std::ifstream second(source, std::ios::binary);
  for (int i = 0; i < 4; ++i)
    if (!ReadSector(second, sector))
      return false;
  std::optional<Sector> end_sector;

  if (bdop) {
    while (ReadSector(second, sector)) {
      if (IsEnd(sector)) {
        end_sector = sector;
        continue;
      }
      Body body = BodyOf(sector);
      if (!WriteBody(out, body, pack_index++, exact_rate, mux_rate))
        return false;
      if (StreamId(sector) == kC2) {
        if (!PatchBodySid(body, kC3) ||
            !WriteBody(out, body, pack_index++, exact_rate, mux_rate))
          return false;
      }
    }
  } else {
    size_t seen_c1 = 0;
    size_t seen_c2 = 0;
    const size_t group_count = std::max(c1.size(), c2.size());
    std::vector<bool> emitted(group_count, false);
    while (ReadSector(second, sector)) {
      if (IsEnd(sector)) {
        end_sector = sector;
        continue;
      }
      const auto id = StreamId(sector);
      if (id != kC1 && id != kC2) {
        if (!WriteBody(out, BodyOf(sector), pack_index++, exact_rate, mux_rate))
          return false;
        continue;
      }
      const size_t group = id == kC1 ? seen_c1++ : seen_c2++;
      if (group >= emitted.size() || emitted[group])
        continue;
      emitted[group] = true;
      if (group < c1.size() &&
          !WriteBody(out, c1[group], pack_index++, exact_rate, mux_rate))
        return false;
      if (group < c2.size() &&
          !WriteBody(out, c2[group], pack_index++, exact_rate, mux_rate))
        return false;
      if (group < c1.size()) {
        Body c3 = c1[group];
        if (!PatchBodySid(c3, kC3) ||
            !WriteBody(out, c3, pack_index++, exact_rate, mux_rate))
          return false;
      }
    }
    if (std::find(emitted.begin(), emitted.end(), false) != emitted.end()) {
      error = "Korean movie audio packet groups are incomplete";
      return false;
    }
  }

  Sector ending{};
  if (end_sector) {
    ending = *end_sector;
  } else {
    std::copy(kEndStart.begin(), kEndStart.end(), ending.begin());
    std::fill(ending.begin() + 4, ending.end(), 0xFF);
  }
  out.write(reinterpret_cast<const char *>(ending.data()), ending.size());
  out.close();
  if (!out) {
    error = "converted Korean movie could not be finalized";
    return false;
  }
  if (!VerifyConvertedSfd(source, dest)) {
    error = "converted Korean movie failed packet-body verification";
    return false;
  }
  return true;
}

void CollectFiles(rex::filesystem::Entry *dir, const std::string &prefix,
                  std::vector<DiscFile> &out) {
  for (const auto &child : dir->children()) {
    const std::string rel = prefix.empty() ? child->name()
                                           : prefix + "/" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory)
      CollectFiles(child.get(), rel, out);
    else
      out.push_back({rel, child.get()});
  }
}

bool WriteBytes(const fs::path &path, std::span<const u8> bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool IsKoreanRecord(const vfs::Key &key) {
  const std::string lower = Lower(std::string(key.str()));
  return lower.find("_kr.") != std::string::npos;
}

fs::path KeyPath(const vfs::Key &key) {
  std::string text(key.str());
  std::replace(text.begin(), text.end(), '\\', '/');
  return fs::path(text);
}

void AddProgress(InstallProgress &progress, size_t bytes) {
  progress.files_total.fetch_add(1);
  progress.bytes_total.fetch_add(bytes);
}

void FinishProgressItem(InstallProgress &progress, size_t bytes) {
  progress.files_done.fetch_add(1);
  progress.bytes_done.fetch_add(bytes);
}

bool Fail(InstallProgress &progress, std::string message) {
  progress.SetError(std::move(message));
  progress.failed.store(true);
  return false;
}

bool CopyTracked(const DiscFile &file, const fs::path &dest,
                 InstallProgress &progress) {
  if (progress.canceled.load())
    return false;
  AddProgress(progress, file.entry->size());
  progress.SetCurrentFile("Korean import: " + file.relative);
  if (!CopyEntry(*file.entry, dest / fs::path(file.relative)))
    return Fail(progress, "Could not copy Korean retail file: " + file.relative);
  FinishProgressItem(progress, file.entry->size());
  return true;
}

bool PatchBootIni(const fs::path &game_root, InstallProgress &progress) {
  const fs::path ini = game_root / "bd_boot.ini";
  std::ifstream in(ini);
  if (!in)
    return Fail(progress, "Installed bd_boot.ini is missing");
  std::vector<std::string> lines;
  std::string line;
  bool language = false;
  bool voice = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.starts_with("[Language]")) {
      language = true;
      const size_t close = line.find(']');
      std::string rest = close == std::string::npos ? std::string{} : line.substr(close + 1);
      std::string lower = Lower(rest);
      if (lower.find("kr") == std::string::npos)
        rest += " KR";
      while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
        rest.erase(rest.begin());
      line = "[Language]\t" + rest;
    } else if (line.starts_with("[Voice]")) {
      voice = true;
      const size_t close = line.find(']');
      std::string rest =
          close == std::string::npos ? std::string{} : line.substr(close + 1);
      std::string lower = Lower(rest);
      if (lower.find("kr") == std::string::npos)
        rest += " KR";
      while (!rest.empty() &&
             std::isspace(static_cast<unsigned char>(rest.front())))
        rest.erase(rest.begin());
      line = "[Voice]\t" + rest;
    }
    lines.push_back(std::move(line));
  }
  if (!language || !voice)
    return Fail(progress, "bd_boot.ini has no [Language]/[Voice] entries");

  const fs::path backup = game_root / "bd_boot.ini.pre_kr";
  std::error_code ec;
  if (!fs::exists(backup, ec))
    fs::copy_file(ini, backup, fs::copy_options::none, ec);

  AddProgress(progress, 1);
  progress.SetCurrentFile("Korean import: bd_boot.ini");
  std::ofstream out(ini, std::ios::trunc);
  for (const auto &l : lines)
    out << l << "\r\n";
  if (!out)
    return Fail(progress, "Could not update bd_boot.ini for Korean data");
  FinishProgressItem(progress, 1);
  return true;
}

bool ExtractKoreanText(rex::filesystem::DiscImageDevice &disc,
                       const fs::path &install_root, const fs::path &temp_root,
                       InstallProgress &progress) {
  struct ArchiveSpec {
    const char *disc_path;
    const char *prefix;
  };
  constexpr ArchiveSpec specs[] = {
      {"pack/!necessity.ipk", "!necessity"},
      {"pack/sca.ipk", "sca"},
      {"pack/sequence.ipk", "sequence"},
  };
  const fs::path mod_root = install_root / "mods" / "bd_asia_text";

  for (const auto &spec : specs) {
    auto *entry = disc.ResolvePath(spec.disc_path);
    if (!entry)
      return Fail(progress, std::string("Korean disc is missing ") + spec.disc_path);
    fs::path temp = temp_root / (std::string(spec.prefix) + ".ipk");
    progress.SetCurrentFile(std::string("Korean import: ") + spec.disc_path);
    if (!CopyEntry(*entry, temp))
      return Fail(progress, std::string("Could not stage ") + spec.disc_path);

    auto mount = vfs::IPKMount::Open(temp, vfs::Key::FromRelative(spec.prefix));
    if (!mount)
      return Fail(progress, std::string("Could not read ") + spec.disc_path);
    size_t extracted = 0;
    for (const auto &key : mount->Keys()) {
      if (!IsKoreanRecord(key))
        continue;
      const auto size = mount->Stat(key);
      auto bytes = mount->Read(key);
      if (!size || !bytes || bytes->empty())
        return Fail(progress, "Could not extract Korean text record: " +
                                  std::string(key.str()));
      AddProgress(progress, bytes->size());
      progress.SetCurrentFile("Korean import: " + std::string(key.str()));
      if (!WriteBytes(mod_root / KeyPath(key),
                      std::span<const u8>(bytes->data(), bytes->size())))
        return Fail(progress, "Could not write Korean text record: " +
                                  std::string(key.str()));
      FinishProgressItem(progress, bytes->size());
      ++extracted;
    }
    mount.reset();
    std::error_code ec;
    fs::remove(temp, ec);
    if (extracted == 0)
      return Fail(progress, std::string("No Korean records found in ") +
                                spec.disc_path);
    BD_INFO("Korean import: {} records from {}", extracted, spec.disc_path);
  }

  const std::string mod_toml =
      "[mod]\n"
      "name = \"Blue Dragon Korean Retail Text\"\n"
      "author = \"re:Blue contributors\"\n"
      "version = \"1.0\"\n"
      "description = \"Korean text/font/UI records imported from the user's own retail discs for an NTSC-U re:Blue base.\"\n";
  AddProgress(progress, mod_toml.size());
  progress.SetCurrentFile("Korean import: mods/bd_asia_text/mod.toml");
  if (!WriteBytes(mod_root / "mod.toml",
                  std::span<const u8>(reinterpret_cast<const u8 *>(mod_toml.data()),
                                      mod_toml.size())))
    return Fail(progress, "Could not write Korean text mod metadata");
  FinishProgressItem(progress, mod_toml.size());
  return true;
}

} // namespace

bool ValidateKoreanRetailSources(
    const std::array<fs::path, kDiscCount> &sources, std::string *error) {
  for (int i = 0; i < kDiscCount; ++i) {
    auto image = OpenDiscImage(sources[i]);
    if (!image || !ValidateDisc(*image, i + 1)) {
      if (error)
        *error = "Could not open Korean retail DVD " + std::to_string(i + 1);
      return false;
    }
    if (!HasKorean(*image)) {
      if (error)
        *error = "DVD " + std::to_string(i + 1) +
                 " does not advertise Korean content";
      return false;
    }
  }
  return true;
}

bool ImportKoreanRetailData(
    const std::array<fs::path, kDiscCount> &sources,
    const fs::path &game_data_dest, InstallProgress &progress) {
  if (progress.canceled.load())
    return false;

  std::array<std::unique_ptr<rex::filesystem::DiscImageDevice>, kDiscCount>
      images{};
  for (int i = 0; i < kDiscCount; ++i) {
    images[i] = OpenDiscImage(sources[i]);
    if (!images[i] || !ValidateDisc(*images[i], i + 1) ||
        !HasKorean(*images[i]))
      return Fail(progress, "Invalid Korean retail DVD " + std::to_string(i + 1));
  }

  const fs::path install_root = game_data_dest.parent_path();
  const fs::path temp_root = install_root / ".reblue_kr_import";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec)
    return Fail(progress, "Could not create Korean import temporary directory");

  // 1) Korean packmem and XACT banks. Never copy default.xex or common pack
  // archives from these discs: the runtime remains NTSC-U code/data.
  std::vector<DiscFile> copies;
  std::set<std::string> seen;
  for (size_t di = 0; di < images.size(); ++di) {
    auto *disc = images[di].get();
    if (auto *entry = disc->ResolvePath("pack/packmem_kr.ipk"); entry &&
        seen.insert("pack/packmem_kr.ipk").second)
      copies.push_back({"pack/packmem_kr.ipk", entry});

    for (const char *dir_name : {"snd_memory_kr", "snd_stream_kr"}) {
      auto *dir = disc->ResolvePath(dir_name);
      if (!dir || !(dir->attributes() & rex::filesystem::kFileAttributeDirectory))
        continue;
      std::vector<DiscFile> found;
      CollectFiles(dir, dir_name, found);
      for (auto &file : found)
        if (seen.insert(file.relative).second)
          copies.push_back(std::move(file));
    }
  }
  if (!seen.contains("pack/packmem_kr.ipk"))
    return Fail(progress, "Korean packmem_kr.ipk was not found on the retail discs");
  for (const auto &file : copies)
    if (!CopyTracked(file, game_data_dest, progress)) {
      fs::remove_all(temp_root, ec);
      return false;
    }

  // 2) Korean text/font/UI records from the Asian common archives. These are
  // emitted as a loose VFS mod so the NTSC-U archives remain byte-identical.
  if (!ExtractKoreanText(*images[0], install_root, temp_root, progress)) {
    fs::remove_all(temp_root, ec);
    return false;
  }

  // 3) Copy only two-audio-stream regional movies and transform them to the
  // NTSC-U runtime's three-stream topology. One shared BDopdemo appears on
  // multiple discs; relative-path de-duplication keeps a single copy.
  std::vector<DiscFile> movies;
  std::set<std::string> seen_movies;
  for (auto &image : images) {
    auto *movie_dir = image->ResolvePath("movie");
    if (!movie_dir)
      continue;
    std::vector<DiscFile> found;
    CollectFiles(movie_dir, "movie", found);
    for (auto &file : found) {
      const std::string lower = Lower(file.relative);
      if (!lower.ends_with(".sfd") || !seen_movies.insert(lower).second)
        continue;
      if (EntrySfdAudioCount(*file.entry) == 2)
        movies.push_back(std::move(file));
    }
  }
  if (movies.size() != 69)
    BD_WARN("Korean import: detected {} unique two-track regional movies (validated retail set has 69)",
            movies.size());
  else
    BD_INFO("Korean import: detected validated 69-movie regional set");

  for (size_t i = 0; i < movies.size(); ++i) {
    if (progress.canceled.load()) {
      fs::remove_all(temp_root, ec);
      return false;
    }
    auto &movie = movies[i];
    AddProgress(progress, movie.entry->size());
    progress.SetCurrentFile("Korean movie " + std::to_string(i + 1) + "/" +
                            std::to_string(movies.size()) + ": " + movie.relative);
    const fs::path source_temp = temp_root / ("movie_" + std::to_string(i) + ".sfd");
    const fs::path converted_temp = temp_root / ("movie_" + std::to_string(i) + ".converted.sfd");
    if (!CopyEntry(*movie.entry, source_temp)) {
      fs::remove_all(temp_root, ec);
      return Fail(progress, "Could not stage Korean movie: " + movie.relative);
    }
    std::string convert_error;
    const bool bdop = Lower(fs::path(movie.relative).filename().string()) ==
                      "bdopdemo.sfd";
    if (!ConvertKoreanSfd(source_temp, converted_temp, bdop, convert_error)) {
      fs::remove_all(temp_root, ec);
      return Fail(progress, "Could not convert " + movie.relative + ": " +
                                convert_error);
    }
    const fs::path dest = game_data_dest / fs::path(movie.relative);
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
      fs::remove_all(temp_root, ec);
      return Fail(progress, "Could not create Korean movie destination");
    }
    ec.clear();
    fs::remove(dest, ec);
    ec.clear();
    fs::rename(converted_temp, dest, ec);
    if (ec) {
      fs::remove_all(temp_root, ec);
      return Fail(progress, "Could not install converted Korean movie: " +
                                movie.relative);
    }
    fs::remove(source_temp, ec);
    FinishProgressItem(progress, movie.entry->size());
  }

  // 4) Expose KR in the base boot configuration while preserving all voice
  // entries already present in the NTSC-U install.
  if (!PatchBootIni(game_data_dest, progress)) {
    fs::remove_all(temp_root, ec);
    return false;
  }

  const std::string marker =
      "Korean retail data imported by re:Blue\n"
      "Base executable remains NTSC-U.\n";
  if (!WriteBytes(install_root / "reblue_korean_import.marker",
                  std::span<const u8>(reinterpret_cast<const u8 *>(marker.data()),
                                      marker.size()))) {
    fs::remove_all(temp_root, ec);
    return Fail(progress, "Could not write Korean import marker");
  }

  fs::remove_all(temp_root, ec);
  BD_INFO("Korean retail import complete: {} XACT/pack files, {} regional movies",
          copies.size(), movies.size());
  return true;
}

int KoreanVoiceIndex(const fs::path &game_data_dest) {
  std::ifstream in(game_data_dest / "bd_boot.ini");
  if (!in)
    return 0;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.starts_with("[Voice]"))
      continue;
    const size_t close = line.find(']');
    if (close == std::string::npos)
      return 0;
    std::string_view rest(line.data() + close + 1,
                          line.size() - close - 1);
    int index = 0;
    size_t p = 0;
    while (p < rest.size()) {
      while (p < rest.size() &&
             std::isspace(static_cast<unsigned char>(rest[p])))
        ++p;
      size_t q = p;
      while (q < rest.size() &&
             !std::isspace(static_cast<unsigned char>(rest[q])))
        ++q;
      if (q > p) {
        ++index;
        if (Lower(std::string(rest.substr(p, q - p))) == "kr")
          return index;
      }
      p = q;
    }
    return 0;
  }
  return 0;
}

} // namespace bd::installer
