/**
 * @file    core/logging.h
 * @brief   The bd log category.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once
#include <rex/logging/log_category.h>

REXLOG_CATEGORY(bd, "reblue title code")
REXLOG_TABLE(kReblueLog, bd)

#define REXBD_TRACE(fmt, ...) REXLOG_CAT_TRACE(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_DEV(fmt, ...) REXLOG_CAT_DEV(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_DEBUG(fmt, ...) REXLOG_CAT_DEBUG(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_INFO(fmt, ...) REXLOG_CAT_INFO(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_WARN(fmt, ...) REXLOG_CAT_WARN(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_ERROR(fmt, ...) REXLOG_CAT_ERROR(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
#define REXBD_CRITICAL(fmt, ...) \
  REXLOG_CAT_CRITICAL(::rex::log::bd, fmt __VA_OPT__(, ) __VA_ARGS__)
