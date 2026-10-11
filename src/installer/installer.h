/**
 * @file    installer/installer.h
 * @brief   The install registry is always available. The extractor and its
 *          wizard exist only in a REBLUE_BUILD_INSTALLER build.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "installer/boot_languages.h"
#include "installer/install_registry.h"
#include "installer/self_install.h"

#ifdef REBLUE_BUILD_INSTALLER
#include "installer/disc_install.h"
#include "installer/installer_wizard.h"
#include "installer/upgrade_prompt.h"
#endif
