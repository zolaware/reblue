/**
 * @file        bdengine/file_dialogue.cpp
 * @brief       Windows COM IFileOpenDialog implementation for file selection.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/platform/file_dialogue.h"
#include "bdengine/common/logging.h"

#include <shobjidl.h>
#include <comdef.h>

#include <rex/types.h>

namespace bd {

std::optional<std::filesystem::path> OpenFileDialogue(
    const wchar_t* title,
    const wchar_t* filter_name,
    const wchar_t* filter_pattern) {

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    IFileOpenDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (FAILED(hr)) {
        BD_ERROR("[dlc] CoCreateInstance failed: 0x{:08X}", static_cast<u32>(hr));
        if (needsUninit) CoUninitialize();
        return std::nullopt;
    }

    DWORD options = 0;
    pDialog->GetOptions(&options);
    pDialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    pDialog->SetTitle(title);

    if (filter_name && filter_pattern) {
        COMDLG_FILTERSPEC filter = { filter_name, filter_pattern };
        pDialog->SetFileTypes(1, &filter);
    }

    hr = pDialog->Show(nullptr);
    if (FAILED(hr)) {
        pDialog->Release();
        if (needsUninit) CoUninitialize();
        return std::nullopt;
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
