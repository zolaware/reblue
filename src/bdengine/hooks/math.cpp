/**
 * @file    bdengine/hooks/math.cpp
 * @brief   CRT math replacements - native x86 instead of recompiled PPC.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include <cmath>
#include <cstdlib>

#include <rex/ppc.h>

static double native_atan2(double y, double x) { return std::atan2(y, x); }
static double native_atof(const char* s) { return std::atof(s); }

PPC_HOOK(rex_atan2, native_atan2);
PPC_HOOK(rex_atof, native_atof);
