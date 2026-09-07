/**
 * @file    core/logging.h
 * @brief   BD_* logging macros bound to the bd log category.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once
#include <rex/logging.h>

#include "core/settings.h"

REXLOG_DEFINE_CATEGORY(bd)

#define BD_TRACE(...) REXLOG_CAT_TRACE(::rex::log::bd(), __VA_ARGS__)
#define BD_DEBUG(...) REXLOG_CAT_DEBUG(::rex::log::bd(), __VA_ARGS__)
#define BD_INFO(...) REXLOG_CAT_INFO(::rex::log::bd(), __VA_ARGS__)
#define BD_WARN(...) REXLOG_CAT_WARN(::rex::log::bd(), __VA_ARGS__)
#define BD_ERROR(...) REXLOG_CAT_ERROR(::rex::log::bd(), __VA_ARGS__)
#define BD_CRITICAL(...) REXLOG_CAT_CRITICAL(::rex::log::bd(), __VA_ARGS__)

#define BD_DEV_WARN(...)                                                       \
  do {                                                                         \
    if (::bd::Settings::Get().Devmode())                                       \
      BD_WARN(__VA_ARGS__);                                                    \
  } while (0)

#define BD_DEV_INFO(...)                                                       \
  do {                                                                         \
    if (::bd::Settings::Get().Devmode())                                       \
      BD_INFO(__VA_ARGS__);                                                    \
  } while (0)
