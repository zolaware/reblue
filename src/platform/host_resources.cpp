/**
 * @file    platform/host_resources.cpp
 * @brief   Descriptor and mapping counters from /proc on Linux, /dev/fd and
 *          Mach VM regions on macOS, or the Win32 handle count.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/host_resources.h"

#include <cstdio>

#if defined(_WIN32)

#include "core/windows_lean.h"

namespace bd::platform {

std::string ResourceUse() {
  DWORD handles = 0;
  if (!GetProcessHandleCount(GetCurrentProcess(), &handles))
    return "handles unknown";
  return "handles " + std::to_string(handles);
}

void RaiseFDLimit() {}

} // namespace bd::platform

#else

#include <dirent.h>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
#include <mach/vm_map.h>
#else
#include <mach/mach_vm.h>
#endif
#include <mach/vm_region.h>
#endif

namespace bd::platform {
namespace {

// opendir's own descriptor shows up in the listing, so drop it.
long CountOpenDescriptors() {
  DIR *dir = ::opendir(
#if defined(__APPLE__)
      "/dev/fd" // no /proc on macOS, fdescfs mounts the same thing here
#else
      "/proc/self/fd"
#endif
  );
  if (!dir)
    return -1;
  long count = 0;
  while (const dirent *entry = ::readdir(dir)) {
    if (entry->d_name[0] != '.')
      ++count;
  }
  ::closedir(dir);
  return count - 1;
}

#if defined(__APPLE__)

// Walks the task's top-level VM regions the way counting /proc/self/maps
// lines does on Linux, macOS has no /proc, so this is the mach equivalent.
long CountMemoryRegions() {
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  vm_address_t address = 0;
  vm_size_t size = 0;
#else
  mach_vm_address_t address = 0;
  mach_vm_size_t size = 0;
#endif
  long regions = 0;
  vm_region_basic_info_data_64_t info;
  while (true) {
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name = MACH_PORT_NULL;
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
    const kern_return_t kr = vm_region_64(
        mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &info_count, &object_name);
#else
    const kern_return_t kr = mach_vm_region(
        mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &info_count, &object_name);
#endif
    if (kr != KERN_SUCCESS)
      break;
    if (object_name != MACH_PORT_NULL)
      ::mach_port_deallocate(mach_task_self(), object_name);
    ++regions;
    // A zero-length region would leave the cursor parked, so stop rather than
    // spin: this runs on failure paths, where a hang buries the real error.
    if (size == 0)
      break;
    address += size;
  }
  return regions;
}

#else

long CountLines(const char *path) {
  FILE *file = ::fopen(path, "re");
  if (!file)
    return -1;
  long lines = 0;
  for (int c = ::fgetc(file); c != EOF; c = ::fgetc(file)) {
    if (c == '\n')
      ++lines;
  }
  ::fclose(file);
  return lines;
}

long ReadLong(const char *path) {
  FILE *file = ::fopen(path, "re");
  if (!file)
    return -1;
  long value = -1;
  if (std::fscanf(file, "%ld", &value) != 1)
    value = -1;
  ::fclose(file);
  return value;
}

#endif

} // namespace

std::string ResourceUse() {
  rlimit nofile{};
  const long fd_limit = (::getrlimit(RLIMIT_NOFILE, &nofile) == 0)
                            ? static_cast<long>(nofile.rlim_cur)
                            : -1;
  char line[192];
#if defined(__APPLE__)
  // No vm.max_map_count analog on macOS (no VMA count ceiling, just address
  // space), so the ceiling is reported as -1 (not applicable).
  std::snprintf(line, sizeof(line), "fds %ld/%ld, maps %ld/-1",
                CountOpenDescriptors(), fd_limit, CountMemoryRegions());
#else
  std::snprintf(line, sizeof(line), "fds %ld/%ld, maps %ld/%ld",
                CountOpenDescriptors(), fd_limit, CountLines("/proc/self/maps"),
                ReadLong("/proc/sys/vm/max_map_count"));
#endif
  return line;
}

void RaiseFDLimit() {
  rlimit lim{};
  if (::getrlimit(RLIMIT_NOFILE, &lim) != 0)
    return;
  const rlim_t target =
      (lim.rlim_max == RLIM_INFINITY) ? rlim_t{10240} : lim.rlim_max;
  if (lim.rlim_cur >= target)
    return;
  lim.rlim_cur = target;
  ::setrlimit(RLIMIT_NOFILE, &lim);
}

} // namespace bd::platform

#endif
