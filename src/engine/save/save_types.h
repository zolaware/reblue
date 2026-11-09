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

#include <rex/kernel/types/content.h>
#include <rex/types.h>

namespace bd::engine {

// The config tail of a save block, so the struct starts at SaveBlock_t::config
// rather than at the block itself.
struct SaveConfigBlock_t {
  /* 0x00 */ be_i32 msgSpeed;
  /* 0x04 */ be_i32 msgSize;
  /* 0x08 */ be_i32 ruby;
  /* 0x0C */ be_i32 subtitles;
  /* 0x10 */ be_i32 battleHints;
  /* 0x14 */ be_i32 skipEvents;
  /* 0x18 */ be_i32 targetFirst;
  /* 0x1C */ be_i32 camera;
  /* 0x20 */ be_f32 musicVolume;
  /* 0x24 */ be_f32 seVolume;
  /* 0x28 */ be_f32 voiceVolume;
  /* 0x2C */ be_i32 opaque;
  /* 0x30 */ be_f32 brightness;
  /* 0x34 */ be_f32 screenPosX;
  /* 0x38 */ be_f32 screenPosY;
};
static_assert(sizeof(SaveConfigBlock_t) == 0x3C);
static_assert(offsetof(SaveConfigBlock_t, targetFirst) == 0x18);
static_assert(offsetof(SaveConfigBlock_t, camera) == 0x1C);
static_assert(offsetof(SaveConfigBlock_t, opaque) == 0x2C);
static_assert(offsetof(SaveConfigBlock_t, brightness) == 0x30);

// The fields bdSaveBlockRestoreConfig reads from a block. fltVoice is what it
// copies into g_cfgFltVoice, the audio hints global.
struct SaveBlock_t {
  /* 0x0000 */ u8 _pad0000[0x9F60];
  /* 0x9F60 */ be_i32 fltVoice;
  /* 0x9F64 */ u8 _pad9F64[0xAF4C - 0x9F64];
  /* 0xAF4C */ SaveConfigBlock_t config;
  /* 0xAF88 */ u8 _padAF88[0xB1AC - 0xAF88];
  /* 0xB1AC */ be_i32 ctlNormalType;
  /* 0xB1B0 */ be_i32 ctlMechattType;
};
static_assert(offsetof(SaveBlock_t, fltVoice) == 0x9F60);
static_assert(offsetof(SaveBlock_t, config) == 0xAF4C);
static_assert(offsetof(SaveBlock_t, ctlNormalType) == 0xB1AC);
static_assert(offsetof(SaveBlock_t, ctlMechattType) == 0xB1B0);

// ContentTask. OnDeviceSelected fills entries[]/Count, and
// CheckForAnySavedGame reads the PrimaryUser/SelectedUser guard. Two save
// blocks follow. The restore reads the second only when blockSelector is set,
// and bdSaveBlockCapture always writes the second.
struct ContentTask {
  /* 0x00000 */ u8 _pad00000[0xB4];
  /* 0x000B4 */ be_u32 PrimaryUser; // primary controller user index
  /* 0x000B8 */ u8 _pad000B8[0x04];
  /* 0x000BC */ be_u32 SelectedUser; // == PrimaryUser once signed in
  /* 0x000C0 */ u8 _pad000C0[0xDC - 0xC0];
  /* 0x000DC */ be_u32 DeviceId; // device the enumerator used
  /* 0x000E0 */ u8 _pad000E0[0x118 - 0xE0];
  /* 0x00118 */ rex::kernel::XCONTENT_DATA entries[30];
  /* 0x02530 */ be_u32 Count;
  /* 0x02534 */ u8 _pad02534[0x04];
  /* 0x02538 */ SaveBlock_t readBlock;
  /* 0x0D6EC */ u8 _pad0D6EC[0xD7A8 - 0x2538 - sizeof(SaveBlock_t)];
  /* 0x0D7A8 */ SaveBlock_t writeBlock;
  /* 0x1895C */ u8 _pad1895C[0x18A20 - 0xD7A8 - sizeof(SaveBlock_t)];
  /* 0x18A20 */ be_i32 blockSelector;
};
static_assert(offsetof(ContentTask, PrimaryUser) == 0xB4);
static_assert(offsetof(ContentTask, SelectedUser) == 0xBC);
static_assert(offsetof(ContentTask, DeviceId) == 0xDC);
static_assert(offsetof(ContentTask, entries) == 0x118);
static_assert(offsetof(ContentTask, Count) == 0x2530);
static_assert(offsetof(ContentTask, readBlock) == 0x2538);
static_assert(offsetof(ContentTask, writeBlock) == 0xD7A8);
static_assert(offsetof(ContentTask, blockSelector) == 0x18A20);

} // namespace bd::engine
