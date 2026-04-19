// reblue - ReXGlue Recompiled Project

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/rex_app.h>

#include "installer/install_registry.h"
#include "ui/installer_wizard.h"

struct ImFontAtlas;

class ReblueApp : public rex::ReXApp {
 public:
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx);
  ~ReblueApp() override;

 protected:
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override;
  void OnPreLaunchModule() override;
  void OnShutdown() override;

 private:
  explicit ReblueApp(rex::ui::WindowedAppContext& ctx);

  void FinishInstaller(rex::PathConfig defaults,
                       std::function<void(rex::PathConfig)> resume,
                       bool completed,
                       const reblue::InstallConfig& cfg);

  // Non-null while the installer wizard is visible. Owned via the derived
  // type because ImGuiDialog's destructor is non-virtual (see wizard header).
  std::unique_ptr<reblue::ui::InstallerWizard> installer_wizard_;
};
