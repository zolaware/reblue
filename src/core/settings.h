/**
 * @file    core/settings.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <string>

#include <rex/types.h>

namespace bd {

enum class UpdateChannel : i32 { Stable, Nightly };

const char *ToString(UpdateChannel channel);

UpdateChannel GetUpdateChannel();

std::string GetUpdateURL();

} // namespace bd
