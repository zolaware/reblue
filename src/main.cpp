/**
 * @file    main.cpp
 * @brief   Application entry point, runs ReblueApp through its lifecycle steps.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <rex/app/title_app.h>
#include <rex/app/app.h>

#include "reblue_app.h"

namespace {

rex::i32 ReblueMain(ReblueApp &app, const rex::CLIArgList &args) {
  app.SetCommandLine(args);
  if (app.Init(args) && !rex::app::IsShutdownRequested()) {
    if (app.PrepareSetup()) {
      while (app.SetupActive() && app.RunFrame()) {
        app.PresentSetupFrame();
      }
    }
    if (!rex::app::IsShutdownRequested() && app.LaunchTitle()) {
      app.Run();
    }
  }
  const rex::i32 code = app.Shutdown();
  app.RunHandoff();
  return code;
}

} // namespace

REX_TITLE_APP_MAIN(ReblueApp, ReblueMain)
