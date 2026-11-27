// SPDX-License-Identifier: BSD-3-Clause
// Keep the SDK's startup and shutdown implementation intact. Only its public
// entry-point names change, allowing setup to finish before any SDL video init.
#define main ReblueSdkMain
#define wWinMain ReblueSdkWinMain
#include REBLUE_SDK_ENTRYPOINT
#undef main
#undef wWinMain

#include "launcher/recomplauncher.h"
#include <iostream>

namespace {
int Launch(std::vector<std::string> args) {
  if (auto code = bd::launcher::Configure(args))
    return *code;
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(arg.data());
  argv.push_back(nullptr);
  try {
    return RunWindowedApp(static_cast<int>(args.size()), argv.data());
  } catch (const std::exception &error) {
    if (!bd::launcher::ExternalLaunch())
      throw;
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
} // namespace

#if REX_PLATFORM_WIN32
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show) {
  int argc = 0;
  auto **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return 2;
  auto args = WideArgsToUtf8(argc, argv);
  LocalFree(argv);
  return Launch(std::move(args));
}
#else
int main(int argc, char **argv) {
  return Launch(std::vector<std::string>(argv, argv + argc));
}
#endif
