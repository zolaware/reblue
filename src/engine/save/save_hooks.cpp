/**
 * @file        engine/save/save_hooks.cpp
 *
 * @brief       Route Blue Dragon's save path off the XAM content manager onto
 *              the host save store.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <rex/ppc/hook.h>
#include <rex/memory.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/types/content.h>
#include <rex/kernel/types/status.h>
#include <rex/logging/log_category.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/save/save_manager.h"
#include "engine/save/save_types.h"
#include "core/cvar_table.h"
#include "engine/settings.h"

using rex::kernel::X_RESULT;
using rex::kernel::X_ERROR_ALREADY_EXISTS;
using rex::kernel::X_ERROR_INVALID_PARAMETER;
using rex::kernel::X_ERROR_PATH_NOT_FOUND;
using rex::kernel::X_ERROR_SUCCESS;

namespace {

// XCONTENT_DISPOSITION, returned via pdwDisposition.
enum class XContentDisposition : u32 {
  kNew = 1,
  kExisting = 2,
};

// The low nibble of dwContentFlags, matching CreateFile's disposition set.
enum class ContentCreateDisposition : u32 {
  kCreateNew = 1,
  kCreateAlways = 2,
  kOpenExisting = 3,
  kOpenAlways = 4,
  kTruncateExisting = 5,
};
constexpr u32 kContentDispositionMask = 0xF;

constexpr u32 kContentTaskMaxSlots = 30;

} // namespace

// XContentCreate, a thunk onto XamContentCreateEx: nine arguments,
// the last one passed on the caller's stack at r1+0x54. Handle root "save"
// locally, defer any other root to the original (no other roots reach this in
// BD).
REX_IMPORT(__imp__rex_XContentCreateWrapper, XContentCreateWrapper,
           u32(u32, u32, u32, u32, u32, u32, u32, u64, u32));
u32 rex_XContentCreateWrapper_hook(u32 userIndex, u32 rootName, u32 contentData,
                                   u32 flags, u32 dispositionOut,
                                   u32 licenseMaskOut, u32 cacheSize,
                                   u64 contentSize, u32 overlapped) {
  const auto *root = bd::mem::at<const char>(rootName);
  const auto *content = bd::mem::at<const rex::kernel::XCONTENT_DATA>(contentData);
  auto *disposition = bd::mem::at<be_u32>(dispositionOut);

  if (!root || std::strncmp(root, "save", 5) != 0 || !content) {
    return XContentCreateWrapper(userIndex, rootName, contentData, flags,
                                 dispositionOut, licenseMaskOut, cacheSize,
                                 contentSize, overlapped);
  }

  std::string slot = content->GetFileName();

  const bool exists = bd::engine::SaveSlotExists(slot);
  const u32 requested = flags & kContentDispositionMask;
  u32 result = X_ERROR_INVALID_PARAMETER;
  u32 out_disposition = 0;

  switch (static_cast<ContentCreateDisposition>(requested)) {
  case ContentCreateDisposition::kCreateNew:
    if (exists) {
      result = X_ERROR_ALREADY_EXISTS;
    } else if (bd::engine::CreateSaveSlotDir(slot)) {
      out_disposition = u32(XContentDisposition::kNew);
      result = X_ERROR_SUCCESS;
    } else {
      result = X_ERROR_PATH_NOT_FOUND; // fail the save loudly
    }
    break;
  case ContentCreateDisposition::kCreateAlways:
  case ContentCreateDisposition::kOpenAlways:
    if (bd::engine::CreateSaveSlotDir(slot)) {
      out_disposition = u32(exists ? XContentDisposition::kExisting
                                   : XContentDisposition::kNew);
      result = X_ERROR_SUCCESS;
    } else {
      result = X_ERROR_PATH_NOT_FOUND;
    }
    break;
  case ContentCreateDisposition::kOpenExisting:
  case ContentCreateDisposition::kTruncateExisting:
    // A miss here is how enumeration learns the slot is empty.
    if (exists) {
      out_disposition = u32(XContentDisposition::kExisting);
      result = X_ERROR_SUCCESS;
    } else {
      result = X_ERROR_PATH_NOT_FOUND;
    }
    break;
  default:
    REXBD_WARN("savehook unexpected disposition {} for slot '{}'", requested,
            slot);
    break;
  }

  if (result == X_ERROR_SUCCESS)
    bd::engine::SetCurrentSaveSlot(slot);
  if (disposition)
    *disposition = out_disposition;
  return result;
}
REX_HOOK(rex_XContentCreateWrapper, rex_XContentCreateWrapper_hook);

// ContentTask::OnDeviceSelected: BD's save slot enumeration.
// Replace the XAM enumerator with the host save store: synthesize one
// XCONTENT_DATA per slot dir (szFileName = the dir name the game created the
// slot with). Display name stays empty, since BD reads the in-game slot name
// from savegame.dat (SaveDataTask::ParseSlotData), not XCONTENT_DATA. task is
// the ContentTask 'this' (engine VA).
u32 ContentTask__OnDeviceSelected_hook(u32 task) {
  if (!task)
    return 0;
  auto *ct = bd::mem::at<bd::engine::ContentTask>(task);
  if (!ct)
    return 0;

  const u32 device_id = ct->DeviceId;
  const std::vector<std::string> slots = bd::engine::ListSaveSlots();
  u32 count = 0;
  for (const std::string &slot : slots) {
    if (count >= kContentTaskMaxSlots)
      break;
    rex::kernel::XCONTENT_DATA &entry = ct->entries[count];
    std::memset(&entry, 0, sizeof(entry));
    entry.device_id = device_id;
    entry.content_type = rex::kernel::XContentType::kSavedGame;
    entry.SetFileName(slot);
    ++count;
  }
  ct->Count = count;
  return 0;
}
REX_HOOK(ContentTask__OnDeviceSelected, ContentTask__OnDeviceSelected_hook);

// ContentTask::CheckForAnySavedGame: returns whether any
// saved-game content exists, and TitleTask__Update gates the "Load Game" entry
// on it. Answer from the host save store (the XAM tree is empty). The original
// SelectedUser == PrimaryUser guard is preserved for identical timing. Do NOT
// call __imp__ (it would rescan the empty XAM tree). Same ListSaveSlots()
// source as OnDeviceSelected keeps this consistent with the enumerated slots.
u32 ContentTask__CheckForAnySavedGame_hook(u32 task) {
  if (!task)
    return 0;
  auto *ct = bd::mem::at<bd::engine::ContentTask>(task);
  if (!ct || u32(ct->SelectedUser) != u32(ct->PrimaryUser))
    return 0;
  return bd::engine::ListSaveSlots().empty() ? 0 : 1;
}
REX_HOOK(ContentTask__CheckForAnySavedGame,
         ContentTask__CheckForAnySavedGame_hook);

// bdSaveGameWriteThumbnail: its XamContentSetThumbnail call would
// re-create the XAM save tree the host store replaced, and BD never reads the
// thumbnail back (no XamContentGetThumbnail import), so skip the write. The
// stock function always returns 1, so match it and bdSaveGameWrite is
// unchanged.
u32 bdSaveGameWriteThumbnail_hook(u32 /*task*/, u32 /*filenamePtr*/) {
  return 1;
}
REX_HOOK(bdSaveGameWriteThumbnail, bdSaveGameWriteThumbnail_hook);

// Midasm hook body, name externed by the generated dispatch.
// Camp_IsSaveAvailable answers "is an in-range save point present?". Camp's top
// menu gates entry 12's enabled flag on it (what makes A deny) and paints btn11
// DisableColor, and nothing past that re-checks.
REX_MIDASM_BRANCH(bdCampSaveAnywhereHook, ()) {
  return bd::cvars::Get().save_anywhere.Get();
}
REX_MIDASM_AT(bdCampSaveAnywhereHook, 0x822D7640, before, 0x822D7694);
