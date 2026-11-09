/**
 * @file    platform/host_ui.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string_view>

#include <rex/ui/user_interface.h>

namespace bd::platform {

rex::ui::IUserInterface *UI();
void SetUI(rex::ui::IUserInterface *ui);
void ShowFatal(std::string_view title, std::string_view body);

} // namespace bd::platform
