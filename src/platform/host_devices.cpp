/**
 * @file    platform/host_devices.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/host_devices.h"

#include <rex/hid/input_driver.h>
#include <rex/types.h>

#include "platform/hid.h"

namespace bd::platform {

namespace {

constexpr u16 kValveVendor = 0x28DE;
constexpr u16 kSteamDeckProduct = 0x1205;

PadBrand BrandOf(const rex::hid::PadInfo &pad) {
  if (pad.vendor == kValveVendor && pad.product == kSteamDeckProduct)
    return PadBrand::SteamDeck;

  using rex::hid::PadType;
  switch (pad.type) {
  case PadType::kX360:
    return PadBrand::Xbox360;
  case PadType::kXOne:
    return PadBrand::XboxSeries;
  case PadType::kPS3:
  case PadType::kPS4:
  case PadType::kPS5:
    return PadBrand::PlayStation;
  case PadType::kSwitchPro:
  case PadType::kSwitchJoyCon:
    return PadBrand::Switch;
  default:
    return PadBrand::Unknown;
  }
}

} // namespace

PadBrand ConnectedPad() {
  const auto pad = HID().GetPadInfo(0);
  return pad ? BrandOf(*pad) : PadBrand::Unknown;
}

} // namespace bd::platform
