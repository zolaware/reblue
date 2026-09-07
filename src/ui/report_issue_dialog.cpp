/**
 * @file    ui/report_issue_dialog.cpp
 * @brief   Writes screenshot.png, info.txt, config.toml, description.txt and
 *          the current session log into the report folder.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/report_issue_dialog.h"

#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/version.h>

#include "core/build_info.h"
#include "core/logging.h"
#include "engine/engine.h"
#include "gpu/gpu.h"

namespace bd::ui {

namespace {

std::string Stamp() {
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  return std::format("{:%Y%m%d-%H%M%S}", now);
}

void WriteBytes(const std::filesystem::path &p, const std::vector<u8> &b) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (f)
    f.write(reinterpret_cast<const char *>(b.data()),
            static_cast<std::streamsize>(b.size()));
}

void WriteText(const std::filesystem::path &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (f)
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// Highest reblue_NNN.log in logs_dir == the current session's log.
std::filesystem::path FindCurrentLog(const std::filesystem::path &logs_dir) {
  std::error_code ec;
  int best = -1;
  std::filesystem::path best_path;
  for (const auto &e : std::filesystem::directory_iterator(logs_dir, ec)) {
    if (ec)
      break;
    if (e.path().extension() != ".log")
      continue;
    const std::string name = e.path().stem().string(); // reblue_NNN
    if (name.rfind("reblue_", 0) != 0 || name.size() < 10)
      continue;
    const int seq = std::atoi(name.substr(7, 3).c_str());
    if (seq > best) {
      best = seq;
      best_path = e.path();
    }
  }
  return best_path;
}

std::string BuildInfoTxt(const ReportContext &ctx) {
  const auto stats = bd::gpu::GetFrameStats();
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  std::string o;
  o += std::format("time:        {:%Y-%m-%d %H:%M:%S}\n", now);
  o += std::format("build:       reblue-v{}\n", REBLUE_VERSION_STRING);
  o += std::format("sdk:         {}\n", REXGLUE_BUILD_TITLE);
  o += std::format("gpu:         {}\n", bd::gpu::Video::GetDeviceName());
  o += std::format("backend:     {}\n", bd::gpu::Video::GetBackendInfo());
  o += std::format("swapchain:   {} x {}\n", bd::gpu::Video::OutputWidth(),
                   bd::gpu::Video::OutputHeight());
  u32 render_w = 0, render_h = 0;
  bd::gpu::Output::LatchedFit(render_w, render_h);
  o += std::format("render:      {} x {}  (resolution '{}')\n", render_w,
                   render_h, rex::cvar::GetFlagByName("resolution"));
  o += std::format("window:      {} x {}\n", ctx.window_width,
                   ctx.window_height);
  o += std::format("window_cfg:  {} x {}\n",
                   rex::cvar::GetFlagByName("window_width"),
                   rex::cvar::GetFlagByName("window_height"));
  o += std::format("fullscreen:  {}\n", rex::cvar::GetFlagByName("fullscreen"));
  o += std::format("frame_ms:    {:.2f}  (fps {:.1f})\n", stats.frame_time_ms,
                   stats.fps);
  o += "platform:    Windows x64\n";

  const bd::engine::Field field;
  const bd::engine::Stage stage = field.Stage();
  if (stage) {
    const std::string name = stage.Name();
    if (!name.empty())
      o += std::format("stage:       {} ({})\n", name, stage.CombinedNum());
    else
      o += std::format("stage:       id {} (cat {})\n", stage.CombinedNum(),
                       stage.Category());
  } else {
    o += "stage:       (not in field / menu)\n";
  }
  if (field.HasPlayer()) {
    const bd::engine::Vec3 pos = field.Position();
    const bd::engine::Vec3 rot = field.Rotation();
    o += std::format("player_pos:  {:.2f}, {:.2f}, {:.2f}\n", pos[0], pos[1],
                     pos[2]);
    o += std::format("player_rot:  yaw {:.3f} rad (x {:.3f}, z {:.3f})\n",
                     rot[1], rot[0], rot[2]);
  } else {
    o += "player_pos:  unknown\n";
  }
  return o;
}

// Archive every regular file in 'folder' (flat, by filename) into out_zip.
bool ZipDir(const std::filesystem::path &folder,
            const std::filesystem::path &out_zip) {
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, out_zip.string().c_str(), 0))
    return false;
  bool ok = true;
  std::error_code ec;
  for (const auto &e : std::filesystem::directory_iterator(folder, ec)) {
    if (ec) {
      ok = false;
      break;
    }
    if (!e.is_regular_file(ec))
      continue;
    const std::string arc = e.path().filename().string();
    if (!mz_zip_writer_add_file(&zip, arc.c_str(), e.path().string().c_str(),
                                nullptr, 0, MZ_DEFAULT_LEVEL)) {
      ok = false;
      break;
    }
  }
  if (ok)
    ok = mz_zip_writer_finalize_archive(&zip);
  mz_zip_writer_end(&zip);
  if (!ok)
    std::filesystem::remove(out_zip, ec);
  return ok;
}

// Returns the written .zip path (or, if zipping fails, the unzipped staging
// folder so the data is never lost), or empty if nothing could be written.
std::filesystem::path WriteReport(const ReportContext &ctx,
                                  const std::string &description,
                                  const std::vector<u8> &png) {
  std::error_code ec;
  std::filesystem::create_directories(ctx.reports_root, ec);
  const std::string base = "report_" + Stamp();
  std::filesystem::path zip_path = ctx.reports_root / (base + ".zip");
  for (int i = 1; std::filesystem::exists(zip_path, ec); ++i)
    zip_path = ctx.reports_root / (base + "_" + std::to_string(i) + ".zip");

  // Stage the files in a temp folder named after the zip, then archive +
  // remove.
  const std::filesystem::path dir = ctx.reports_root / zip_path.stem();
  if (!std::filesystem::create_directories(dir, ec)) {
    BD_WARN("report: cannot create {}", dir.string());
    return {};
  }

  if (!png.empty())
    WriteBytes(dir / "screenshot.png", png);
  WriteText(dir / "description.txt", description);
  rex::cvar::SaveConfig(dir / "config.toml");
  WriteText(dir / "info.txt", BuildInfoTxt(ctx));
  const std::filesystem::path log = FindCurrentLog(ctx.logs_dir);
  if (!log.empty())
    std::filesystem::copy_file(
        log, dir / log.filename(),
        std::filesystem::copy_options::overwrite_existing, ec);

  if (ZipDir(dir, zip_path)) {
    std::filesystem::remove_all(dir, ec); // keep only the zip
    return zip_path;
  }
  BD_WARN("report: zip failed, left files in {}", dir.string());
  return dir; // fallback: the unzipped folder still has everything
}

} // namespace

ReportIssueDialog::ReportIssueDialog(rex::ui::ImGuiDrawer *drawer,
                                     ReportContext ctx,
                                     std::function<void()> on_closed)
    : rex::ui::ImGuiDialog(drawer), ctx_(std::move(ctx)),
      on_closed_(std::move(on_closed)) {}

ReportIssueDialog::~ReportIssueDialog() = default;

void ReportIssueDialog::RequestClose() { Close(); }

void ReportIssueDialog::OnClose() {
  bd::gpu::CancelScreenshot(); // drop any pending capture
  if (on_closed_)
    on_closed_();
}

void ReportIssueDialog::OnDraw(ImGuiIO &) {
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Report an Issue", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (submitted_) {
    if (saved_path_.empty()) {
      ImGui::TextWrapped("Failed to save the report. See the log for details.");
    } else {
      ImGui::TextWrapped("Report saved to:");
      ImGui::TextWrapped("%s", saved_path_.string().c_str());
      ImGui::Spacing();
      ImGui::TextWrapped(
          "Please attach this .zip to your bug report on the reblue Discord.");
    }
    ImGui::Separator();
    if (!saved_path_.empty()) {
      if (ImGui::Button("Copy path", ImVec2(120, 0)))
        ImGui::SetClipboardText(saved_path_.string().c_str());
      ImGui::SameLine();
    }
    if (ImGui::Button("Done", ImVec2(120, 0)))
      Close();
    ImGui::End();
    return;
  }

  ImGui::TextWrapped(
      "Describe what went wrong. A screenshot and debug info are attached.");
  ImGui::Spacing();
  ImGui::InputTextMultiline("##desc", description_, sizeof(description_),
                            ImVec2(-1.0f, 140.0f));
  ImGui::Spacing();

  ImGui::TextUnformatted("Included in this report:");
  ImGui::BulletText("Screenshot: %s",
                    bd::gpu::ReadyScreenshot() ? "captured" : "capturing...");
  ImGui::BulletText("info.txt (build, GPU, resolution, frame timing)");
  ImGui::BulletText("config.toml (your settings)");
  ImGui::BulletText("Current session log");
  ImGui::Separator();

  if (ImGui::Button("Submit", ImVec2(120, 0))) {
    std::vector<u8> png;
    if (bd::gpu::ReadyScreenshot())
      png = bd::gpu::EncodePng(bd::gpu::TakeScreenshot());
    saved_path_ = WriteReport(ctx_, description_, png);
    submitted_ = true;
    if (!saved_path_.empty())
      BD_INFO("report: written to {}", saved_path_.string());
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    Close();
  }

  ImGui::End();
}

} // namespace bd::ui
