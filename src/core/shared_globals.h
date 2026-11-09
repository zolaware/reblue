/**
 * @file    core/shared_globals.h
 * @brief   Guest globals more than one file reads or writes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/runtime/guest_global.h>
#include <rex/types.h>

namespace bd::globals {

REX_GUEST_GLOBAL(be_i32, kLocaleId, 0x827A8578);

REX_GUEST_GLOBAL(be_i32, kVoiceType, 0x82DC40D8);
inline constexpr auto kYYSelector = kVoiceType;

} // namespace bd::globals
