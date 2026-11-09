/**
 * @file    reblue_app.h
 * @brief   ReblueApp: the title app wiring paths, installer, and renderer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include <rex/cli.h>
#include <rex/input_keys.h>
#include <rex/app/title_app.h>
#include <rex/ui/ui_system.h>

#include "core/cvar_table.h"
#include "gpu/imgui_overlay_drawer.h"

#include "installer/installer.h"
#include "ui/ui.h"

class ReblueApp : public rex::app::TitleApp, public rex::ui::IUIEventListener {
public:
  ReblueApp();
  ~ReblueApp() override;

  // Raises the installer or the upgrade prompt when the install needs one.
  // True while that setup UI owns the boot, false when the title can launch or
  // shutdown was requested.
  // The raw arguments, read in OnConfigure ahead of the command line layer so
  // the profile and the install can pick the config file.
  void SetCommandLine(const rex::CLIArgList &args) { args_ = &args; }

  bool PrepareSetup();
  bool SetupActive() const;
  void PresentSetupFrame();

  // Starts the replacement executable an install or upgrade chose, after
  // Shutdown released everything.
  void RunHandoff();

protected:
  rex::AppSysRequirements GetRequirements() const override;
  rex::Result<void> OnConfigure(rex::SettingsRegistry &settings) override;
  rex::Result<void> OnInit() override;
  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime &title) override;
  void OnTitleExit(rex::kernel::TitleExitReason reason) override;
  void OnShutdown() override;
  std::span<const rex::CLIAction> GetActions() const override;
  rex::Result<void> OnAction(const rex::CLIActionHit &hit) override;
  void OnUIEvent(const rex::ui::UIEvent &event) override;
  void OnInputEvent(rex::InputEvent &event) override;

private:
  static constexpr std::array<rex::CLIAction, 1> kActions = {REX_CLI_ACTION(
      repair, "Open the installer wizard in repair mode on an existing install "
              "to re-copy game files or add DLC, even when the install is valid")};
  bool repair_requested_ = false;

  // Paths for booting against a recorded install. One owner: a declined
  // upgrade prompt has to reach the same answer PrepareSetup would have.
  // Whether this build sits outside a non-portable install, so upgrading it
  // copies binaries and has to ask first. Callers gate on official and newer.
  bool NeedsUpgradePrompt(const bd::installer::InstallConfig &cfg) const;

  // Restamps the record so the upgrade is not offered again. Everything the
  // prompt path does beyond this is in FinishUpgrade.
  void RestampInstall(const bd::installer::InstallConfig &cfg);

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  // Raises the prompt over the setup frames and hands the answer to
  // FinishUpgrade.
  void BeginUpgrade(const bd::installer::InstallConfig &cfg);

  // Accepted: copies this build in, restamps, and hands off to the install.
  // Declined: boots the install as it stands, leaving the record alone so the
  // offer comes back next launch.
  void FinishUpgrade(bool accepted, const bd::installer::InstallConfig &cfg);
#endif

#ifdef REBLUE_BUILD_INSTALLER
  void FinishInstaller(bool completed, const bd::installer::InstallConfig &cfg,
                       const bd::installer::WizardChoices &choices);
#endif

  // Brings up the native device and the overlay hook before the title, so the
  // setup UI can draw. No Runtime exists yet.
  bool BeginPreTitleUI();
  bool CreateDevice();
  void InstallOverlayDrawHook();

  // Raises the update prompt the first time Updates::Newer() has an answer.
  // Polled from the per-frame overlay marshal rather than a new pump.
  void MaybeShowUpdatePrompt();

  // Raises the corner readout while either startup check is still working,
  // and drops it once neither has anything to report.
  void UpdateCheckStatus();

  // Whether either startup check still has something to put on screen. The
  // overlay gate needs this because the calls that raise those dialogs run
  // behind it, so HasDialogs alone would never let the first one through.
  bool PendingOverlayWork() const;

  rex::ScopeGuard ui_listener_guard_;

#ifdef REBLUE_BUILD_INSTALLER
  std::unique_ptr<bd::installer::InstallerWizard> installer_wizard_;
  rex::ScopeGuard installer_wizard_guard_;
#endif

  // The check the prompt last answered, so a re-run offers its build instead
  // of reading as the one already declined.
  u32 update_prompt_generation_ = 0;
  std::unique_ptr<bd::ui::UpdateStatusOverlay> update_status_;
  rex::ScopeGuard update_status_guard_;
  std::unique_ptr<bd::ui::UpdatePromptDialog> update_prompt_;
  rex::ScopeGuard update_prompt_guard_;

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  std::unique_ptr<bd::installer::UpgradePrompt> upgrade_prompt_;
  rex::ScopeGuard upgrade_prompt_guard_;
#endif

  // Active profile, resolved once in OnConfigure from --bd.profile.
  // profile_root_ = install_root_/"profiles"/active_profile_ = user_data_root.
  std::string active_profile_ = "default";
  std::filesystem::path install_root_;
  std::filesystem::path profile_root_;

  // How OnInit resolved XCONFIG_USER_LANGUAGE, logged in OnTitleLaunch.
  bool language_autodetected_ = false;
  std::string unknown_language_code_;
  std::string location_warning_;

  const rex::CLIArgList *args_ = nullptr;
  std::unique_ptr<bd::gpu::ImGuiOverlayDrawer> overlay_drawer_;
  bd::SettingWatches watches_;
  std::optional<std::filesystem::path> handoff_exe_;
};
