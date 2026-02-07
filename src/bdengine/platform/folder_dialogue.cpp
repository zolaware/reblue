/**
 * @file        bdengine/folder_dialogue.cpp
 * @brief       Windows COM IFileOpenDialog implementation.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/platform/folder_dialogue.h"
#include "bdengine/common/logging.h"

#include <shobjidl.h>
#include <comdef.h>

#include <rex/types.h>

namespace bd {

std::optional<std::filesystem::path> OpenFolderDialogue() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE)
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needsUninit = SUCCEEDED(hr);

    IFileOpenDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (FAILED(hr)) {
        BD_ERROR("[modmgr] CoCreateInstance failed: 0x{:08X}", static_cast<u32>(hr));
        if (needsUninit) CoUninitialize();
        return std::nullopt;
    }

    DWORD options = 0;
    pDialog->GetOptions(&options);
    pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDialog->SetTitle(L"Select Mod Folder");

    hr = pDialog->Show(nullptr);
    if (FAILED(hr)) {
        pDialog->Release();
        if (needsUninit) CoUninitialize();
        return std::nullopt;  // user cancelled or error
    }

    IShellItem* pItem = nullptr;
    hr = pDialog->GetResult(&pItem);
    if (FAILED(hr)) {
        pDialog->Release();
        if (needsUninit) CoUninitialize();
        return std::nullopt;
    }

    PWSTR path = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(hr) && path) {
        result = std::filesystem::path(path);
        CoTaskMemFree(path);
    }

    pItem->Release();
    pDialog->Release();
    if (needsUninit) CoUninitialize();

    return result;
}

}  // namespace bd
