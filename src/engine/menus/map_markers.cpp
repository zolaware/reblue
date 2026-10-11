/**
 * @file    engine/menus/map_markers.cpp
 * @brief   Which sheet cell a gimmick draws from, how it pulses, and the quad
 *          batch it goes out through.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/menus/map_markers.h"
#include <chrono>
#include <cmath>

#include "engine/frame_clock.h"

namespace bd::engine {

IconSheet g_targetSheet{{"d2anime\\res\\", {"wrp_target"}}, 128, 128};
IconSheet g_target03Sheet{{"d2anime\\res\\", {"wrp_target03"}}, 128, 128};

IconSheet *const kIconSheets[2] = {&g_targetSheet, &g_target03Sheet};

    namespace {

        // Cells of the two sheets, as d2anime\uv.csv names them.
constexpr UVRect kUvWrpPnt = {0.5f, 0.0f, 0.75f, 0.25f};
constexpr UVRect kUvWrpPntGlow = {0.75f, 0.0f, 1.0f, 0.25f};
constexpr UVRect kUvWrpHatenaLarge = {0.0f, 0.0f, 0.5f, 0.5f};
constexpr UVRect kUvWrpHatenaSmall = {0.0f, 0.5f, 0.5f, 1.0f};

        constexpr u32 kRgbMask = 0x00FFFFFFu;
        // A quad below this costs a draw and shows nothing.
        constexpr u32 kFaintAlpha = 10;
        constexpr float kPi = 3.14159265f;

        // 42 ticks is 1.4 seconds at 30Hz. The two spreads turn a marker's world
        // position into its place in that period, so a field of them ripples rather
        // than blinking in lockstep.
        constexpr float kPulsePeriodSeconds = 1.4f;
        constexpr float kPulsePeriodTicks = 42.0f;
        constexpr float kPulseSpreadX = 0.013f;
        constexpr float kPulseSpreadZ = 0.019f;

        float CurrentPulseBasePhase() {
            const u64 ticks = TickCount();
            if (ticks > 0) {
                return (static_cast<float>(ticks) + Alpha()) / kPulsePeriodTicks;
            }

            // Fallback if TickCount() is frozen (e.g. at fixed 30Hz): use steady_clock
            using Clock = std::chrono::steady_clock;
            static const auto kStartTime = Clock::now();
            const float elapsed = std::chrono::duration<float>(Clock::now() - kStartTime).count();
            return elapsed / kPulsePeriodSeconds;
        }

    } // namespace

    void QuadWriter::Draw(float cx, float cy, float halfW, float halfH, bool turned,
        u32 color) {
        const float c = turned ? kSqrtHalf : 1.0f;
        const float s = turned ? kSqrtHalf : 0.0f;
  const float xs[4] = {-halfW, -halfW, halfW, halfW};
  const float ys[4] = {-halfH, halfH, halfH, -halfH};
        const u32 a = ((color >> 24) * alpha) / 255u;
        const u32 shaded = (a << 24) | (color & kRgbMask);

        PrimBegin(kTexturedQuad2D, z, 0, 0);
        PrimSetTexture(0, texture);
        for (int i = 0; i < 4; ++i)
            PrimPushVertex2D(cx + xs[i] * c - ys[i] * s, cy + xs[i] * s + ys[i] * c,
                kSolidU, kSolidV, 0, 0, 0, 0, shaded);
        PrimEnd();
        if (z - kMarkerZStep > zFloor)
            z -= kMarkerZStep;
    }

    void QuadWriter::DrawIcon(u32 tex, float cx, float cy, float halfW, float halfH,
                          const UVRect &uv, u32 color) {
        const u32 a = ((color >> 24) * alpha) / 255u;
        const u32 shaded = (a << 24) | (color & kRgbMask);

        PrimBegin(kTexturedQuad2D, z, 0, 0);
        PrimSetTexture(0, tex);
        PrimPushVertex2D(cx - halfW, cy - halfH, uv.u0, uv.v0, 0, 0, 0, 0, shaded);
        PrimPushVertex2D(cx - halfW, cy + halfH, uv.u0, uv.v1, 0, 0, 0, 0, shaded);
        PrimPushVertex2D(cx + halfW, cy + halfH, uv.u1, uv.v1, 0, 0, 0, 0, shaded);
        PrimPushVertex2D(cx + halfW, cy - halfH, uv.u1, uv.v0, 0, 0, 0, 0, shaded);
        PrimEnd();
        if (z - kMarkerZStep > zFloor)
            z -= kMarkerZStep;
    }

    u32 DotColor(GimmickKind kind) {
        switch (kind) {
        case GimmickKind::Item:
            return kDotItem;
        case GimmickKind::Gold:
            return kDotGold;
        case GimmickKind::Medal:
            return kDotMedal;
        case GimmickKind::Param:
            return kDotParam;
        case GimmickKind::Heal:
            return kDotHeal;
        case GimmickKind::Grass:
            return kDotGrass;
        case GimmickKind::Barrier:
            return kDotBarrier;
        default:
            return kDotPlain;
        }
    }

    float AsymmetricPulse(float phase) {
        float u = std::fmod(phase, 1.0f);
        if (u < 0.0f)
            u += 1.0f;

        constexpr float kRiseEnd = 0.18f;
        constexpr float kFallEnd = 0.28f;

        if (u < kRiseEnd)
            return 0.5f - 0.5f * std::cos(u / kRiseEnd * kPi);
        if (u < kFallEnd)
            return 0.5f + 0.5f * std::cos((u - kRiseEnd) / (kFallEnd - kRiseEnd) * kPi);
        return 0.0f;
    }

    float MarkerPulse(const Marker &mk) {
        const float phase = CurrentPulseBasePhase();
        return AsymmetricPulse(phase + mk.x * kPulseSpreadX + mk.z * kPulseSpreadZ);
    }

    void DrawMarkerShape(QuadWriter &out, GimmickKind kind, float x, float y,
        float pulse, float halfSize) {
        const u32 flashAlpha = static_cast<u32>(pulse * 255.0f);
        const u32 baseAlpha = static_cast<u32>((1.0f - pulse) * 255.0f);
        const u32 white = kOpaqueWhite & kRgbMask;

        if (kind == GimmickKind::Chest) {
            if (const u32 tex = g_targetSheet.list.Texture(0)) {
                out.DrawIcon(tex, x, y, halfSize, halfSize, kUvWrpPnt);
                // The glow rides over the colored pip in white, so the flash reads as a
                // shine rather than a brighter chest.
                if (flashAlpha > kFaintAlpha)
                    out.DrawIcon(tex, x, y, halfSize, halfSize, kUvWrpPntGlow,
                        (flashAlpha << 24) | white);
                return;
            }
  } else if (const u32 tex = g_target03Sheet.list.Texture(0)) {
            const u32 baseColor = DotColor(kind) & kRgbMask;
            if (baseAlpha > kFaintAlpha)
                out.DrawIcon(tex, x, y, halfSize, halfSize, kUvWrpHatenaSmall,
                    (baseAlpha << 24) | baseColor);
            if (flashAlpha > kFaintAlpha)
                out.DrawIcon(tex, x, y, halfSize, halfSize, kUvWrpHatenaLarge,
                    (flashAlpha << 24) | white);
            return;
        }

        // Neither sheet has resolved yet, so the plain dot stands in for the badge.
        const u32 color = kind == GimmickKind::Chest ? kChestLid : DotColor(kind);
        out.Draw(x, y, halfSize * 0.6f, halfSize * 0.6f, false, color);
        out.Draw(x, y, halfSize * 0.6f, halfSize * 0.6f, true, color);
    }

bool MarkerVisible(const Marker &mk) { return mk.trackable && !mk.collected; }

} // namespace bd::engine