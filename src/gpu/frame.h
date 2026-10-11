/**
 * @file    gpu/frame.h
 * @brief   Shared by the TUs that record one frame: the command list ring,
 *          the draw path, the EDRAM resolve emulation, and present.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

#include <plume_render_interface.h>

#include "gpu/device.h"

namespace bd::gpu {

void BeginCommandList(VideoState &s);

void DrainSlot(VideoState &s, u32 slot);

void AdvanceAndWaitReused(VideoState &s);

void SubmitOpenListLocked(VideoState &s);

void ResolveEffectiveTargets(VideoState &s, GuestTexture *&rt,
                             GuestTexture *&ds);

plume::RenderFramebuffer *GetFramebuffer(VideoState &s, GuestTexture *rt,
                                         GuestTexture *ds);

bool CopySurfaceToTextureLocked(VideoState &s, GuestTexture *src,
                                GuestTexture *dst, const char *reason);
bool MaterializeOutboundLocked(VideoState &s, GuestTexture *source,
                               bool aliasable_only = false);
void MaterializeInboundLocked(VideoState &s, GuestTexture *dst);
void DetachSourceSurfaceLocked(VideoState &s, GuestTexture *texture);
bool FullscreenChainClassLocked(const VideoState &s, const GuestTexture *t);

extern Video::OverlayDrawHook g_overlay_draw_hook;

} // namespace bd::gpu
