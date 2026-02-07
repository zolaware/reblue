/**
 * @file        bdengine/d2anime.h
 * @brief       D2AnimeScreen base class - thin wrapper over the d2anime
 *              sequence system for registering custom UI screens.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/types.h>

struct PPCContext;

namespace bd {

class D2AnimeScreen {
public:
    D2AnimeScreen(const std::string& name, const std::string& csvPath);
    virtual ~D2AnimeScreen() = default;

    const std::string& GetName() const { return name_; }
    const std::string& GetCsvPath() const { return csv_path_; }
    u32 GetSeqId() const { return seq_id_; }
    u32 GetTitleSeqId() const { return title_seq_id_; }

    void ActivateAsChild(u32 taskAddr) {
        task_addr_ = taskAddr;
        active_ = true;
        menu_addr_ = 0;
    }
    void Deactivate() {
        active_ = false;
        task_addr_ = 0;
        menu_addr_ = 0;
    }

protected:
    virtual void OnCreate(u32 taskAddr) {}
    virtual void OnPreUpdate(u32 menuAddr) {}
    virtual void OnUpdate(u32 menuAddr) {}
    virtual void OnDestroy() {}

    u32 GetTaskAddr() const { return task_addr_; }
    u32 GetMenuAddr() const { return menu_addr_; }
    bool IsActive() const { return active_; }

    void TransitionTo(u32 seqId);
    void TransitionTo(const std::string& seqName);

protected:
    bool skip_sequence_ = false;

private:
    std::string name_;
    std::string csv_path_;
    u32 seq_id_ = 0;
    u32 title_seq_id_ = 0;
    u32 task_addr_ = 0;
    u32 menu_addr_ = 0;
    bool active_ = false;

    friend void detail_RegisterAllScreens(PPCContext& ctx, u8* base);
    friend void detail_ScreenFactory(D2AnimeScreen* screen, PPCContext& ctx, u8* base);
    friend void detail_OnMenuUpdate(u32 menuAddr, PPCContext& ctx, u8* base);
    friend D2AnimeScreen* FindScreenByMenu(u32 menuAddr);
    friend D2AnimeScreen* FindScreenByTask(u32 taskAddr);
    friend D2AnimeScreen* FindScreenAwaitingMenu();
};

void RegisterScreen(D2AnimeScreen* screen);
D2AnimeScreen* FindScreen(const std::string& name);

enum class Button : int {
    Back = 5,
    A = 8, B = 9, X = 10, Y = 11,
};

bool CheckButton(Button btn);
bool CheckConfirmInput(u32 menuAddr);
bool CheckCancelInput(u32 menuAddr);

void VarBagSetString(u32 varBag, const char* varName, const char* value);
void VarBagSetColor(u32 varBag, const char* varName, u32 rgba);
void VarBagSetFloat(u32 varBag, const char* varName, double value);

}  // namespace bd
