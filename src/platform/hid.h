/**
 * @file    platform/hid.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/hid.h>
#include <rex/types.h>
#include <rex/ui/ui_event.h>

namespace bd::platform {

inline constexpr u8 kModShift = static_cast<u8>(rex::hid::KeyModifiers::kShift);
inline constexpr u8 kModCtrl = static_cast<u8>(rex::hid::KeyModifiers::kCtrl);
inline constexpr u8 kModAlt = static_cast<u8>(rex::hid::KeyModifiers::kAlt);

// The hid module's backend. Set in OnInit before any reader runs.
rex::hid::IHIDBackend &HID();
void SetHID(rex::hid::IHIDBackend *backend);

// The game draws its own pointer, and mouse look holds it. Look wins.
void SetGameCursorActive(bool active);
void SetLookActive(bool active);

rex::hid::MouseButton ToHIDButton(rex::ui::MouseEvent::Button button);

} // namespace bd::platform
