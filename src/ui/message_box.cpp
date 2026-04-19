#include "ui/message_box.h"

#include <rex/platform.h>

#include "bdengine/common/logging.h"

#if REX_PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>

namespace reblue::ui {
namespace {
std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
  return out;
}
}  // namespace

void ShowFatalError(std::string_view title, std::string_view body) {
  BD_ERROR("Fatal: {} - {}", title, body);
  MessageBoxW(nullptr, Utf8ToWide(body).c_str(), Utf8ToWide(title).c_str(),
              MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

}  // namespace reblue::ui

#else

namespace reblue::ui {
void ShowFatalError(std::string_view title, std::string_view body) {
  BD_ERROR("Fatal: {} - {}", title, body);
}
}  // namespace reblue::ui

#endif
