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
#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/xcontent.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/save/save_store.h"
#include "engine/save/save_types.h"
#include "engine/settings.h"

using rex::X_RESULT;

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
//
// Raw, on the inherited context: a typed REX_IMPORT re-roots the engine stack
// at ThreadState's r1 and overwrites the frames live underneath it.
REX_EXTERN(__imp__rex_XContentCreateWrapper);
REX_HOOK_RAW(rex_XContentCreateWrapper) {
  const auto *root = bd::mem::at<const char>(ctx.r4.u32);
  const auto *content = bd::mem::at<const bd::engine::XContentData>(ctx.r5.u32);
  const u32 flags = ctx.r6.u32;
  auto *disposition = bd::mem::at<be_u32>(ctx.r7.u32);

  if (!root || std::strncmp(root, "save", 5) != 0 || !content) {
    __imp__rex_XContentCreateWrapper(ctx, base);
    return;
  }

  std::string slot(content->szFileName,
                   ::strnlen(content->szFileName, sizeof(content->szFileName)));

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
    BD_WARN("[savehook] unexpected disposition {} for slot '{}'", requested,
            slot);
    break;
  }

  if (result == X_ERROR_SUCCESS)
    bd::engine::SetCurrentSaveSlot(slot);
  if (disposition)
    *disposition = out_disposition;
  ctx.r3.u64 = result;
}

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
    bd::engine::XContentData &entry = ct->entries[count];
    std::memset(&entry, 0, sizeof(entry));
    entry.DeviceID = device_id;
    entry.dwContentType = u32(rex::system::XContentType::kSavedGame);
    const size_t n =
        std::min<size_t>(slot.size(), sizeof(entry.szFileName) - 1);
    std::memcpy(entry.szFileName, slot.data(), n);
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
bool bdCampSaveAnywhereHook() {
  return bd::engine::Settings::Get().SaveAnywhere();
}
