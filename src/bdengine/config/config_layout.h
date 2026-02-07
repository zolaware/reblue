/**
 * @file        bdengine/config_layout.h
 * @brief       Config menu layout definitions using AnimeLayout DSL.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/d2anime/anime_layout.h"
#include "bdengine/mods/dlc_manager.h"
#include "bdengine/mods/mod_manager.h"

#include <algorithm>
#include <string>

#include <rex/types.h>

namespace bd {

/**
 * @brief Sidebar button template (l_modmgr_section.csv).
 */
class SectionTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV name{"Name", "Section"};
  StringV wndType{"WndType", "BTN01_OF"};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .var(name)
        .var(wndType)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("type,frame start,frame interval,x,y,w,h,pri,window "
                 "type,alpha,red,green,blue")
        .window(wnd_)
        .blank()
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(msg_);
  }

private:
  AnimePos pos_{"pos", 110, 121, 180, 40};
  AnimeWindow wnd_{"1", 0,         "pos",   0,   0,   0,   0,
                   0,   "WndType", nullptr, 128, 255, 255, 255};
  AnimeMessage msg_{"start", 0,       "pos", 12,    6,     20,    26,
                    1,       "alpha", "255", "255", "255", "Name"};
};

/**
 * @brief Mod list item template (l_modmgr_info.csv).
 */
class ModItemTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV name{"Name", "Mod"};
  StringV wndType{"WndType", "BTN01_OF"};
  FloatV chkOn{"ChkOn", 1.0};
  FloatV chkOff{"ChkOff", -1.0};
  ColorV color{"Color", 255, 255, 255, 255};
  ColorV enableColor{"EnableColor", 140, 255, 140, 255};
  ColorV disableColor{"DisableColor", 127, 127, 127, 255};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .blank()
        .var(name)
        .var(wndType)
        .var(chkOn)
        .var(chkOff)
        .var(color)
        .var(enableColor)
        .var(disableColor)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("type,frame start,frame interval,x,y,w,h,pri,window "
                 "type,alpha,red,green,blue")
        .window(wnd_)
        .blank()
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(msg_)
        .blank()
        .comment("type,frame start,frame "
                 "interval,x,y,w,h,pri,alpha,u0,v0,u1,v1,file")
        .tex(chkOnTex_)
        .tex(chkOffTex_);
  }

private:
  AnimePos pos_{"pos", 110, 121, 420, 45};
  AnimeWindow wnd_{"start", 0,         "pos",   0,   0,   0,   0,
                   0,       "WndType", nullptr, 128, 255, 255, 255};
  AnimeMessage msg_{"start",   0,         "pos", 20,        7,
                    20,        26,        1,     "Color.a", "Color.r",
                    "Color.g", "Color.b", "Name"};
  AnimeTex chkOnTex_{"ChkOn",
                     0,
                     "pos",
                     370,
                     8,
                     28,
                     28,
                     2,
                     255,
                     {0.5f, 0.25f, 1.0f, 0.5f},
                     "res\\mark_config",
                     "#ON"};
  AnimeTex chkOffTex_{"ChkOff",
                      0,
                      "pos",
                      370,
                      8,
                      28,
                      28,
                      2,
                      255,
                      {0.0f, 0.25f, 0.5f, 0.5f},
                      "res\\mark_config",
                      "#OFF"};
};

/**
 * @brief DLC list item template (l_modmgr_dlcinfo.csv).
 */
class DlcItemTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV name{"Name", "DLC"};
  StringV wndType{"WndType", "BTN01_OF"};
  ColorV color{"Color", 255, 255, 255, 255};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .blank()
        .var(name)
        .var(wndType)
        .var(color)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("type,frame start,frame interval,x,y,w,h,pri,window "
                 "type,alpha,red,green,blue")
        .window(wnd_)
        .blank()
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(msg_);
  }

private:
  AnimePos pos_{"pos", 110, 121, 420, 45};
  AnimeWindow wnd_{"start", 0,         "pos",   0,   0,   0,   0,
                   0,       "WndType", nullptr, 128, 255, 255, 255};
  AnimeMessage msg_{"start",   0,         "pos", 20,        7,
                    20,        26,        1,     "Color.a", "Color.r",
                    "Color.g", "Color.b", "Name"};
};

/**
 * @brief DLC detail panel template (l_modmgr_dlcdetail.csv).
 */
class DlcDetailTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};

  static constexpr int kDescLines = 3;
  StringV descLines[kDescLines]{
      {"Desc0", ""}, {"Desc1", ""}, {"Desc2", ""},
  };

  ColorV labelColor{"LabelColor", 255, 255, 255, 255};
  ColorV valueColor{"ValueColor", 255, 255, 255, 255};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .blank();
    for (int i = 0; i < kDescLines; ++i)
      b.var(descLines[i]);
    b.blank()
        .var(labelColor)
        .var(valueColor)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("metadata")
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message");
    for (int i = 0; i < kDescLines; ++i)
      b.message(descMsgLines_[i]);
    b.blank()
        .comment("divider")
        .comment(
            "type,frame start,frame interval,x,y,w,h,pri,alpha,red,green,blue")
        .frame(divider_);
  }

private:
  AnimePos pos_{"pos", 740, 140, 400, 450};
  AnimeMessage descMsgLines_[kDescLines]{
      {"start", 0, "pos", 10, 10, 16, 20, 1, "LabelColor.a", "LabelColor.r", "LabelColor.g", "LabelColor.b", "Desc0"},
      {"start", 0, "pos", 10, 30, 16, 20, 1, "LabelColor.a", "LabelColor.r", "LabelColor.g", "LabelColor.b", "Desc1"},
      {"start", 0, "pos", 10, 50, 16, 20, 1, "LabelColor.a", "LabelColor.r", "LabelColor.g", "LabelColor.b", "Desc2"},
  };
  AnimeFrameRel divider_{"start", 0, "pos",       10,  75, 380,
                         1,       3, "alpha-127", 255, 255, 255};
};

/**
 * @brief Shown when no mods installed (l_modmgr_nodata.csv).
 */
class NodataTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV name{"Name", "No mods installed"};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .blank()
        .var(name)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(msg_);
  }

private:
  AnimePos pos_{"pos", 110, 121, 420, 45};
  AnimeMessage msg_{"start", 0,       "pos", 100,   7,     24,    30,
                    1,       "alpha", "150", "150", "150", "Name"};
};

/**
 * @brief Detail panel template (l_modmgr_detail.csv).
 */
class DetailTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  FloatV hasPreview{"HasPreview", -1.0};
  StringV previewFile{"PreviewFile", "res\\mark_config"};

  StringV author{"Author", ""};
  StringV version{"Version", ""};
  StringV created{"Created", ""};
  StringV desc{"Desc", ""};

  ColorV labelColor{"LabelColor", 255, 255, 255, 255};
  ColorV valueColor{"ValueColor", 255, 255, 255, 255};

  void build(CsvBuilder &b) override {
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .var(hasPreview)
        .var(previewFile)
        .blank()
        .var(author)
        .var(version)
        .var(created)
        .var(desc)
        .blank()
        .var(labelColor)
        .var(valueColor)
        .blank()
        .comment("type,name,x,y,w,h")
        .pos(pos_)
        .blank()
        .comment("preview image")
        .comment("type,frame start,frame "
                 "interval,x,y,w,h,pri,alpha,u0,v0,u1,v1,file")
        .tex(preview_)
        .blank()
        .comment("metadata")
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(authorMsg_)
        .message(versionMsg_)
        .message(createdMsg_)
        .message(descMsg_)
        .blank()
        .comment("divider")
        .comment(
            "type,frame start,frame interval,x,y,w,h,pri,alpha,red,green,blue")
        .frame(divider_);
  }

private:
  AnimePos pos_{"pos", 740, 140, 400, 450};
  AnimeTex preview_{"HasPreview", 0, "pos", 10,           10,           380,
                    200,          2, 255,   {0, 0, 1, 1}, "PreviewFile"};
  AnimeMessage authorMsg_{"start",
                          0,
                          "pos",
                          10,
                          10,
                          16,
                          20,
                          1,
                          "LabelColor.a",
                          "LabelColor.r",
                          "LabelColor.g",
                          "LabelColor.b",
                          "Author"};
  AnimeMessage versionMsg_{"start",
                           0,
                           "pos",
                           10,
                           35,
                           16,
                           20,
                           1,
                           "LabelColor.a",
                           "LabelColor.r",
                           "LabelColor.g",
                           "LabelColor.b",
                           "Version"};
  AnimeMessage createdMsg_{"start",
                           0,
                           "pos",
                           10,
                           60,
                           16,
                           20,
                           1,
                           "LabelColor.a",
                           "LabelColor.r",
                           "LabelColor.g",
                           "LabelColor.b",
                           "Created"};
  AnimeMessage descMsg_{"start",
                        0,
                        "pos",
                        10,
                        85,
                        16,
                        20,
                        1,
                        "LabelColor.a",
                        "LabelColor.r",
                        "LabelColor.g",
                        "LabelColor.b",
                        "Desc"};
  AnimeFrameRel divider_{"start", 0, "pos",       10,  110, 380,
                         1,       3, "alpha-127", 255, 255, 255};
};

/**
 * @brief Main three-panel config menu (l_modmgr.csv).
 */
class ConfigLayout : public AnimeLayout {
public:
  // Child panels
  AnimePanel detail{"detail", "l_modmgr_detail.csv"};
  AnimePanel dlcDetail{"dlcdetail", "l_modmgr_dlcdetail.csv"};

  // Layout-level vars
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV title{"Title", "re:Blue Configuration"};
  StringV hdrSections{"HdrSections", ""};
  StringV hdrMods{"HdrMods", "Mods"};
  StringV hdrDetails{"HdrDetails", "Details"};
  StringV ftrA{"FtrA", "Select"};
  StringV ftrB{"FtrB", "Exit"};
  StringV ftrY{"FtrY", ""};
  StringV ftrX{"FtrX", ""};
  StringV ftrBack{"FtrBack", ""};
  FloatV ftrAVis{"FtrAVis", 1.0};
  FloatV ftrXVis{"FtrXVis", -1.0};
  FloatV ftrYVis{"FtrYVis", -1.0};
  FloatV ftrBackVis{"FtrBackVis", -1.0};

  // Menu widgets
  AnimeMenuWidget sectionMenu{
      "SltSection", 80,         140,     200, 120, 3,
      75,           150,        "RIGHT", 200, 50,  128,
      "BTN01_ON",   "BTN01_OF", 2,       1,   2,   "l_modmgr_section.csv"};

  AnimeMenuWidget modList{
      "ModList", 300,        140,     420, 0,  3,
      295,       150,        "RIGHT", 420, 45, 128,
      "FRAME01", "BTN01_OF", 0,       1,   0,  "l_modmgr_info.csv"};

  AnimeMenuWidget dlcList{
      "DlcList", 300,        140,     420, 0,  3,
      295,       150,        "RIGHT", 420, 45, 128,
      "FRAME01", "BTN01_OF", 0,       1,   0,  "l_modmgr_dlcinfo.csv"};

  /**
   * @brief Set mod count and adjust menu height/rows.
   */
  void setModCount(size_t count) {
    modCount_ = std::min(count, size_t(12));
    if (modCount_ > 0) {
      modList.h = static_cast<int>(modCount_) * 45 + 20;
      modList.rows = static_cast<int>(modCount_);
      modList.defaultItem = 0;
      modList.itemOnType = "FRAME01";
      modList.itemOffType = "BTN01_OF";
      modList.templateCsv = "l_modmgr_info.csv";
    } else {
      modList.h = 65;
      modList.rows = 1;
      modList.defaultItem = 1;
      modList.itemOnType = "NOFRAME03";
      modList.itemOffType = "NOFRAME03";
      modList.templateCsv = "l_modmgr_nodata.csv";
    }
  }

  void setDlcCount(size_t count) {
    dlcCount_ = std::min(count, size_t(12));
    if (dlcCount_ > 0) {
      dlcList.h = static_cast<int>(dlcCount_) * 45 + 20;
      dlcList.rows = static_cast<int>(dlcCount_);
      dlcList.defaultItem = 0;
      dlcList.itemOnType = "FRAME01";
      dlcList.itemOffType = "BTN01_OF";
      dlcList.templateCsv = "l_modmgr_dlcinfo.csv";
    } else {
      dlcList.h = 65;
      dlcList.rows = 1;
      dlcList.defaultItem = 1;
      dlcList.itemOnType = "NOFRAME03";
      dlcList.itemOffType = "NOFRAME03";
      dlcList.templateCsv = "l_modmgr_nodata.csv";
    }
  }

  void setDlcList(const std::vector<bd::DlcInfo>* list) { dlcList_ptr_ = list; }

  void build(CsvBuilder &b) override {
    // Child panels
    b.panel(detail).blank();
    b.panel(dlcDetail).blank();

    // Variables
    b.comment("variable definitions")
        .var(start)
        .var(alpha)
        .var(title)
        .var(hdrSections)
        .var(hdrMods)
        .var(hdrDetails)
        .var(ftrA)
        .var(ftrB)
        .var(ftrX)
        .var(ftrY)
        .var(ftrBack)
        .var(ftrAVis)
        .var(ftrXVis)
        .var(ftrYVis)
        .var(ftrBackVis)
        .blank();

    // Register vars for runtime sync
    registerVar(title);
    registerVar(hdrSections);
    registerVar(hdrMods);
    registerVar(hdrDetails);
    registerVar(ftrA);
    registerVar(ftrB);
    registerVar(ftrX);
    registerVar(ftrY);
    registerVar(ftrBack);
    registerVar(ftrAVis);
    registerVar(ftrXVis);
    registerVar(ftrYVis);
    registerVar(ftrBackVis);

    // Background overlay
    b.comment("type,frame start,frame interval,x,y,w,h,pri,window "
              "type,alpha,red,green,blue")
        .window(bg_)
        .blank();

    // Column headers
    b.comment("type,frame start,frame interval,x,y,font w,font "
              "h,pri,alpha,red,green,blue,message")
        .message(hdrSectionsMsg_)
        .message(hdrModsMsg_)
        .message(hdrDetailsMsg_)
        .blank()
        .blank();

    // Section sidebar
    b.comment("section sidebar")
        .comment("type,x,y,w,h,pri,name,StartCurX,StartCurY,CurDir,ItemW,ItemH,"
                 "ItemAlpha,ItemOnType,ItemOffType,TableSizeRow,"
                 "TableSizeColumn,defaultItem,TemplateAnime")
        .menu(sectionMenu)
        .blank()
        .menuSet(sectionMenu, 1, "Name", "Mods")
        .menuSet(sectionMenu, 2, "Name", "Official DLC")
        .blank();

    // Mod list
    b.comment("mod list")
        .comment("type,x,y,w,h,pri,name,StartCurX,StartCurY,CurDir,ItemW,ItemH,"
                 "ItemAlpha,ItemOnType,ItemOffType,TableSizeRow,"
                 "TableSizeColumn,defaultItem,TemplateAnime")
        .menu(modList)
        .blank();

    // DLC list
    b.comment("dlc list")
        .comment("type,x,y,w,h,pri,name,StartCurX,StartCurY,CurDir,ItemW,ItemH,"
                 "ItemAlpha,ItemOnType,ItemOffType,TableSizeRow,"
                 "TableSizeColumn,defaultItem,TemplateAnime")
        .menu(dlcList)
        .blank();

    // Header line
    b.comment(
         "type,frame start,frame interval,x,y,w,h,pri,alpha,red,green,blue")
        .frame(headerLine_)
        .blank();

    // Title
    b.comment("type,frame start,frame interval,x,y,font w,font "
              "h,pri,alpha,red,green,blue,message")
        .message(titleMsg_)
        .blank();

    // Bind mod names
    auto &mgr = GetModManager();
    for (size_t i = 0; i < modCount_; ++i) {
      const auto &info = mgr.GetModInfo(i);
      b.menuSet(modList, static_cast<int>(i + 1), "Name", info.name.c_str());
    }

    // Bind DLC names
    if (dlcList_ptr_) {
      for (size_t i = 0; i < dlcCount_; ++i) {
        b.menuSet(dlcList, static_cast<int>(i + 1), "Name",
                  (*dlcList_ptr_)[i].display_name.c_str());
      }
    }

    // Footer button icons
    b.blank()
        .comment("footer button icons")
        .comment("type,frame start,frame "
                 "interval,x,y,w,h,pri,alpha,u0,v0,u1,v1,file")
        .tex(ftrATex_)
        .tex(ftrBTex_)
        .tex(ftrXTex_)
        .tex(ftrYTex_)
        .tex(ftrBackTex_)
        .blank();

    // Footer labels
    b.comment("footer labels")
        .comment("type,frame start,frame interval,x,y,font w,font "
                 "h,pri,alpha,red,green,blue,message")
        .message(ftrAMsg_)
        .message(ftrBMsg_)
        .message(ftrXMsg_)
        .message(ftrYMsg_)
        .message(ftrBackMsg_)
        .blank();

    // Detail panels start hidden
    b.set(detail, "start", "-1");
    b.set(dlcDetail, "start", "-1");

    // SelMesWinTask variables - read by SelMesWinConfig_LoadStrings("RBDEL")
    b.blank()
        .comment("sysmes confirmation dialog variables")
        .var(rbdelSQ1)
        .var(rbdelSQ2)
        .var(rbdelSQ3)
        .var(rbdelSA1)
        .var(rbdelSA2)
        .var(rbdelFS)
        .var(rbdelLN);
    b.pos(rbdelPS_);
    b.pos(rbdelOFS_);
    b.var(rbdelWCL);
    b.var(rbdelECL);
    b.var(rbdelFCL);
  }

private:
  size_t modCount_ = 0;
  size_t dlcCount_ = 0;
  const std::vector<bd::DlcInfo>* dlcList_ptr_ = nullptr;

  AnimeWindow bg_{"start", 0,       nullptr,    0,   0,   1280, 720,
                  5,       nullptr, "BTN01_OF", 160, 255, 255,  255};

  AnimeMessageAbs hdrSectionsMsg_{"start", 0,   80,  110, 18,  22,
                                  4,       255, 255, 255, 255, "HdrSections"};
  AnimeMessageAbs hdrModsMsg_{"start", 0,   300, 110, 18,  22,
                              4,       255, 255, 255, 255, "HdrMods"};
  AnimeMessageAbs hdrDetailsMsg_{"start", 0,   740, 110, 18,  22,
                                 4,       255, 255, 255, 255, "HdrDetails"};

  AnimeFrame headerLine_{"start", 0,           0,   95,  1280, 2,
                         4,       "alpha-127", 255, 255, 255,  "#line"};
  AnimeMessageAbs titleMsg_{"start", 0,   100, 45,  43,  48,
                            0,       255, 255, 255, 255, "Title"};

  // Footer button icons (absolute coords)
  AnimeTexAbs ftrATex_{"FtrAVis",
                       0,
                       90,
                       652,
                       40,
                       40,
                       0,
                       255,
                       {0.0f, 0.0f, 0.25f, 0.25f},
                       "res\\cmn_help_menue"};
  AnimeTexAbs ftrBTex_{"start",
                       0,
                       310,
                       652,
                       40,
                       40,
                       0,
                       255,
                       {0.25f, 0.0f, 0.5f, 0.25f},
                       "res\\cmn_help_menue"};
  AnimeTexAbs ftrXTex_{"FtrXVis",
                       0,
                       530,
                       652,
                       40,
                       40,
                       0,
                       255,
                       {0.5f, 0.0f, 0.75f, 0.25f},
                       "res\\cmn_help_menue"};
  AnimeTexAbs ftrYTex_{"FtrYVis",
                       0,
                       730,
                       652,
                       40,
                       40,
                       0,
                       255,
                       {0.75f, 0.0f, 1.0f, 0.25f},
                       "res\\cmn_help_menue"};
  AnimeTexAbs ftrBackTex_{"FtrBackVis",
                          0,
                          930,
                          652,
                          40,
                          40,
                          0,
                          255,
                          {0.0f, 0.25f, 0.25f, 0.5f},
                          "res\\cmn_help_menue"};

  // Footer labels (absolute coords)
  AnimeMessageAbs ftrAMsg_{"FtrAVis", 0,   135, 658, 16,  20,
                           1,         255, 255, 255, 255, "FtrA"};
  AnimeMessageAbs ftrBMsg_{"start", 0,   355, 658, 16,  20,
                           1,       255, 255, 255, 255, "FtrB"};
  AnimeMessageAbs ftrXMsg_{"FtrXVis", 0,   575, 658, 16,  20,
                           1,         255, 255, 255, 255, "FtrX"};
  AnimeMessageAbs ftrYMsg_{"FtrYVis", 0,   775, 658, 16,  20,
                           1,         255, 255, 255, 255, "FtrY"};
  AnimeMessageAbs ftrBackMsg_{"FtrBackVis", 0,   975, 658, 16,       20, 1,
                              255,          255, 255, 255, "FtrBack"};

  // SelMesWinTask VarBag variables (prefix "RBDEL")
  // Format matches d2anime\warn\warn_mes_us.u16 (e.g. WARN0002)
  StringV rbdelSQ1{"RBDEL_SQ1", ""};
  StringV rbdelSQ2{"RBDEL_SQ2", ""};
  StringV rbdelSQ3{"RBDEL_SQ3", ""};
  StringV rbdelSA1{"RBDEL_SA1", "Yes"};
  StringV rbdelSA2{"RBDEL_SA2", "No"};
  FloatV rbdelFS{"RBDEL_FS", 32.0};
  FloatV rbdelLN{"RBDEL_LN", 2.0};
  AnimePos rbdelPS_{"RBDEL_PS", -1, 260, 870, 305, -100, true};
  AnimePos rbdelOFS_{"RBDEL_OFS", 230, 185, 0, 0};
  ColorV rbdelWCL{"RBDEL_WCL", 0, 0, 0, 192};        // window: black, 75% alpha
  ColorV rbdelECL{"RBDEL_ECL", 192, 192, 192, 255};   // edge: light gray
  ColorV rbdelFCL{"RBDEL_FCL", 240, 240, 240, 255};   // frame: light gray, opaque
};

} // namespace bd
