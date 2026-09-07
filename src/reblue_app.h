/**
 * @file    reblue_app.h
 * @brief   ReblueApp: the application class wiring paths, installer, and
 * renderer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <rex/rex_app.h>

#include "installer/installer.h"
#include "ui/ui.h"

struct ImFontAtlas;

class ReblueApp : public rex::ReXApp {
public:
  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx);
  ~ReblueApp() override;

protected:
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig &config) override;
  void OnConfigureFonts(ImFontAtlas *atlas) override;
  void OnConfigureStyle(ImGuiStyle &imgui_style,
                        rex::ui::Style &ui_style) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer *drawer) override;
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override;
  std::optional<rex::PathConfig>
  OnFinalizePaths(const rex::PathConfig &defaults,
                  std::function<void(rex::PathConfig)> resume) override;
  void OnConfigurePaths(rex::PathConfig &paths) override;
  void OnPreLaunchModule() override;
  void OnWindowPixelSizeChanged(u32 pixel_width, u32 pixel_height) override;
  bool OnWindowCloseRequested() override;
  void OnShutdown() override;

private:
  explicit ReblueApp(rex::ui::WindowedAppContext &ctx);

  // Paths for booting against a recorded install. One owner: the resume that
  // follows the upgrade prompt has to reach the same answer OnFinalizePaths
  // would have returned.
  rex::PathConfig PathsForInstall(const rex::PathConfig &defaults,
                                  const bd::installer::InstallConfig &cfg);

  // Whether this build reached an install it did not write from outside it,
  // meaning the upgrade copies binaries and has to ask first.
  bool NeedsUpgradePrompt(const bd::installer::InstallConfig &cfg) const;

  // Restamps the record so the upgrade is not offered again. Everything the
  // prompt path does beyond this is in FinishUpgrade.
  void RestampInstall(const bd::installer::InstallConfig &cfg);

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  // Raises the prompt over the pre-guest pump and hands the answer to
  // FinishUpgrade. The caller returns nullopt: this owns what happens next.
  void BeginUpgrade(const bd::installer::InstallConfig &cfg,
                    rex::PathConfig defaults,
                    std::function<void(rex::PathConfig)> resume);

  // Accepted: copies this build in, restamps, and relaunches from the install.
  // Declined: boots the install as it stands, leaving the record alone so the
  // offer comes back next launch.
  void FinishUpgrade(bool accepted, bd::installer::InstallConfig cfg,
                     rex::PathConfig defaults,
                     std::function<void(rex::PathConfig)> resume);
#endif

#ifdef REBLUE_BUILD_INSTALLER
  void FinishInstaller(rex::PathConfig defaults,
                       std::function<void(rex::PathConfig)> resume,
                       bool completed, const bd::installer::InstallConfig &cfg,
                       const bd::installer::WizardChoices &choices);
#endif

  // Pre-guest UI plumbing for the installer wizard: bring up the native device
  // + overlay, then a tick thread that presents frames until the wizard
  // resolves. No Runtime/guest exists yet.
  bool BeginPreGuestUI();
  void StartPreGuestPump();
  void PumpPreGuestFrame();
  void StopPreGuestPump();
  void InstallOverlayDrawHook();

  void SetPerfOverlayStage(bd::ui::OverlayStage stage,
                           rex::ui::ImGuiDrawer *drawer);

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

#ifdef REBLUE_BUILD_INSTALLER
  std::unique_ptr<bd::installer::InstallerWizard> installer_wizard_;
#endif
  std::unique_ptr<bd::ui::PerfOverlay> perf_overlay_;
  std::unique_ptr<bd::ui::WatermarkOverlay> watermark_;
  std::unique_ptr<bd::ui::FadeOverlay> fade_overlay_;

  // The check the prompt last answered, so a re-run offers its build instead
  // of reading as the one already declined.
  u32 update_prompt_generation_ = 0;
  std::unique_ptr<bd::ui::UpdateStatusOverlay> update_status_;

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  std::unique_ptr<bd::installer::UpgradePrompt> upgrade_prompt_;
#endif

  std::thread pre_guest_tick_thread_;
  std::atomic<bool> pre_guest_pump_stop_{false};
  std::atomic<bool> pre_guest_tick_pending_{false};

  // Active profile, resolved once in OnConfigurePaths from the 'profile' cvar.
  // profile_root_ = install_root_/"profiles"/active_profile_ = user_data_root.
  std::string active_profile_ = "default";
  std::filesystem::path install_root_;
  std::filesystem::path profile_root_;
};
