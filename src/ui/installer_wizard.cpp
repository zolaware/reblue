#include "ui/installer_wizard.h"

#include <imgui.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <stb_image.h>

#include <functional>
#include <vector>

#include "bdengine/common/logging.h"
#include "installer_font.h"
#include "installer_mp3.h"
#include "installer_png.h"
#include "ui/file_dialog.h"

namespace reblue::ui {
namespace {
const char* kDiscLabels[] = {"DVD 1", "DVD 2", "DVD 3"};

// Populated by InitInstallerFonts(). These are pushed/popped only while the
// installer wizard draws - other overlays (settings, console, debug) keep the
// ImGuiDrawer's default (ProggyTiny) font.
ImFont* g_body_font = nullptr;
ImFont* g_title_font = nullptr;
ImFont* g_path_font = nullptr;
}  // namespace

void InitInstallerFonts(ImFontAtlas* atlas) {
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;  // blob is static, don't let imgui free it
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;

  auto load = [&](float px) {
    return atlas->AddFontFromMemoryTTF(const_cast<uint8_t*>(g_installer_font_data),
                                       static_cast<int>(g_installer_font_size), px, &cfg);
  };
  g_body_font = load(18.0f);
  g_title_font = load(40.0f);
  g_path_font = load(13.0f);
  if (!g_body_font) {
    BD_WARN("Failed to load installer body font; wizard will use drawer default");
  }
}

InstallerWizard::InstallerWizard(rex::ui::ImGuiDrawer* drawer,
                                 rex::ui::ImmediateDrawer* immediate_drawer,
                                 rex::ui::WindowedAppContext& app_context,
                                 const std::filesystem::path& default_install_dir,
                                 CompletionCallback on_done)
    : ImGuiDialog(drawer),
      app_context_(app_context),
      immediate_drawer_(immediate_drawer),
      on_done_(std::move(on_done)),
      install_dir_(default_install_dir),
      audio_(std::make_unique<InstallerAudio>(g_installer_mp3_data, g_installer_mp3_size, 0.35f)) {}

InstallerWizard::~InstallerWizard() {
  if (install_thread_.joinable()) {
    progress_.cancelled.store(true);
    install_thread_.join();
  }
}

void InstallerWizard::Finish(bool completed) {
  if (finished_) return;
  finished_ = true;

  reblue::InstallConfig cfg;
  cfg.install_root = std::filesystem::absolute(install_dir_);
  cfg.iso1_fingerprint = iso_fingerprints_[0];
  cfg.iso2_fingerprint = iso_fingerprints_[1];
  cfg.iso3_fingerprint = iso_fingerprints_[2];

  BD_INFO("InstallerWizard: finished, completed={}", completed);

  auto cb = on_done_;
  app_context_.CallInUIThreadDeferred([cb, completed, cfg]() { cb(completed, cfg); });
}

void InstallerWizard::ValidateISO(int index) {
  iso_valid_[index] = false;
  iso_fingerprints_[index].clear();
  if (iso_paths_[index].empty()) {
    iso_status_[index].clear();
    return;
  }

  auto disc = reblue::OpenDiscImage(iso_paths_[index]);
  if (!disc) {
    iso_status_[index] = "Could not read as Xbox 360 disc image";
    return;
  }
  if (!reblue::ValidateDisc(*disc, index + 1)) {
    iso_status_[index] = std::string("Wrong disc - expected ") + kDiscLabels[index];
    return;
  }
  iso_valid_[index] = true;
  iso_fingerprints_[index] = reblue::DiscFingerprint(iso_paths_[index], *disc, index + 1);
  iso_status_[index] = "Valid";
}

bool InstallerWizard::InputsReady() const {
  return iso_valid_[0] && iso_valid_[1] && iso_valid_[2] && !install_dir_.empty();
}

void InstallerWizard::PickISO(int index) {
  BD_INFO("InstallerWizard: PickISO({}) invoked", index);
  static constexpr FileFilter kIsoFilters[] = {
      {L"ISO Disc Images", L"*.iso"},
      {L"All Files", L"*.*"},
  };
  auto picked = ShowOpenFileDialog(L"Select Xbox 360 Disc Image", kIsoFilters);
  if (!picked) {
    BD_INFO("InstallerWizard: PickISO({}) returned no selection", index);
    return;
  }
  BD_INFO("InstallerWizard: PickISO({}) selected '{}'", index, picked->string());
  iso_paths_[index] = *picked;
  ValidateISO(index);
}

void InstallerWizard::PickInstallDir() {
  BD_INFO("InstallerWizard: PickInstallDir invoked");
  auto picked = ShowOpenFolderDialog(L"Select Install Location");
  if (!picked) {
    BD_INFO("InstallerWizard: PickInstallDir returned no selection");
    return;
  }
  BD_INFO("InstallerWizard: PickInstallDir selected '{}'", picked->string());
  install_dir_ = *picked;
  install_status_.clear();
}

void InstallerWizard::StartInstall() {
  // InstallProgress holds atomics/mutexes and is non-assignable; reset fields
  // individually instead of assigning a fresh instance.
  progress_.files_done.store(0);
  progress_.files_total.store(0);
  progress_.bytes_done.store(0);
  progress_.bytes_total.store(0);
  progress_.complete.store(false);
  progress_.failed.store(false);
  progress_.cancelled.store(false);
  progress_.SetCurrentFile("");
  progress_.SetError("");
  done_message_.clear();
  done_success_ = false;
  install_status_.clear();
  page_ = Page::Installing;

  const auto abs_install = std::filesystem::absolute(install_dir_);
  const auto abs_game = abs_install / "game";
  const auto abs_user = abs_install / "user";
  BD_INFO("InstallerWizard: install root -> '{}'", abs_install.string());
  BD_INFO("InstallerWizard:   game data  -> '{}'", abs_game.string());
  BD_INFO("InstallerWizard:   user data  -> '{}'", abs_user.string());

  // Create the user data root (and its dlc subdir) up front. The installer
  // thread only touches the game data tree.
  std::error_code ec;
  std::filesystem::create_directories(abs_user / "dlc", ec);

  try {
    install_thread_ = reblue::Installer::RunAsync(iso_paths_[0], iso_paths_[1], iso_paths_[2],
                                                  abs_game, progress_);
  } catch (const std::system_error& e) {
    BD_ERROR("Installer::RunAsync failed to spawn worker: {}", e.what());
    progress_.SetError(std::string("Failed to start install: ") + e.what());
    progress_.failed.store(true);
    progress_.complete.store(true);
  }
}

void InstallerWizard::OnDraw(ImGuiIO&) {
  // Lazily decode and upload the background PNG on first draw. We only attempt
  // once; on failure we fall back to a plain background.
  if (!background_texture_ && !background_tried_ && immediate_drawer_) {
    background_tried_ = true;
    int w = 0, h = 0, channels = 0;
    uint8_t* rgba = stbi_load_from_memory(g_installer_png_data,
                                          static_cast<int>(g_installer_png_size),
                                          &w, &h, &channels, /*req_comp=*/4);
    if (!rgba) {
      BD_ERROR("InstallerWizard: stbi_load_from_memory failed: {}", stbi_failure_reason());
    } else {
      background_texture_ = immediate_drawer_->CreateTexture(
          static_cast<uint32_t>(w), static_cast<uint32_t>(h),
          rex::ui::ImmediateTextureFilter::kLinear, /*is_repeated=*/false, rgba);
      stbi_image_free(rgba);
      if (!background_texture_) {
        BD_ERROR("InstallerWizard: failed to create background texture");
      }
    }
  }

  auto* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowBgAlpha(0.0f);  // window bg transparent; background image fills instead
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##installer", nullptr, flags)) {
    if (background_texture_) {
      ImVec2 p0 = vp->WorkPos;
      ImVec2 p1 = ImVec2(p0.x + vp->WorkSize.x, p0.y + vp->WorkSize.y);
      ImGui::GetWindowDrawList()->AddImage(
          reinterpret_cast<ImTextureID>(background_texture_.get()), p0, p1);
    }

    // Semi-transparent dark panel so the controls stay readable against the
    // full-viewport background image.
    const float panel_width = 760.0f;
    const float panel_margin = 32.0f;
    ImGui::SetCursorPos(ImVec2(panel_margin, panel_margin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 20, 28, 220));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    // Generous button padding so labels aren't jammed against the edges.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
    if (ImGui::BeginChild("##installer_panel",
                          ImVec2(panel_width, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoSavedSettings)) {
      // Scope the Helvetica body font to the wizard only so other overlays
      // (settings, console, debug) keep the SDK's default font.
      if (g_body_font) ImGui::PushFont(g_body_font);
      switch (page_) {
        case Page::SelectInputs: DrawSelectInputs(); break;
        case Page::Installing:   DrawInstalling();   break;
        case Page::Done:         DrawDone();         break;
      }
      if (g_body_font) ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
  }
  ImGui::End();
}

namespace {
void DrawTitle(const char* text) {
  if (g_title_font) ImGui::PushFont(g_title_font);
  ImGui::TextUnformatted(text);
  if (g_title_font) ImGui::PopFont();
}
}  // namespace

namespace {
// Muted section header, visually quieter than the page title.
void SectionHeader(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.90f, 1.0f));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::Separator();
}

// Shows basename only; hover for full path. Used in the ISO table where the
// status/fingerprint columns already tell the user which disc is which.
void FilenameCell(const std::filesystem::path& path) {
  if (path.empty()) {
    ImGui::TextDisabled("not selected");
    return;
  }
  ImGui::TextUnformatted(path.filename().string().c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", path.string().c_str());
  }
}

// Directory row: heading + sublabel + Change button on one line, then the
// full path on the next line in a smaller font and slightly indented.
void DirectoryRow(const char* heading, const char* sublabel,
                  const std::filesystem::path& path, const char* id,
                  const std::function<void()>& on_change) {
  ImGui::PushID(id);
  // Align the text baselines with the Change button's frame padding so the
  // whole row reads as one line rather than the text riding up.
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(heading);
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.62f, 0.70f, 1.0f));
  ImGui::TextUnformatted(sublabel);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::Button("Change")) on_change();

  if (g_path_font) ImGui::PushFont(g_path_font);
  ImGui::Indent(12.0f);
  if (path.empty()) {
    ImGui::TextDisabled("not selected");
  } else {
    ImGui::TextWrapped("%s", path.string().c_str());
  }
  ImGui::Unindent(12.0f);
  if (g_path_font) ImGui::PopFont();
  ImGui::PopID();
}
}  // namespace

void InstallerWizard::DrawSelectInputs() {
  DrawTitle("re:Blue - Installer");
  ImGui::Spacing();

  SectionHeader("Install Sources");
  const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody;
  if (ImGui::BeginTable("##inputs", 3, flags)) {
    ImGui::TableSetupColumn("##btn", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("##path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed, 100.0f);

    for (int i = 0; i < 3; ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      char btn[32];
      std::snprintf(btn, sizeof(btn), "Select %s", kDiscLabels[i]);
      if (ImGui::Button(btn, ImVec2(-FLT_MIN, 0))) PickISO(i);

      ImGui::TableSetColumnIndex(1);
      FilenameCell(iso_paths_[i]);

      ImGui::TableSetColumnIndex(2);
      if (!iso_status_[i].empty()) {
        const ImVec4 colour = iso_valid_[i] ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f)
                                            : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(colour, "%s", iso_status_[i].c_str());
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, 12));
  SectionHeader("Install Directory");
  DirectoryRow("Install Location", "(~13 GB required)",
               install_dir_, "install_dir",
               [this]() { PickInstallDir(); });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (!install_status_.empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", install_status_.c_str());
    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!InputsReady());
  if (ImGui::Button("Install", ImVec2(120, 0))) StartInstall();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) Finish(false);
}

void InstallerWizard::DrawInstalling() {
  DrawTitle("Installing...");
  ImGui::Spacing();

  size_t total_bytes = progress_.bytes_total.load();
  size_t done_bytes = progress_.bytes_done.load();
  float fraction = total_bytes == 0 ? 0.0f
                                    : static_cast<float>(done_bytes) / static_cast<float>(total_bytes);
  ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), nullptr);

  auto format_bytes = [](size_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMiB = 1024.0 * 1024.0;
    char buf[32];
    double b = static_cast<double>(bytes);
    if (b >= kGiB) {
      std::snprintf(buf, sizeof(buf), "%.2f GiB", b / kGiB);
    } else {
      std::snprintf(buf, sizeof(buf), "%.1f MiB", b / kMiB);
    }
    return std::string(buf);
  };
  ImGui::Text("%s / %s", format_bytes(done_bytes).c_str(), format_bytes(total_bytes).c_str());

  auto current = progress_.GetCurrentFile();
  if (!current.empty()) ImGui::Text("Installing: %s", current.c_str());

  ImGui::Spacing();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    progress_.cancelled.store(true);
  }

  if (progress_.complete.load()) {
    if (install_thread_.joinable()) install_thread_.join();
    if (progress_.cancelled.load()) {
      // Cancel returns to the input page per spec - user can fix inputs and retry.
      install_status_ = "Previous install was cancelled. Review inputs and click Install to resume.";
      page_ = Page::SelectInputs;
    } else if (progress_.failed.load()) {
      done_success_ = false;
      done_message_ = "Install failed: " + progress_.GetError();
      page_ = Page::Done;
    } else {
      done_success_ = true;
      done_message_ = "Install complete.";
      page_ = Page::Done;
    }
  }
}

void InstallerWizard::DrawDone() {
  DrawTitle(done_success_ ? "Done" : "Stopped");
  ImGui::Spacing();
  ImGui::TextWrapped("%s", done_message_.c_str());
  ImGui::Spacing();
  if (done_success_) {
    if (ImGui::Button("Continue", ImVec2(120, 0))) Finish(true);
  } else {
    if (ImGui::Button("Quit", ImVec2(120, 0))) Finish(false);
  }
}

}  // namespace reblue::ui
