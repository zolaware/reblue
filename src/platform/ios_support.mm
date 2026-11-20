#include "platform/ios_support.h"

#include <TargetConditionals.h>

#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "core/logging.h"

namespace bd::platform::ios {
namespace {

void ExcludeFromBackup(const std::filesystem::path &path) {
  @autoreleasepool {
    NSURL *url =
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]
                   isDirectory:YES];
    NSError *error = nil;
    if (![url setResourceValue:@YES
                        forKey:NSURLIsExcludedFromBackupKey
                         error:&error]) {
      BD_WARN("[ios] could not exclude {} from iCloud backup: {}",
              path.string(),
              error ? error.localizedDescription.UTF8String : "unknown error");
    }
  }
}

} // namespace

std::filesystem::path DocumentsDir() {
  @autoreleasepool {
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *first = paths.firstObject;
    return first ? std::filesystem::path(first.UTF8String)
                 : std::filesystem::path{};
  }
}

std::filesystem::path BundleResource(const char *name) {
  @autoreleasepool {
    NSString *res = [[NSBundle mainBundle]
        pathForResource:[NSString stringWithUTF8String:name]
                 ofType:nil];
    return res ? std::filesystem::path(res.UTF8String)
               : std::filesystem::path{};
  }
}

void PrepareDocuments() {
  const auto documents = DocumentsDir();
  if (documents.empty())
    return;
  std::error_code ec;
  for (const char *folder :
       {"game", "profiles/default/saves", "cache", "logs"}) {
    std::filesystem::create_directories(documents / folder, ec);
  }
  for (const char *folder : {"game", "cache", "logs"}) {
    ExcludeFromBackup(documents / folder);
  }
}

void FitMetalView(void *uiWindow, void *metalView) {
  if (!uiWindow || !metalView)
    return;
  UIWindow *window = (__bridge UIWindow *)uiWindow;
  UIView *view = (__bridge UIView *)metalView;
  auto fit = ^{
    const CGRect before = view.frame;
    view.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    view.transform = CGAffineTransformIdentity;
    view.frame = window.bounds;
    [view setNeedsLayout];
    [view layoutIfNeeded];
    BD_INFO("[ios] Metal view {}x{} -> {}x{} points (window {}x{}, scale {})",
            before.size.width, before.size.height, view.frame.size.width,
            view.frame.size.height, window.bounds.size.width,
            window.bounds.size.height, view.layer.contentsScale);
  };
  if ([NSThread isMainThread])
    fit();
  else
    dispatch_async(dispatch_get_main_queue(), fit);
}

std::filesystem::path UbiquityDocuments(const char *containerIdentifier) {
  @autoreleasepool {
    NSURL *container = [NSFileManager.defaultManager
        URLForUbiquityContainerIdentifier:
            [NSString stringWithUTF8String:containerIdentifier]];
    if (!container)
      return {};
    return std::filesystem::path(container.path.UTF8String) / "Documents";
  }
}

void DownloadUbiquitousItems(const std::filesystem::path &folder,
                             std::chrono::milliseconds timeout) {
  const auto pending = [&] {
    std::vector<std::filesystem::path> items;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
      const auto name = it->path().filename().string();
      if (name.size() > 7 && name.front() == '.' && name.ends_with(".icloud")) {
        items.push_back(it->path().parent_path() /
                        name.substr(1, name.size() - 8));
      }
    }
    return items;
  };
  auto items = pending();
  if (items.empty())
    return;
  BD_INFO("[ios] downloading {} iCloud save file(s)", items.size());
  @autoreleasepool {
    for (const auto &item : items) {
      NSURL *url =
          [NSURL fileURLWithPath:[NSString stringWithUTF8String:item.c_str()]];
      NSError *error = nil;
      if (![NSFileManager.defaultManager startDownloadingUbiquitousItemAtURL:url
                                                                       error:&error]) {
        BD_WARN("[ios] iCloud download of {} failed: {}", item.string(),
                error ? error.localizedDescription.UTF8String
                      : "unknown error");
      }
    }
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!(items = pending()).empty() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (!items.empty()) {
    BD_WARN("[ios] {} iCloud save file(s) still downloading; they will sync "
            "next start",
            items.size());
  }
}

} // namespace bd::platform::ios

#endif
