/**
 * @file    engine/save/save_types.h
 * @brief   Engine memory layouts (be<>) for the X360 content/save structs the
 *          save hooks read and write.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

namespace bd::engine {

// X360 XCONTENT_DATA (sizeof 0x134 = 308).
struct XContentData {
  /* 0x000 */ be_u32 DeviceID;
  /* 0x004 */ be_u32 dwContentType;  // XCONTENTTYPE_*
  /* 0x008 */ u8 szDisplayName[256]; // wchar[128]
  /* 0x108 */ char szFileName[42];
  /* 0x132 */ u8 _pad132[2];
};
static_assert(sizeof(XContentData) == 0x134);
static_assert(offsetof(XContentData, dwContentType) == 0x004);
static_assert(offsetof(XContentData, szFileName) == 0x108);

// ContentTask save enumeration view. ContentTask::OnDeviceSelected fills
// entries[]/Count, and CheckForAnySavedGame reads the PrimaryUser/SelectedUser
// guard. The real task struct is larger (modeled only to Count).
struct ContentTask {
  /* 0x000 */ u8 _pad000[180];
  /* 0x0B4 */ be_u32 PrimaryUser; // primary controller user index
  /* 0x0B8 */ u8 _pad0B8[4];
  /* 0x0BC */ be_u32 SelectedUser; // == PrimaryUser once signed in
  /* 0x0C0 */ u8 _pad0C0[220 - 192];
  /* 0x0DC */ be_u32 DeviceId; // device the enumerator used
  /* 0x0E0 */ u8 _pad0E0[280 - 224];
  /* 0x118 */ XContentData entries[30];
  /* 0x2530 */ be_u32 Count;
};
static_assert(offsetof(ContentTask, PrimaryUser) == 180);
static_assert(offsetof(ContentTask, SelectedUser) == 188);
static_assert(offsetof(ContentTask, DeviceId) == 220);
static_assert(offsetof(ContentTask, entries) == 280);
static_assert(offsetof(ContentTask, Count) == 9520);

} // namespace bd::engine
