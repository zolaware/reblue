/**
 * @file        bdengine/d2anime_types.h
 * @brief       Guest-memory struct definitions for d2anime objects.
 *
 *              These structs use rex::be<> to marshal big-endian PPC guest
 *              memory.  Cast via reinterpret_cast<T*>(base + guestAddr).
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <cstdint>

#include <rex/types.h>

namespace bd {

/**
 * @brief Singleton that drives screen transitions.
 *
 *        Global pointer at 0x827A7EC0. Created in bdGameTasksInit.
 */
struct SequenceControl_t {
    /* 0x000 */ be_u32 vtable;
    /* 0x004 */ u8 _pad004[0x64];
    /* 0x068 */ be_u32 activeSequence;   // Task* to running sequence, NULL = idle
    /* 0x06C */ be_u32 seqHolder;         // SequenceHolder*, set once at init
    /* 0x070 */ be_u32 requestedSeqID;    // 0 = no request, consumed by vf02
    /* 0x074 */ be_u32 directSeqPtr;      // SequenceUnit*, takes priority over ID
};
static_assert(offsetof(SequenceControl_t, activeSequence) == 0x068);
static_assert(offsetof(SequenceControl_t, seqHolder)      == 0x06C);
static_assert(offsetof(SequenceControl_t, requestedSeqID) == 0x070);
static_assert(offsetof(SequenceControl_t, directSeqPtr)   == 0x074);
static_assert(sizeof(SequenceControl_t)                    == 0x078);

/**
 * @brief Registered sequences as a vector of SequenceUnitBase*.
 *
 *        Pointed to by SequenceControl+0x6C.
 */
struct SequenceHolder_t {
    be_u32 nextID;       // +0x00 - pre-incremented counter, first ID = 1
    be_u32 vecBegin;     // +0x04 - vector<SequenceUnitBase*> begin
    be_u32 vecEnd;       // +0x08 - vector end
    be_u32 vecCap;       // +0x0C - vector capacity
};
static_assert(sizeof(SequenceHolder_t) == 0x10);

/**
 * @brief Per-item metadata in AnimeMenu's item-data vector.
 *
 *        Field +0x04 drives enable/disable color tinting.
 */
struct AnimeItemData_t {
    be_u32 index;        // +0x00 - item index
    be_u32 enabled;      // +0x04 - non-zero = enabled (EnableColor), 0 = disabled (DisableColor)
    be_u32 unk08;        // +0x08
    be_u32 customColor;  // +0x0C - guest ptr to custom color data, 0 = use defaults
};
static_assert(sizeof(AnimeItemData_t) == 0x10);

/**
 * @brief d2anime menu with cursor, templates, and item-data.
 */
struct AnimeMenu_t {
    /* 0x000 */ be_u32 vtable;
    /* 0x004 */ u8 _pad004[0x54];
    /* 0x058 */ be_u32 flags;          // bit0=animating, bit1=active/ready, bit2=locked
    /* 0x05C */ u8 _pad05C[0x38];
    /* 0x094 */ be_u32 cursorShowA;    // non-zero to show cursor sprite
    /* 0x098 */ be_u32 cursorShowB;    // non-zero to show cursor sprite
    /* 0x09C */ u8 _pad09C[0x04];
    /* 0x0A0 */ be_u32 cursorTask;     // CursorTask* - per-menu cursor sprite
    /* 0x0A4 */ be_u32 gridDimX;       // rows or cols depending on orientation
    /* 0x0A8 */ be_u32 gridDimY;       // cols or rows depending on orientation
    /* 0x0AC */ u8 _pad0AC[0x04];
    /* 0x0B0 */ be_u32 entryDataBegin; // entry-data vector begin (gates CheckConfirmInput)
    /* 0x0B4 */ be_u32 entryDataEnd;   // entry-data vector end
    /* 0x0B8 */ u8 _pad0B8[0x04];
    /* 0x0BC */ u8 _pad0BC[0x04];            // visibleItems vector base
    /* 0x0C0 */ be_u32 itemDataBegin;  // visibleItems vector begin (scroll window)
    /* 0x0C4 */ be_u32 itemDataEnd;    // visibleItems vector end
    /* 0x0C8 */ be_u32 itemDataCap;    // visibleItems vector capacity
    /* 0x0CC */ be_u32 cursorIndex;
    /* 0x0D0 */ be_u32 scrollOffset;   // u16 in practice
    /* 0x0D4 */ be_u32 orientation;    // 0=horizontal/linear, nonzero=vertical/grid
    /* 0x0D8 */ be_u32 activeFlag;
    /* 0x0DC */ u8 _pad0DC[0x0C];
    /* 0x0E8 */ be_u32 scrollbarEnabled;
    /* 0x0EC */ u8 _pad0EC[0x10];
    /* 0x0FC */ be_u32 selectAll;      // 1 = all items show EnableWndType
    /* 0x100 */ be_u32 deselectAll;    // 1 = all items show DisableWndType (hides cursor frame)
    /* 0x104 */ be_u32 hasWndType;     // 1 if templates have WndType string variable
    /* 0x108 */ u8 _pad108[0x50];
    /* 0x158 */ be_u32 templateBegin;  // vector<D2AnimeTask*> begin
    /* 0x15C */ be_u32 templateEnd;    // vector<D2AnimeTask*> end
    /* 0x160 */ be_u32 templateCap;    // vector<D2AnimeTask*> capacity
    /* 0x164 */ be_u32 enableColor;    // packed RGBA (R<<24|G<<16|B<<8|A)
    /* 0x168 */ be_u32 disableColor;   // packed RGBA
    /* 0x16C */ be_u32 needsRebuild;   // set to 1 triggers RebuildVisibleItems

    u32 templateCount() const {
        u32 b = templateBegin, e = templateEnd;
        return b ? (e - b) / 4 : 0;
    }

    u32 itemDataCount() const {
        u32 b = itemDataBegin, e = itemDataEnd;
        return b ? (e - b) / 4 : 0;
    }
};
static_assert(offsetof(AnimeMenu_t, flags)           == 0x058);
static_assert(offsetof(AnimeMenu_t, cursorShowA)     == 0x094);
static_assert(offsetof(AnimeMenu_t, cursorShowB)     == 0x098);
static_assert(offsetof(AnimeMenu_t, cursorTask)      == 0x0A0);
static_assert(offsetof(AnimeMenu_t, gridDimX)        == 0x0A4);
static_assert(offsetof(AnimeMenu_t, entryDataBegin)  == 0x0B0);
static_assert(offsetof(AnimeMenu_t, itemDataBegin)   == 0x0C0);
static_assert(offsetof(AnimeMenu_t, cursorIndex)     == 0x0CC);
static_assert(offsetof(AnimeMenu_t, orientation)     == 0x0D4);
static_assert(offsetof(AnimeMenu_t, activeFlag)      == 0x0D8);
static_assert(offsetof(AnimeMenu_t, scrollbarEnabled)== 0x0E8);
static_assert(offsetof(AnimeMenu_t, selectAll)       == 0x0FC);
static_assert(offsetof(AnimeMenu_t, deselectAll)     == 0x100);
static_assert(offsetof(AnimeMenu_t, hasWndType)      == 0x104);
static_assert(offsetof(AnimeMenu_t, templateBegin)   == 0x158);
static_assert(offsetof(AnimeMenu_t, enableColor)     == 0x164);
static_assert(offsetof(AnimeMenu_t, needsRebuild)    == 0x16C);

/**
 * @brief Loaded animation/UI task.
 */
struct D2AnimeTask_t {
    /* 0x000 */ u8 _pad000[0x58];
    /* 0x058 */ be_u32 flags;
    /* 0x05C */ u8 _pad05C[0x04];
    /* 0x060 */ be_u32 destroyFlag;
    /* 0x064 */ u8 _pad064[0x04];
    /* 0x068 */ be_u32 visible;
    /* 0x06C */ be_u32 autoPlay;       // init=1, propagated to child menu +0xD8
    /* 0x070 */ be_u32 loadState;      // 1=loading,2=error,3=children,4=ready,5=finished
    /* 0x074 */ // AnimeData embedded subobject (~450 bytes, own vtable)
    /* 0x074 */ // AnimeVarBag accessible at guest_addr + 0x74
    /* 0x074 */ u8 _animeData[0x1C4];
    /* 0x238 */ be_u32 loopFlag;       // non-zero = loop animation, 0 = one-shot then FINISHED
    /* 0x23C */ u8 _pad23C[0x08];
    /* 0x244 */ be_u32 menusBegin;     // vector<AnimeMenu*> begin
    /* 0x248 */ be_u32 menusEnd;       // vector<AnimeMenu*> end
    /* 0x24C */ be_u32 menusCap;       // vector<AnimeMenu*> capacity
    /* 0x250 */ u8 _pad250[0x10];
    /* 0x260 */ be_u32 drawDirty;      // ping-pong between Draw and PostUpdate
    /* 0x264 */ u8 _pad264[0x04];
};
static_assert(offsetof(D2AnimeTask_t, flags)      == 0x058);
static_assert(offsetof(D2AnimeTask_t, destroyFlag)== 0x060);
static_assert(offsetof(D2AnimeTask_t, visible)    == 0x068);
static_assert(offsetof(D2AnimeTask_t, autoPlay)   == 0x06C);
static_assert(offsetof(D2AnimeTask_t, loadState)  == 0x070);
static_assert(offsetof(D2AnimeTask_t, loopFlag)   == 0x238);
static_assert(offsetof(D2AnimeTask_t, menusBegin) == 0x244);
static_assert(offsetof(D2AnimeTask_t, drawDirty)  == 0x260);
static_assert(sizeof(D2AnimeTask_t)               == 0x268);

constexpr u32 kAnimeVarBagOffset = 0x74;

}  // namespace bd
