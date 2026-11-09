/**
 * @file    engine/sfx.h
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */

#pragma once

#include <rex/ppc/hook.h>
#include <rex/types.h>

REX_IMPORT(__imp__bdPlaySoundEffect, PlaySoundEffect, u32(u32));

namespace bd::engine::sfx {

	inline constexpr u32 kOpen = 0;
	inline constexpr u32 kCancel = 1;
	inline constexpr u32 kDisabled = 2;
	inline constexpr u32 kCursor = 3;
	inline constexpr u32 kToggle = 4;

	inline void Play(u32 id) {
		PlaySoundEffect(id);
	}

} // namespace bd::engine::sfx

// Alias for global lookup
namespace sfx = bd::engine::sfx;