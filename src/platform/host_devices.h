/**
 * @file    platform/host_devices.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

namespace bd::platform {

enum class PadBrand {
  Unknown,
  Xbox360,
  XboxSeries,
  PlayStation,
  Switch,
  SteamDeck,
};

PadBrand ConnectedPad();

} // namespace bd::platform
