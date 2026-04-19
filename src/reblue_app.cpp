#include "reblue_app.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/version.h>

#include "bdengine/common/logging.h"
#include "bdengine/common/threading.h"
#include "generated/reblue_init.h"
#include "ui/installer_wizard.h"
#include "ui/message_box.h"

REXCVAR_DECLARE(std::string, game_data_root);
REXCVAR_DECLARE(std::string, cache_path);
REXCVAR_DEFINE_BOOL(no_installer, false, "reblue",
                    "Skip the first-run installer; error out if no install is configured");

std::unique_ptr<rex::ui::WindowedApp> ReblueApp::Create(rex::ui::WindowedAppContext& ctx) {
  return std::unique_ptr<ReblueApp>(new ReblueApp(ctx));
}

ReblueApp::ReblueApp(rex::ui::WindowedAppContext& ctx)
    : rex::ReXApp(ctx, "reblue", PPCImageConfig) {}

ReblueApp::~ReblueApp() = default;

void ReblueApp::OnConfigureFonts(ImFontAtlas* atlas) {
  reblue::ui::InitInstallerFonts(atlas);
}

void ReblueApp::OnCreateDialogs(rex::ui::ImGuiDrawer*) {
  window()->SetTitle(std::string("re:Blue ") + REXGLUE_BUILD_TITLE);
}

std::optional<rex::PathConfig> ReblueApp::OnFinalizePaths(
    const rex::PathConfig& defaults,
    std::function<void(rex::PathConfig)> resume) {
  // The SDK has already applied --game_data_root / positional game_directory
  // into defaults.game_data_root. Detect whether the user supplied either.
  const bool user_supplied_path =
      GetArgument("game_directory").has_value() || !REXCVAR_GET(game_data_root).empty();

  if (user_supplied_path) {
    if (!std::filesystem::exists(defaults.game_data_root / "default.xex")) {
      reblue::ui::ShowFatalError(
          "reblue - invalid game directory",
          "Path '" + defaults.game_data_root.string() +
              "' does not contain default.xex. "
              "Pass the directory that contains default.xex (typically the "
              "'base' subdirectory of an installed copy).");
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    return defaults;
  }

  auto apply_install = [&](const reblue::InstallConfig& cfg) {
    rex::PathConfig paths = defaults;
    paths.game_data_root = cfg.game_data_path();
    paths.user_data_root = cfg.user_data_path();
    // Cache follows user_data unless the user pinned it via cvar.
    if (REXCVAR_GET(cache_path).empty()) {
      paths.cache_root = paths.user_data_root / "cache";
    }
    return paths;
  };

  if (auto cfg = reblue::ReadInstallRegistry()) {
    BD_INFO("Resolved install from registry");
    BD_INFO("  install root:   {}", cfg->install_root.string());
    BD_INFO("  game data:      {}", cfg->game_data_path().string());
    BD_INFO("  user data:      {}", cfg->user_data_path().string());
    BD_INFO("  disc1 hash:     {}", cfg->iso1_fingerprint);
    BD_INFO("  disc2 hash:     {}", cfg->iso2_fingerprint);
    BD_INFO("  disc3 hash:     {}", cfg->iso3_fingerprint);
    return apply_install(*cfg);
  }

  if (REXCVAR_GET(no_installer)) {
    reblue::ui::ShowFatalError(
        "reblue - game not installed",
        "Game not installed. Either run without --no-installer to launch the "
        "installer, or pass the installed game directory as the first argument.");
    app_context().QuitFromUIThread();
    return std::nullopt;
  }

  // Async: hand control to the installer wizard. Resume fires when the user
  // either finishes the install or cancels.
  const auto default_install_dir = defaults.config_path.parent_path() / "data";
  installer_wizard_ = std::make_unique<reblue::ui::InstallerWizard>(
      imgui_drawer(), immediate_drawer(), app_context(),
      default_install_dir,
      [this, defaults, resume](bool completed, const reblue::InstallConfig& cfg) {
        FinishInstaller(defaults, resume, completed, cfg);
      });

  return std::nullopt;
}

void ReblueApp::FinishInstaller(rex::PathConfig defaults,
                                std::function<void(rex::PathConfig)> resume,
                                bool completed,
                                const reblue::InstallConfig& cfg) {
  installer_wizard_.reset();

  if (!completed) {
    app_context().QuitFromUIThread();
    return;
  }

  if (!reblue::WriteInstallRegistry(cfg)) {
    BD_WARN("Registry write failed; continuing into game for this session");
  }
  BD_INFO("Installed to {}", cfg.install_root.string());
  BD_INFO("  disc1 hash:     {}", cfg.iso1_fingerprint);
  BD_INFO("  disc2 hash:     {}", cfg.iso2_fingerprint);
  BD_INFO("  disc3 hash:     {}", cfg.iso3_fingerprint);

  rex::PathConfig paths = defaults;
  paths.game_data_root = cfg.game_data_path();
  paths.user_data_root = cfg.user_data_path();
  if (REXCVAR_GET(cache_path).empty()) {
    paths.cache_root = paths.user_data_root / "cache";
  }
  resume(std::move(paths));
}

void ReblueApp::OnPreLaunchModule() {
  bd::EnableHighResTimer();
}

void ReblueApp::OnShutdown() {
  installer_wizard_.reset();
  bd::DisableHighResTimer();
}
