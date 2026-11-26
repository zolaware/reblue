// SPDX-License-Identifier: BSD-3-Clause
#include "launcher/content_setup.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include "core/windows_lean.h"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace bd::launcher {
namespace {
constexpr const char *kTransaction = ".reblue-preparing";
constexpr const char *kLock = ".reblue-preparing.lock";
// Every kContentVersion starts with this, so an older version is still ours to replace.
constexpr std::string_view kOwnerPrefix = "reblue-content-";

// path::string() throws on Windows for characters outside the ANSI code page.
std::string Utf8(const fs::path &path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

std::string MarkerVersion(const fs::path &game) {
  std::ifstream marker(game / kOwner);
  std::string line;
  std::getline(marker, line);
  return line;
}

bool Linked(const fs::path &path) {
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return fs::is_symlink(fs::symlink_status(path));
#endif
}

void CheckDestination(const fs::path &game) {
  if (Linked(game) || (fs::exists(game) && !Owned(game)))
    throw std::runtime_error("The game destination is not managed by this integration; its files were kept.");
}

// An OS lock on a file in the installation, held for the whole setup. The OS
// lets go of it when the process ends, however it ends (the launcher's Cancel,
// its stop after an hour without output, or the launcher closing), so a lock
// that can be taken means any transaction on disk was left by a stopped run.
class SetupLock {
public:
  explicit SetupLock(const fs::path &root) {
    const auto file = root / kLock;
#ifdef _WIN32
    // No sharing: a second open fails while this handle is open. The file goes
    // with the handle.
    handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_SHARING_VIOLATION)
        Running();
      throw std::runtime_error("Cannot create the setup lock " + Utf8(file) + ".");
    }
#else
    // The file stays after the run: removing it while another setup waits to
    // open it would let two setups lock different files.
    fd_ = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0)
      throw std::runtime_error("Cannot create the setup lock " + Utf8(file) + ".");
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      const int error = errno;
      ::close(fd_);
      if (error == EWOULDBLOCK)
        Running();
      throw std::runtime_error("Cannot lock " + Utf8(file) + ".");
    }
#endif
  }
  ~SetupLock() {
#ifdef _WIN32
    CloseHandle(handle_);
#else
    ::close(fd_);
#endif
  }
  SetupLock(const SetupLock &) = delete;
  SetupLock &operator=(const SetupLock &) = delete;

private:
  [[noreturn]] static void Running() {
    throw std::runtime_error("Another re:Blue setup is running for this installation. "
                             "Wait for it to finish, then try again.");
  }
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

// Call with the lock held. A stopped run leaves the transaction in one of these
// states: game/ in place and new/ partly written (remove it), or the previous
// game moved to old/ and the new one not yet published (put old/ back).
void Recover(const fs::path &root) {
  const auto transaction = root / kTransaction;
  if (!fs::exists(fs::symlink_status(transaction)))
    return;
  const auto game = root / "game", old = transaction / "old";
  if (fs::is_directory(fs::symlink_status(old)) && !fs::exists(fs::symlink_status(game)))
    fs::rename(old, game);
  std::error_code error;
  fs::remove_all(transaction, error);
  if (error)
    throw std::runtime_error("Cannot remove the unfinished setup in " + Utf8(transaction) +
                             ": " + error.message());
}
} // namespace

void RecoverSetup(const fs::path &install) {
  const auto root = fs::weakly_canonical(fs::absolute(install));
  if (!fs::exists(fs::symlink_status(root / kTransaction)))
    return;
  SetupLock lock(root);
  Recover(root);
}

bool Overlaps(const fs::path &a, const fs::path &b) {
  const auto left = fs::weakly_canonical(fs::absolute(a));
  const auto right = fs::weakly_canonical(fs::absolute(b));
  for (const auto &pair : {std::pair{left, right}, std::pair{right, left}}) {
    for (auto at = pair.second; !at.empty();) {
      if (pair.first == at ||
          (fs::exists(pair.first) && fs::exists(at) && fs::equivalent(pair.first, at)))
        return true;
      const auto parent = at.parent_path();
      if (parent == at)
        break;
      at = parent;
    }
  }
  return false;
}

Sources FindDiscs(const fs::path &input, const Inspector &inspect) {
  const auto source = fs::canonical(input);
  const auto directory = fs::is_directory(source) ? source : source.parent_path();
  std::vector<fs::path> candidates;
  if (fs::is_regular_file(source))
    candidates.push_back(source);
  for (const auto &entry : fs::directory_iterator(directory)) {
    auto extension = Utf8(entry.path().extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (entry.is_regular_file() && extension == ".iso") {
      const auto path = fs::canonical(entry.path());
      if (std::find(candidates.begin(), candidates.end(), path) == candidates.end())
        candidates.push_back(path);
    }
  }
  std::sort(candidates.begin(), candidates.end());
  Sources sources{};
  for (const auto &path : candidates) {
    const auto discs = inspect(path);
    for (size_t i = 0; i < discs.size(); ++i) {
      if (!discs[i])
        continue;
      if (!sources[i].empty() && !fs::equivalent(sources[i], path))
        throw std::runtime_error("More than one image contains DVD " + std::to_string(i + 1) +
                                 ". Select a folder with one copy of each disc.");
      sources[i] = path;
    }
  }
  for (size_t i = 0; i < sources.size(); ++i)
    if (sources[i].empty())
      throw std::runtime_error("Missing Blue Dragon DVD " + std::to_string(i + 1) +
                               ". Place all three Global-release ISOs in the selected folder.");
  return sources;
}

Sources ValidateDiscs(const Sources &input, const Inspector &inspect) {
  Sources sources{};
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i].empty() || !fs::is_regular_file(input[i]))
      throw std::runtime_error("Choose an ISO file for Blue Dragon DVD " + std::to_string(i + 1) + ".");
    sources[i] = fs::canonical(input[i]);
    for (size_t j = 0; j < i; ++j)
      if (fs::equivalent(sources[i], sources[j]))
        throw std::runtime_error("The same image was selected for more than one DVD.");
    const auto discs = inspect(sources[i]);
    if (!discs[i] || std::count(discs.begin(), discs.end(), true) != 1)
      throw std::runtime_error("The selected image must contain only Blue Dragon DVD " + std::to_string(i + 1) + ".");
  }
  return sources;
}

bool Owned(const fs::path &game) {
  return MarkerVersion(game).starts_with(kOwnerPrefix);
}

bool Current(const fs::path &game) {
  return MarkerVersion(game) == kContentVersion;
}

fs::path PreparedGame(const fs::path &install) {
  const auto game = fs::absolute(install) / "game";
  CheckDestination(game);
  if (!Owned(game) || !fs::is_regular_file(game / "default.xex") ||
      !fs::is_regular_file(game / "reblue_install.marker"))
    throw std::runtime_error("Run content preparation in RecompLauncher before starting re:Blue.");
  if (!Current(game))
    throw std::runtime_error("This re:Blue version needs its game files set up again. "
                             "Run content preparation in RecompLauncher with your discs.");
  return game;
}

bool IsPrepared(const fs::path &install) {
  try {
    PreparedGame(install);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

namespace {
void PrepareSources(const Sources &sources, const fs::path &root,
                    const Extractor &extract, const Progress &progress) {
  const auto game = root / "game";
  for (const auto &disc : sources)
    if (Overlaps(root, disc))
      throw std::runtime_error("The disc images and installation must be in separate folders.");
  fs::create_directories(root);
  const SetupLock lock(root);
  Recover(root);
  CheckDestination(game);
  const auto transaction = root / kTransaction;
  if (!fs::create_directory(transaction))
    throw std::runtime_error("Cannot start setup in " + Utf8(transaction) + ".");
  bool moved_old = false, published = false, preserve = false;
  try {
    const auto staged = transaction / "new" / "game";
    progress(0.05, "Extracting game files");
    extract(sources, staged, progress);
    for (const auto *required : {"default.xex", "bd_boot.ini", "bd_disc_1.xml",
                                 "bd_disc_2.xml", "bd_disc_3.xml", "reblue_install.marker"})
      if (!fs::is_regular_file(staged / required) || fs::file_size(staged / required) == 0)
        throw std::runtime_error(std::string("Preparation did not produce ") + required);
    {
      std::ofstream marker(staged / kOwner, std::ios::trunc);
      marker << kContentVersion << '\n';
      marker.close();
      if (!marker)
        throw std::runtime_error("Cannot write the preparation ownership marker.");
    }
    CheckDestination(game);
    if (fs::exists(game)) {
      fs::rename(game, transaction / "old");
      moved_old = true;
    }
    fs::rename(staged, game);
    published = true;
  } catch (...) {
    if (moved_old && !published) {
      std::error_code error;
      fs::rename(transaction / "old", game, error);
      preserve = static_cast<bool>(error);
    }
    if (!preserve) {
      std::error_code ignored;
      fs::remove_all(transaction, ignored);
    }
    throw;
  }
  std::error_code ignored;
  fs::remove_all(transaction, ignored);
  progress(1, "Ready to play");
}
} // namespace

std::vector<DLCSource> ValidateDLC(const std::vector<fs::path> &input,
                                   const fs::path &install, const DLCValidator &validate) {
  std::vector<DLCSource> sources;
  const auto root = fs::weakly_canonical(fs::absolute(install));
  for (const auto &path : input) {
    if (!fs::is_regular_file(path))
      throw std::runtime_error("DLC package not found: " + Utf8(path));
    const auto source = fs::canonical(path);
    if (Overlaps(root, source.parent_path()))
      throw std::runtime_error("Choose an installation folder separate from your DLC packages.");
    const auto name = validate(source);
    for (const auto &previous : sources)
      if (fs::equivalent(source, previous.path) || name == previous.name)
        throw std::runtime_error("The same DLC pack was selected more than once.");
    sources.push_back({source, name});
  }
  return sources;
}

void Prepare(const fs::path &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress) {
  const auto root = fs::weakly_canonical(fs::absolute(install));
  const auto source = fs::canonical(input);
  const auto source_dir = fs::is_directory(source) ? source : source.parent_path();
  if (Overlaps(root, source_dir))
    throw std::runtime_error("Choose an installation folder separate from your disc-image folder.");
  const auto game = root / "game";
  CheckDestination(game);
  progress(0, "Checking all three Blue Dragon discs");
  const auto sources = FindDiscs(source, inspect);
  PrepareSources(sources, root, extract, progress);
}

void Prepare(const Sources &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress) {
  const auto root = fs::weakly_canonical(fs::absolute(install));
  CheckDestination(root / "game");
  progress(0, "Checking all three Blue Dragon discs");
  const auto sources = ValidateDiscs(input, inspect);
  for (const auto &disc : sources)
    if (Overlaps(root, disc.parent_path()))
      throw std::runtime_error("Choose an installation folder separate from your disc-image folders.");
  PrepareSources(sources, root, extract, progress);
}
} // namespace bd::launcher
