/**
 * @file    vfs/dir_io.cpp
 * @brief   Guest directory listing interception: the debug tools browse the
 * disc with FindFirstFile, which cannot see inside a .ipk, so the mount index
 * is merged into every listing they take.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <cstddef>
#include <set>
#include <string>
#include <string_view>

#include <rex/ppc/hook.h>
#include <rex/ppc/stack.h>
#include <rex/string/text.h>
#include <rex/types.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "vfs/file_system.h"
#include "vfs/vfs.h"

// isk::StrList::Add: vsprintf's its format into a scratch,
// duplicates the result and appends it. Reused rather than reimplemented
// because the list is torn down with the CRT free() that only its own malloc
// pairs with.
REX_IMPORT(__imp__isk__StrList__Add, StrListAdd, u32(u32, u32, u32));

namespace {

struct IskStrEntry_t {
  be_u32 type; // written by the scan, not by Add
  be_u32 name;
};
static_assert(sizeof(IskStrEntry_t) == 0x08);

struct IskStrList_t {
  be_u32 vtable;
  be_u32 count;
  be_u32 entries; // -> IskStrEntry_t[count]
};
static_assert(offsetof(IskStrList_t, count) == 0x04);
static_assert(offsetof(IskStrList_t, entries) == 0x08);

// isk__StrList__ScanDirectory's r6.
constexpr u32 kScanRecurse = 0x00001;  // descend rather than list directories
constexpr u32 kScanFullPath = 0x10000; // store dir+name, not the leaf name

constexpr u32 kTypeDir = 1;
constexpr u32 kTypeFile = 2;

// isk::StrList's sort (0x823885F8) stages the list in a fixed stack frame of
// 0x1000 bytes of indices and 0x20000 of names, 0x80 per entry, so a longer
// list smashes its own stack. The engine never listed a directory that big,
// but merging archive records into one can.
constexpr u32 kMaxEntries = 1024;

// PathExtensionEquals: what follows the last '.' of each side, compared
// case-blind.
bool MatchesFilter(const std::string &name, const std::string &filter) {
  if (filter.empty())
    return true;
  auto extension = [](const std::string &text) {
    const auto dot = text.rfind('.');
    return dot == std::string::npos ? text : text.substr(dot + 1);
  };
  return rex::string::utf8_lower_ascii(extension(name)) == rex::string::utf8_lower_ascii(extension(filter));
}

u32 EntryCount(u32 list_va) {
  const auto *list = bd::mem::at<const IskStrList_t>(list_va);
  return list ? static_cast<u32>(list->count) : 0;
}

// What the host walk already found. The engine recurses through this same hook,
// so a name shipped loose, or merged one level down, must not be added twice.
std::set<std::string> ExistingNames(u32 list_va) {
  std::set<std::string> names;
  const auto *list = bd::mem::at<const IskStrList_t>(list_va);
  if (!list)
    return names;

  const auto *entries = bd::mem::at<const IskStrEntry_t>(list->entries);
  if (!entries)
    return names;

  const u32 count = list->count;
  for (u32 i = 0; i < count; ++i)
    names.insert(rex::string::utf8_lower_ascii(bd::mem::cstr(entries[i].name)));
  return names;
}

void AddEntry(u32 list_va, const std::string &text, u32 type) {
  rex::ppc::StackFrame frame;
  // Passed through a '%s' rather than as the format itself, so a '%' in a
  // record name cannot make the guest walk a va_list that is not there.
  const u32 format = frame.PushString("%s").GetGuestAddress();
  const u32 value = frame.PushString(text).GetGuestAddress();

  const u32 count = StrListAdd(list_va, format, value);
  if (count == 0)
    return;

  const auto *list = bd::mem::at<const IskStrList_t>(list_va);
  if (!list)
    return;
  if (auto *entries = bd::mem::at<IskStrEntry_t>(list->entries))
    entries[count - 1].type = type;
}

// An .ipk is a file to the host walk and a directory to the mount index. Flip
// the entry the walk already made so the browser descends into it, which comes
// back through this hook with nothing on disk to find and the records to serve.
void PromoteToDirectory(u32 list_va, const std::string &text) {
  const auto *list = bd::mem::at<const IskStrList_t>(list_va);
  if (!list)
    return;
  auto *entries = bd::mem::at<IskStrEntry_t>(list->entries);
  if (!entries)
    return;

  const std::string wanted = rex::string::utf8_lower_ascii(text);
  const u32 count = list->count;
  for (u32 i = 0; i < count; ++i) {
    if (rex::string::utf8_lower_ascii(bd::mem::cstr(entries[i].name)) == wanted)
      entries[i].type = kTypeDir;
  }
}

// 'dir' is the scan's own argument: drive-prefixed and separator-terminated.
bd::vfs::Key DirKey(const std::string &dir) {
  auto key = bd::vfs::Key::FromGuestPath(dir).Localized();
  const auto text = key.str();
  return (!text.empty() && text.back() == '\\') ? key.Slice(0, text.size() - 1)
                                                : key;
}

u32 MergeMounts(u32 list_va, const std::string &dir, const std::string &filter,
                u32 flags, std::set<std::string> &seen) {
  u32 added = 0;
  for (const auto &entry : bd::vfs::VFS::Get().Files().List(DirKey(dir))) {
    // The scan composes its own paths as '%s%s', trailing separator and
    // all, and what it stores has to open again through the same loader.
    const std::string path = dir + entry.name;

    if (entry.is_dir && (flags & kScanRecurse)) {
      added += MergeMounts(list_va, path + "\\", filter, flags, seen);
      continue;
    }
    if (!entry.is_dir && !MatchesFilter(entry.name, filter))
      continue;

    const std::string text = (flags & kScanFullPath) ? path : entry.name;
    if (!seen.insert(rex::string::utf8_lower_ascii(text)).second) {
      if (entry.is_dir)
        PromoteToDirectory(list_va, text);
      continue;
    }

    if (EntryCount(list_va) >= kMaxEntries) {
      REXBD_WARN("vfs directory listing capped at {} entries: '{}'", kMaxEntries,
              dir);
      return added;
    }
    AddEntry(list_va, text, entry.is_dir ? kTypeDir : kTypeFile);
    ++added;
  }
  return added;
}

} // namespace

// isk__StrList__ScanDirectory, the debug tools' recursive directory scan:
// r3 = isk::StrList, r4 = directory, r5 = extension filter or 0, r6 = flags.
// Returns how many entries it appended. AddEntry pushes its scratch strings
// onto the stack (see the note there).
REX_DETOUR(isk__StrList__ScanDirectory, u32,
           (u32 list, u32 directory, u32 filterVA, u32 flags)) {
  const std::string dir = bd::mem::cstr(directory);
  const std::string filter = bd::mem::cstr(filterVA);

  const u32 found = original(list, directory, filterVA, flags);

  auto seen = ExistingNames(list);
  return found + MergeMounts(list, dir, filter, flags, seen);
}
