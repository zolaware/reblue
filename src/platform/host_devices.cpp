/**
 * @file    platform/host_devices.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/host_devices.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

namespace bd::platform {

namespace {

constexpr Uint16 kValveVendor = 0x28DE;
constexpr Uint16 kSteamDeckProduct = 0x1205;

PadBrand BrandOf(SDL_JoystickID id) {
  if (SDL_GetGamepadVendorForID(id) == kValveVendor &&
      SDL_GetGamepadProductForID(id) == kSteamDeckProduct)
    return PadBrand::SteamDeck;

  switch (SDL_GetGamepadTypeForID(id)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    return PadBrand::Xbox360;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    return PadBrand::XboxSeries;
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return PadBrand::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return PadBrand::Switch;
  default:
    return PadBrand::Unknown;
  }
}

} // namespace

PadBrand ConnectedPad() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  if (!ids)
    return PadBrand::Unknown;
  const PadBrand brand = count > 0 ? BrandOf(ids[0]) : PadBrand::Unknown;
  SDL_free(ids);
  return brand;
}

} // namespace bd::platform
