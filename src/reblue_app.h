// reblue - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>

#include "bdengine/common/threading.h"

class ReblueApp : public rex::ReXApp {
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx) {
    return std::unique_ptr<ReblueApp>(
        new ReblueApp(ctx, "reblue", PPCImageConfig));
  }

  void OnShutdown() override { bd::DisableHighResTimer(); }
};
