/**
 * @file    platform/hid.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/input_keys.h>
#include <rex/hid/input_system.h>
#include <rex/types.h>

namespace bd::platform {

inline constexpr u8 kModShift = static_cast<u8>(rex::KeyModifiers::kShift);
inline constexpr u8 kModCtrl = static_cast<u8>(rex::KeyModifiers::kCtrl);
inline constexpr u8 kModAlt = static_cast<u8>(rex::KeyModifiers::kAlt);

// The app's input system. Set in OnInit before any reader runs.
rex::hid::IInputSystem &HID();
void SetInput(rex::hid::IInputSystem *input);

// The game draws its own pointer, and mouse look holds it. Look wins.
void SetGameCursorActive(bool active);
void SetLookActive(bool active);

} // namespace bd::platform
