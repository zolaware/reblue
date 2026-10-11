/**
 * @file    engine/guest_prim.h
 * @brief   The engine's 2D primitive entry points, under one name each.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/hook.h>
#include <rex/types.h>

// ReXGlue numbers GPR ordinals over integer parameters only, so every register
// a float argument reserves has to be spelled out as a placeholder for the
// ones behind it to sit where the engine reads them.
//
// REX_IMPORT declares a static callable, so every translation unit that
// includes this gets its own and nothing is shared across the link.
REX_IMPORT(__imp__Visual__Prim__ctor, PrimState, u32());
REX_IMPORT(__imp__Visual__SelectRenderTarget, PrimSelectTexture, u32(u32, u32));
REX_IMPORT(__imp__bdPrimBegin, PrimBegin, void(u32, f64, u32, u32));
REX_IMPORT(__imp__bdPrimSetTexture, PrimSetTexture, u32(u32, u32));
REX_IMPORT(__imp__bdPrimPushVertex2D, PrimPushVertex2D,
           u32(f64, f64, f64, f64, u32, u32, u32, u32, u32));
REX_IMPORT(__imp__bdPrimEnd, PrimEnd, u32());
REX_IMPORT(__imp__bdPrimDrawRect2D, PrimDrawRect2D,
           void(f64, f64, f64, f64, f64, u32, u32, u32, u32, u32, u32));
REX_IMPORT(__imp__bdPrimDrawRectRotated, PrimDrawRectRotated,
           u32(f64, f64, f64, f64, f64, f64, u32, u32, u32, u32, u32, u32,
               u32));

// Where Visual__SelectRenderTarget leaves the texture it bound.
constexpr u32 kPrim_Texture = 0x24;
