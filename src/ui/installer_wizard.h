#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/windowed_app_context.h>

#include "installer/install_registry.h"
#include "installer/installer.h"
#include "ui/installer_audio.h"

struct ImFontAtlas;

namespace reblue::ui {

// Registers Helvetica Neue body + title fonts on the given ImGui font atlas.
// Wired through ReXApp::OnConfigureFonts; the drawer builds the atlas after
// this returns.
void InitInstallerFonts(ImFontAtlas* atlas);

// NOTE: rex::ui::ImGuiDialog's destructor is NOT virtual, and the SDK base
// class does `delete this` inside its Close() path. Do NOT call Close() on
// this wizard, and always own it via unique_ptr<InstallerWizard> (the derived
// type). Deletion through an ImGuiDialog* base pointer is UB here because the
// wizard holds non-trivial members (std::thread, std::mutex) that would leak
// or std::terminate the process.
class InstallerWizard : public rex::ui::ImGuiDialog {
 public:
  // Called via CallInUIThreadDeferred when the wizard resolves.
  // completed=true → user finished installing or accepted and cfg is valid.
  // completed=false → user cancelled / errored out → app should quit.
  using CompletionCallback =
      std::function<void(bool completed, const reblue::InstallConfig& cfg)>;

  InstallerWizard(rex::ui::ImGuiDrawer* drawer,
                  rex::ui::ImmediateDrawer* immediate_drawer,
                  rex::ui::WindowedAppContext& app_context,
                  const std::filesystem::path& default_install_dir,
                  CompletionCallback on_done);
  ~InstallerWizard();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Page { SelectInputs, Installing, Done };

  void DrawSelectInputs();
  void DrawInstalling();
  void DrawDone();

  void PickISO(int index);
  void PickInstallDir();
  void ValidateISO(int index);
  bool InputsReady() const;
  void StartInstall();
  void Finish(bool completed);

  rex::ui::WindowedAppContext& app_context_;
  rex::ui::ImmediateDrawer* immediate_drawer_;
  CompletionCallback on_done_;
  bool finished_ = false;

  // Lazily created on first OnDraw.
  std::unique_ptr<rex::ui::ImmediateTexture> background_texture_;
  bool background_tried_ = false;

  Page page_ = Page::SelectInputs;

  std::filesystem::path iso_paths_[3];
  bool iso_valid_[3] = {};
  std::string iso_status_[3];
  std::string iso_fingerprints_[3];

  std::filesystem::path install_dir_;  // install_dir/game and install_dir/user are created at install time
  std::string install_status_;         // banner shown above the Install/Cancel row

  reblue::InstallProgress progress_;
  std::thread install_thread_;
  std::string done_message_;
  bool done_success_ = false;

  // Looping background music; silent if audio init fails.
  std::unique_ptr<InstallerAudio> audio_;
};

}  // namespace reblue::ui
