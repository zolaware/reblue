#pragma once

#include <chrono>
#include <filesystem>

namespace bd::platform::ios {

std::filesystem::path DocumentsDir();
std::filesystem::path BundleResource(const char *name);
void PrepareDocuments();
void FitMetalView(void *uiWindow, void *metalView);
std::filesystem::path UbiquityDocuments(const char *containerIdentifier);
void DownloadUbiquitousItems(const std::filesystem::path &folder,
                             std::chrono::milliseconds timeout);

} // namespace bd::platform::ios
