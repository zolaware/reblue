#pragma once
#include <rex/logging.h>

REXLOG_DEFINE_CATEGORY(bd)

#define BD_TRACE(...)    REXLOG_CAT_TRACE(::rex::log::bd(), __VA_ARGS__)
#define BD_DEBUG(...)    REXLOG_CAT_DEBUG(::rex::log::bd(), __VA_ARGS__)
#define BD_INFO(...)     REXLOG_CAT_INFO(::rex::log::bd(), __VA_ARGS__)
#define BD_WARN(...)     REXLOG_CAT_WARN(::rex::log::bd(), __VA_ARGS__)
#define BD_ERROR(...)    REXLOG_CAT_ERROR(::rex::log::bd(), __VA_ARGS__)
#define BD_CRITICAL(...) REXLOG_CAT_CRITICAL(::rex::log::bd(), __VA_ARGS__)
