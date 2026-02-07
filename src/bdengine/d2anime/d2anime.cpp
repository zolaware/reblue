/**
 * @file        bdengine/d2anime.cpp
 * @brief       D2AnimeScreen framework -- sequence registration, factory,
 *              menu update, and cancel-input hooks.
 *
 *              Provides a base class for custom d2anime UI screens that
 *              plug into the game's sequence system. Each registered screen
 *              gets its own sequence ID, factory thunk, and automatic
 *              lifecycle management (create, update, destroy).
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/d2anime/d2anime.h"
#include "bdengine/d2anime/d2anime_types.h"
#include "bdengine/common/logging.h"

#include <cstring>
#include <unordered_map>
#include <vector>

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

REX_IMPORT(__imp__SequenceHolder_SequenceUnit_ctor, SeqUnit_ctor, void(u32, u32, u32));
REX_IMPORT(__imp__SequenceHolder_RegisterSequence, SeqRegister, u32(u32, u32));
REX_IMPORT(__imp__SequenceHolder_SequenceUnitBase_dtor, SeqUnit_dtor, void(u32));
REX_IMPORT(__imp__SequenceHolder_FindSequenceByName, FindSeqByName, u32(u32, u32));
REX_IMPORT(__imp__LH_Binary__LoadAsync, LoadAsync, u32(u32, u32, u32));
REX_IMPORT(__imp__D2AnimeTask_SetVisibleAndPlay, SetVisibleAndPlay, void(u32, u32));
REX_IMPORT(__imp__bdInputCheckButton, InputCheckButton, u32(u32, u32, u32));
REX_IMPORT(__imp__AnimeMenu_CheckConfirmInput, CheckConfirmInput_Guest, u32(u32));
REX_IMPORT(__imp__AnimeMenu_CheckCancelInput, CheckCancelInput_Guest, u32(u32));
REX_IMPORT(__imp__AnimeVarBag_SetStringVar, VarBag_SetString,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetColorRGBA, VarBag_SetColor,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetFloatVar, VarBag_SetFloat,
           void(u32, u32, f64));

REX_EXTERN(__imp__bdGameTasksInit);
REX_EXTERN(__imp__AnimeMenu_Update);

static constexpr u32 kSequenceControlAddr = 0x827A7EC0;
static constexpr size_t   kMaxScreens          = 16;

static std::vector<bd::D2AnimeScreen*>& ScreenRegistry() {
    static std::vector<bd::D2AnimeScreen*> s_screens;
    return s_screens;
}

static bd::D2AnimeScreen* s_slots[kMaxScreens] = {};

/**
 * @brief Slot-based trampoline functions.
 *
 *        AllocateThunk requires a plain PPCFunc* with no captures, so each
 *        slot gets a distinct static function that forwards to the screen
 *        stored in s_slots[N].
 */
namespace bd {
    void detail_ScreenFactory(D2AnimeScreen* screen, PPCContext& ctx, u8* base);
}

#define DEFINE_SLOT_TRAMPOLINE(N) \
    static void ScreenTrampoline_##N(PPCContext& c, u8* b) { \
        if (s_slots[N]) bd::detail_ScreenFactory(s_slots[N], c, b); \
    }

DEFINE_SLOT_TRAMPOLINE(0)
DEFINE_SLOT_TRAMPOLINE(1)
DEFINE_SLOT_TRAMPOLINE(2)
DEFINE_SLOT_TRAMPOLINE(3)
DEFINE_SLOT_TRAMPOLINE(4)
DEFINE_SLOT_TRAMPOLINE(5)
DEFINE_SLOT_TRAMPOLINE(6)
DEFINE_SLOT_TRAMPOLINE(7)
DEFINE_SLOT_TRAMPOLINE(8)
DEFINE_SLOT_TRAMPOLINE(9)
DEFINE_SLOT_TRAMPOLINE(10)
DEFINE_SLOT_TRAMPOLINE(11)
DEFINE_SLOT_TRAMPOLINE(12)
DEFINE_SLOT_TRAMPOLINE(13)
DEFINE_SLOT_TRAMPOLINE(14)
DEFINE_SLOT_TRAMPOLINE(15)

#undef DEFINE_SLOT_TRAMPOLINE

static PPCFunc* s_trampolines[kMaxScreens] = {
    &ScreenTrampoline_0,  &ScreenTrampoline_1,  &ScreenTrampoline_2,
    &ScreenTrampoline_3,  &ScreenTrampoline_4,  &ScreenTrampoline_5,
    &ScreenTrampoline_6,  &ScreenTrampoline_7,  &ScreenTrampoline_8,
    &ScreenTrampoline_9,  &ScreenTrampoline_10, &ScreenTrampoline_11,
    &ScreenTrampoline_12, &ScreenTrampoline_13, &ScreenTrampoline_14,
    &ScreenTrampoline_15,
};

namespace bd {

D2AnimeScreen* FindScreenByMenu(u32 menuAddr) {
    for (auto* s : ScreenRegistry()) {
        if (s->active_ && s->menu_addr_ != 0 && s->menu_addr_ == menuAddr)
            return s;
    }
    return nullptr;
}

D2AnimeScreen* FindScreenByTask(u32 taskAddr) {
    for (auto* s : ScreenRegistry()) {
        if (s->active_ && s->task_addr_ == taskAddr)
            return s;
    }
    return nullptr;
}

D2AnimeScreen* FindScreenAwaitingMenu() {
    for (auto* s : ScreenRegistry()) {
        if (s->active_ && s->menu_addr_ == 0)
            return s;
    }
    return nullptr;
}

D2AnimeScreen::D2AnimeScreen(const std::string& name, const std::string& csvPath)
    : name_(name), csv_path_(csvPath) {}

void D2AnimeScreen::TransitionTo(u32 seqId) {
    auto* ks = rex::system::kernel_state();
    auto* base = ks->memory()->virtual_membase();

    u32 seqCtrl = rex::memory::load_and_swap<u32>(base + kSequenceControlAddr);
    if (!seqCtrl) {
        BD_ERROR("[d2anime] TransitionTo: g_pSequenceControl is NULL");
        return;
    }

    rex::memory::store_and_swap<u32>(base + seqCtrl + 0x70, seqId);

    if (task_addr_) {
        auto* task = reinterpret_cast<D2AnimeTask_t*>(base + task_addr_);
        task->flags = static_cast<u32>(task->flags) | 0xDEAD0000;
        task->destroyFlag = 1;
    }

    BD_INFO("[d2anime] '{}' transitioning to seqId={}, task DEAD-flagged",
            name_, seqId);

    active_ = false;
    task_addr_ = 0;
    menu_addr_ = 0;
}

/**
 * @brief Transition to a sequence by name.
 * @note Only safe to call from inside a hook where ctx is live. It creates a
 *       temporary PPCContext for the FindSeqByName call, borrowing the
 *       caller's guest stack (r1). The caller must pass the live ctx through
 *       the base pointer chain.
 */
void D2AnimeScreen::TransitionTo(const std::string& seqName) {
    for (auto* s : ScreenRegistry()) {
        if (s->name_ == seqName && s->seq_id_ != 0) {
            TransitionTo(s->seq_id_);
            return;
        }
    }

    // "Title" is cached on every screen
    if (seqName == "Title" && title_seq_id_ != 0) {
        TransitionTo(title_seq_id_);
        return;
    }

    // General case: look up via guest FindSequenceByName
    auto* ks = rex::system::kernel_state();
    auto* base = ks->memory()->virtual_membase();
    auto* ctx = rex::runtime::current_ppc_context();

    u32 seqCtrl = rex::memory::load_and_swap<u32>(base + kSequenceControlAddr);
    if (seqCtrl) {
        rex::ppc::stack_guard guard(*ctx);
        u32 nameAddr = rex::ppc::stack_push_string(*ctx, base, seqName.c_str());
        u32 seqId = FindSeqByName(seqCtrl, nameAddr);
        if (seqId) {
            TransitionTo(seqId);
            return;
        }
    }

    BD_ERROR("[d2anime] TransitionTo: sequence '{}' not found", seqName);
}

void RegisterScreen(D2AnimeScreen* screen) {
    ScreenRegistry().push_back(screen);
}

D2AnimeScreen* FindScreen(const std::string& name) {
    for (auto* s : ScreenRegistry()) {
        if (s->GetName() == name)
            return s;
    }
    return nullptr;
}

static constexpr u32 kInputManagerAddr = 0x82DC9844;

bool CheckButton(Button btn) {
    auto* base = rex::system::kernel_state()->memory()->virtual_membase();
    u32 inputMgr = rex::memory::load_and_swap<u32>(
        base + kInputManagerAddr);
    if (!inputMgr) return false;
    return InputCheckButton(inputMgr, 0, static_cast<u32>(btn)) != 0;
}

bool CheckConfirmInput(u32 menuAddr) {
    return CheckConfirmInput_Guest(menuAddr) != 0;
}

bool CheckCancelInput(u32 menuAddr) {
    return CheckCancelInput_Guest(menuAddr) != 0;
}

void VarBagSetString(u32 varBag, const char* varName, const char* value) {
    rex::ppc::stack_guard guard;
    u32 name = rex::ppc::stack_push_string(varName);
    u32 val  = rex::ppc::stack_push_string(value);
    VarBag_SetString(varBag, name, val);
}

void VarBagSetColor(u32 varBag, const char* varName, u32 rgba) {
    rex::ppc::stack_guard guard;
    u32 name = rex::ppc::stack_push_string(varName);
    VarBag_SetColor(varBag, name, rgba);
}

void VarBagSetFloat(u32 varBag, const char* varName, double value) {
    rex::ppc::stack_guard guard;
    u32 name = rex::ppc::stack_push_string(varName);
    VarBag_SetFloat(varBag, name, value);
}

/**
 * @brief Called via slot trampoline per-screen.
 */
void detail_ScreenFactory(D2AnimeScreen* screen, PPCContext& ctx, u8* base) {
    u32 parent = ctx.r3.u32;

    screen->OnCreate(parent);

    // Write CSV path onto the guest stack and call LoadAsync
    rex::ppc::stack_guard guard(ctx);
    u32 csvAddr = rex::ppc::stack_push_string(ctx, base, screen->csv_path_.c_str());

    u32 taskAddr = LoadAsync(parent, csvAddr, 0);

    if (!taskAddr) {
        BD_ERROR("[d2anime] factory '{}': LoadAsync returned NULL", screen->name_);
        ctx.r3.u64 = 0;
        return;
    }

    // Disable looping so the task doesn't auto-restart
    auto* task = reinterpret_cast<D2AnimeTask_t*>(base + taskAddr);
    task->loopFlag = 0;

    SetVisibleAndPlay(taskAddr, 1);

    screen->task_addr_ = taskAddr;
    screen->active_ = true;
    screen->menu_addr_ = 0;

    BD_INFO("[d2anime] factory '{}': task at 0x{:08X}", screen->name_, taskAddr);

    ctx.r3.u32 = taskAddr;
}

/**
 * @brief Register all screens; called from bdGameTasksInit hook.
 */
void detail_RegisterAllScreens(PPCContext& ctx, u8* base) {
    auto& screens = ScreenRegistry();
    if (screens.empty()) return;

    u32 seqCtrl = rex::memory::load_and_swap<u32>(base + kSequenceControlAddr);
    if (!seqCtrl) {
        BD_ERROR("[d2anime] g_pSequenceControl is NULL after bdGameTasksInit");
        return;
    }
    u32 holder = rex::memory::load_and_swap<u32>(base + seqCtrl + 0x6C);

    auto* ks = rex::system::kernel_state();
    auto* dispatcher = ks->function_dispatcher();

    // Cache "Title" sequence ID (shared by all screens for B-exit)
    u32 titleSeqId = 0;
    {
        rex::ppc::stack_guard guard(ctx);
        u32 nameAddr = rex::ppc::stack_push_string(ctx, base, "Title");
        titleSeqId = FindSeqByName(seqCtrl, nameAddr);
    }

    if (titleSeqId) {
        BD_INFO("[d2anime] cached Title sequence ID {}", titleSeqId);
    } else {
        BD_ERROR("[d2anime] failed to find Title sequence");
    }

    if (screens.size() > kMaxScreens) {
        BD_ERROR("[d2anime] too many screens registered ({} > {})",
                 screens.size(), kMaxScreens);
    }

    for (size_t i = 0; i < screens.size() && i < kMaxScreens; ++i) {
        auto* screen = screens[i];
        if (screen->skip_sequence_) {
            BD_INFO("[d2anime] '{}' skipped sequence registration (child mode)",
                    screen->name_);
            continue;
        }
        s_slots[i] = screen;

        u32 factoryAddr = dispatcher->AllocateThunk(s_trampolines[i]);
        if (!factoryAddr) {
            BD_ERROR("[d2anime] AllocateThunk failed for '{}'", screen->name_);
            continue;
        }
        BD_INFO("[d2anime] factory thunk for '{}' at 0x{:08X}",
                screen->name_, factoryAddr);

        // Build SequenceUnit on guest stack and register it
        {
            rex::ppc::stack_guard guard(ctx);
            u8 unit_buf[0x60] = {};
            std::strcpy(reinterpret_cast<char*>(unit_buf + 0x40), screen->name_.c_str());
            u32 unitAddr = rex::ppc::stack_push(ctx, base, unit_buf, 0x60);

            SeqUnit_ctor(unitAddr, unitAddr + 0x40, factoryAddr);
            SeqRegister(holder, unitAddr);
            SeqUnit_dtor(unitAddr);
        }

        // Look up the assigned sequence ID
        {
            rex::ppc::stack_guard guard(ctx);
            u32 nameAddr = rex::ppc::stack_push_string(ctx, base, screen->name_.c_str());
            screen->seq_id_ = FindSeqByName(seqCtrl, nameAddr);
        }

        screen->title_seq_id_ = titleSeqId;

        if (screen->seq_id_) {
            BD_INFO("[d2anime] registered sequence '{}' with ID {}",
                    screen->name_, screen->seq_id_);
        } else {
            BD_ERROR("[d2anime] failed to find sequence '{}' after registration",
                     screen->name_);
        }
    }
}

/**
 * @brief Dispatches OnUpdate to matching screen.
 */
void detail_OnMenuUpdate(u32 menuAddr, PPCContext& ctx, u8* base) {
    // Menu discovery: if a screen is active with no menu yet, capture it.
    // The engine sets flags 0x00->0x02 naturally during Init, but activeFlag
    // stays 0 until game code drives a SetVisibleAndPlay cycle. We re-trigger
    // that here - the engine then sets activeFlag=1 on the next Update.
    auto* awaiting = FindScreenAwaitingMenu();
    if (awaiting) {
        awaiting->menu_addr_ = menuAddr;
        BD_INFO("[d2anime] '{}' discovered menu at 0x{:08X}",
                awaiting->name_, menuAddr);

        // Re-trigger SetVisibleAndPlay to advance the menu to active state
        SetVisibleAndPlay(awaiting->task_addr_, 1);
    }

    // Dispatch OnUpdate to the matching screen
    auto* screen = FindScreenByMenu(menuAddr);
    if (screen) {
        screen->OnUpdate(menuAddr);
    }
}

}  // namespace bd

/**
 * @brief Register all screens after game init (bdGameTasksInit hook).
 */
REX_HOOK_RAW(bdGameTasksInit) {
    __imp__bdGameTasksInit(ctx, base);
    bd::detail_RegisterAllScreens(ctx, base);
}

/**
 * @brief Discover menus, dispatch OnUpdate (AnimeMenu_Update hook).
 */
REX_HOOK_RAW(AnimeMenu_Update) {
    u32 menuAddr = ctx.r3.u32;

    // Always run the original update (handles cursor movement, animations)
    __imp__AnimeMenu_Update(ctx, base);

    bd::detail_OnMenuUpdate(menuAddr, ctx, base);
}

