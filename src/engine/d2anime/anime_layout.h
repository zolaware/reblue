/**
 * @file    engine/d2anime/anime_layout.h
 * @brief       Typed DSL for d2anime UI layouts - CSV generation and runtime
 * VarBag sync.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include "engine/d2anime/anime_data.h"

#include <string>
#include <variant>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

struct AnimeColorVal {
  u8 r, g, b, a;
};

class FloatV {
public:
  FloatV(const char *name, double defaultVal = 0.0)
      : name_(name), value_(defaultVal) {}

  const char *name() const { return name_; }
  double value() const { return value_; }
  void set(double v) { value_ = v; }

private:
  const char *name_;
  double value_;
};

class StringV {
public:
  StringV(const char *name, const char *defaultVal = "")
      : name_(name), value_(defaultVal) {}

  const char *name() const { return name_; }
  const std::string &value() const { return value_; }
  void set(const char *v) { value_ = v; }
  void set(const std::string &v) { value_ = v; }

private:
  const char *name_;
  std::string value_;
};

class ColorV {
public:
  ColorV(const char *name, u8 r, u8 g, u8 b, u8 a = 255)
      : name_(name), value_{r, g, b, a} {}

  const char *name() const { return name_; }
  AnimeColorVal value() const { return value_; }
  // The packing AnimeVarBag_SetColorRGBA reads, alpha in the high byte, which
  // is the CSV column order a color row is written in.
  u32 argb() const {
    return (u32(value_.a) << 24) | (u32(value_.r) << 16) |
           (u32(value_.g) << 8) | u32(value_.b);
  }
  void set(u8 r, u8 g, u8 b, u8 a = 255) { value_ = {r, g, b, a}; }
  void set(u32 argb) {
    value_.a = (argb >> 24) & 0xFF;
    value_.r = (argb >> 16) & 0xFF;
    value_.g = (argb >> 8) & 0xFF;
    value_.b = argb & 0xFF;
  }

private:
  const char *name_;
  AnimeColorVal value_;
};

// Column legends the disc's own CSVs carry above each block of element rows.
// Restated in full by every layout before this, and never quite identically.
inline constexpr const char *kColsPos = "type,name,x,y,w,h";
inline constexpr const char *kColsPosPri = "type,name,x,y,w,h,pri";
inline constexpr const char *kColsWindow =
    "type,frame start,frame interval,x,y,w,h,pri,window "
    "type,alpha,red,green,blue";
inline constexpr const char *kColsMessage =
    "type,frame start,frame interval,x,y,font w,font "
    "h,pri,alpha,red,green,blue,message";
inline constexpr const char *kColsTex =
    "type,frame start,frame interval,x,y,w,h,pri,alpha,u0,v0,u1,v1,file";
inline constexpr const char *kColsFrame =
    "type,frame start,frame interval,x,y,w,h,pri,alpha,red,green,blue";
inline constexpr const char *kColsMenu =
    "type,x,y,w,h,pri,name,StartCurX,StartCurY,CurDir,ItemW,ItemH,"
    "ItemAlpha,ItemOnType,ItemOffType,TableSizeRow,"
    "TableSizeColumn,defaultItem,TemplateAnime";
inline constexpr const char *kColsPanel =
    "type,frame start,frame interval,name,filename,loop";

struct AnimePos {
  const char *name;
  int x, y, w, h;
  int pri = 0; // optional 5th value (used by sysmes _PS)
  bool hasPri = false;
};

struct UVRect {
  float u0, v0, u1, v1;
};

// Cells of the button icon sheet res\cmn_help_menue, named the way the disc's
// own d2anime\Uv.csv names them. These are emitted as expressions rather than
// numbers on purpose: AnimeVarBag_FindVar special-cases the 'uv' prefix and
// resolves it against one process-wide bag, so a CSV naming a cell reads the
// same eleven variables every shipped footer reads. engine/glyph_set.cpp puts
// the keyboard block of the sheet behind these names.
//
// A uv. reference is copied at parse, not linked: the engine registers a live
// element link for every other variable, but the uv branch stores the value
// and registers nothing. Fine for a layout parsed on open, stale for one that
// outlives a rebind or a device switch. PromptGlyph covers that case.
//
// The engine resolves a tex path against the directory of the CSV naming it, so
// a layout served anywhere but d2anime's root reaches the sheet through the
// disc root escape the disc's own out-of-root footers use. Absolute is correct
// from every directory and is the only spelling here.
inline constexpr const char *kHelpSheetTex = ":d2anime\\res\\cmn_help_menue";

struct HelpUv {
  const char *u0, *v0, *u1, *v1;
};

#define BD_HELP_UV(name)                                                       \
  { "uv." name ".x", "uv." name ".y", "uv." name ".w", "uv." name ".h" }

inline constexpr HelpUv kHelpUvA = BD_HELP_UV("Help_A_Uv");
inline constexpr HelpUv kHelpUvB = BD_HELP_UV("Help_B_Uv");
inline constexpr HelpUv kHelpUvX = BD_HELP_UV("Help_X_Uv");
inline constexpr HelpUv kHelpUvY = BD_HELP_UV("Help_Y_Uv");
inline constexpr HelpUv kHelpUvBack = BD_HELP_UV("Help_BACK_Uv");
inline constexpr HelpUv kHelpUvCross = BD_HELP_UV("Help_CROSS_Uv");
inline constexpr HelpUv kHelpUvLB = BD_HELP_UV("Help_LB_Uv");
inline constexpr HelpUv kHelpUvRB = BD_HELP_UV("Help_RB_Uv");
inline constexpr HelpUv kHelpUvLT = BD_HELP_UV("Help_LT_Uv");
inline constexpr HelpUv kHelpUvRT = BD_HELP_UV("Help_RT_Uv");
inline constexpr HelpUv kHelpUvStart = BD_HELP_UV("Help_STARTUv");

#undef BD_HELP_UV

// A button cap carried in a layout's own element var instead of the uv. bag.
// The engine copies a uv. reference at parse rather than linking it, so a
// layout that outlives a rebind or a device switch declares a pos var per cap,
// references its fields from the tex row, and has its owner rewrite the var
// from Glyphs::CellUV on every glyph generation.
struct PromptGlyph {
  const char *posVar;   // the CSV element var carrying the cap's UV rect
  HelpUv uv;            // its four fields, as the tex rows reference them
  const char *helpName; // the Uv.csv prompt this cap stands for
};

struct AnimeMessage {
  const char *frameStart; // float var name gating visibility (e.g. "start")
  int frameInterval = 0;
  const char *posRef; // position name (e.g. "pos")
  int offsetX = 0, offsetY = 0;
  int fontW = 20, fontH = 26;
  int priority = 1;
  const char *alphaRef; // alpha expression (e.g. "alpha", "Color.a")
  const char *colorR;   // color expressions (e.g. "255", "Color.r")
  const char *colorG;
  const char *colorB;
  const char *contentVar;        // string var name whose value is rendered
  const char *font = nullptr;    // optional font type (e.g. "meiryo")
  const char *posType = nullptr; // optional alignment (e.g. "center")
  // Emitted in place of the literal priority. A menu widget overwrites its
  // template's pos element, so a row template must express depth relative to
  // it ("pos.pri-0.5") or the row window buries its own label.
  const char *priorityExpr = nullptr;
};

struct AnimeMessageAbs {
  const char *frameStart;
  int frameInterval = 0;
  int x, y;
  int fontW, fontH;
  int priority = 1;
  int alpha = 255;
  int r = 255, g = 255, b = 255;
  const char *contentVar;
  // Optional alignment. "center" makes x the midpoint instead of the left edge.
  const char *posType = nullptr;
  // Float var name driving the alpha, for content a host fades in and out.
  // Takes precedence over alpha.
  const char *alphaExpr = nullptr;
  // Single-op expression placed where y would go, for a row a page-wide float
  // var slides (cf. camp L_cfg01.csv "OffsetY+269").
  const char *yExpr = nullptr;
  // Color var name driving alpha and rgb together, the way a row template's
  // "Color" does. Takes precedence over alpha and r/g/b.
  const char *colorVar = nullptr;
  // Font name, for text that has to sit beside the stock button labels
  // ("meiryo"). Empty is the default font.
  const char *font = nullptr;
  // Emits the stock footer's trailing ',0,0,TRUE' (LineCnt, OffSetY, Ruby).
  // The ruby draw path reserves a furigana band above the glyphs, dropping
  // the shipped labels half a line below their y. A label sharing a footer
  // with them needs the same placement.
  bool ruby = false;
};

struct AnimeTex {
  const char *frameStart; // float var name gating visibility
  int frameInterval = 0;
  const char *posRef;
  int offsetX = 0, offsetY = 0;
  int w = 0, h = 0;
  int priority = 0;
  int alpha = 255;
  UVRect uv = {};
  const char *file;          // texture file path (e.g. "res\\mark_config")
  const char *tag = nullptr; // optional tag (e.g. "#ON")
  const char *priorityExpr = nullptr; // see AnimeMessage::priorityExpr
  // Float var names driving the width and the UV rect (a slider fill bar, cf.
  // camp L_cfg01.csv volume bars, or a cell of an atlas). Take precedence over
  // w / uv.
  const char *wExpr = nullptr;
  const char *u0Expr = nullptr;
  const char *v0Expr = nullptr;
  const char *u1Expr = nullptr;
  const char *v1Expr = nullptr;
  // Float var name driving the alpha. Takes precedence over alpha.
  const char *alphaExpr = nullptr;
};

struct AnimeTexAbs {
  const char *frameStart;
  int frameInterval = 0;
  int x, y, w, h;
  int priority = 0;
  int alpha = 255;
  UVRect uv = {};
  const char *file;
  // Float var name driving the alpha, for content a host fades in and out.
  // Takes precedence over alpha.
  const char *alphaExpr = nullptr;
  // Single-op expressions placed where x, y, w and u1 would go: a slider knob
  // rides "<Len>+543", its fill bar takes both its width and its UV from the
  // same pair of vars (cf. camp L_cfg02.csv).
  const char *xExpr = nullptr;
  const char *yExpr = nullptr;
  const char *wExpr = nullptr;
  const char *u1Expr = nullptr;
  // A named cell of the button icon sheet, which supplies all four UV columns
  // at once and takes precedence over uv and u1Expr.
  const HelpUv *helpUv = nullptr;
  // Fractional depth, which the stock slider art relies on to stack its track,
  // fill and knob.
  const char *priorityExpr = nullptr;
};

struct AnimeWindow {
  const char *frameStart; // float var name gating visibility
  int frameInterval = 0;
  const char *posRef;             // nullptr for absolute coords
  int x = 0, y = 0, w = 0, h = 0; // used when posRef is nullptr
  int priority = 0;
  const char *wndTypeVar; // string var name for window type (e.g. "WndType")
  const char *wndTypeLiteral = nullptr; // used when wndTypeVar is nullptr
  int alpha = 128;
  int r = 255, g = 255, b = 255;
  // posRef variant only: nonzero offsetX/offsetY shift from pos.x/pos.y, and
  // nonzero relW/relH override pos.w/pos.h with a fixed size (e.g. a button in
  // a wide row template). All zero reproduces the plain pos.x/y/w/h window.
  int offsetX = 0, offsetY = 0, relW = 0, relH = 0;
  // posRef variant only: float var name driving the width (e.g. a slider fill
  // bar). Takes precedence over relW.
  const char *relWExpr = nullptr;
  // Float var name driving the alpha. Takes precedence over alpha.
  const char *alphaExpr = nullptr;
  // Absolute variant only: single-op expression placed where y would go.
  const char *yExpr = nullptr;
};

struct AnimeFrame {
  const char *frameStart;
  int frameInterval = 0;
  int x = 0, y = 0, w = 0, h = 0;
  int priority = 0;
  const char *alphaRef; // e.g. "alpha-127", "alpha"
  int r = 255, g = 255, b = 255;
  const char *tag = nullptr; // optional tag (e.g. "#line")
  // Single-op expressions placed where y and w would go ("OffsetY+216",
  // "pso.w").
  const char *yExpr = nullptr;
  const char *wExpr = nullptr;
};

struct AnimeFrameRel {
  const char *frameStart;
  int frameInterval = 0;
  const char *posRef;
  int offsetX = 0, offsetY = 0;
  int w = 0, h = 0;
  int priority = 0;
  const char *alphaRef;
  int r = 255, g = 255, b = 255;
  // When set, emitted in place of the literal offsetX/w (e.g. a float var name
  // for a slider's knob position or fill width). Single-op exprs only.
  const char *offsetXExpr = nullptr;
  const char *wExpr = nullptr;
  const char *priorityExpr = nullptr; // see AnimeMessage::priorityExpr
};

// Timeline tween on a single variable, the engine's own way of animating an
// otherwise static element (cf. L_shw_cate_btn.csv): the element reads
// 'pos.x' every frame and this drives 'pos.x' from 'from' to 'to'. The enable
// field takes a var name or the literal "1".
struct AnimeTransition {
  int frameStart = 1;
  int frameInterval = 1;
  const char *kind = "move";
  std::string var;
  double from = 0.0, to = 0.0;
  const char *enable = "1";
};

// Child d2anime task reference.
struct AnimePanel {
  const char *name; // instance name (e.g. "detail")
  const char *csvFile;
  const char *frameStart = "start";
  int frameInterval = 0;
  // Last column of a d2anime row. An animated panel that ends on a transparent
  // frame, as the camp header icon does, vanishes without this.
  int loop = 0;
};

struct AnimeMenuWidget {
  const char *name; // menu name (e.g. "ModList")
  int x, y, w, h;
  int priority = 3;
  int startCurX, startCurY;
  const char *curDir = "RIGHT";
  int itemW, itemH;
  int itemAlpha = 128;
  const char *itemOnType;  // window type when selected (e.g. "FRAME01")
  const char *itemOffType; // window type when deselected (e.g. "BTN01_OF")
  int rows, cols;
  int defaultItem;
  const char *templateCSV;
};

using AnimeVarRef = std::variant<FloatV *, StringV *, ColorV *>;

class CsvBuilder {
public:
  // Keyframe role of an element row. The engine reads an element as a chain of
  // timeline segments (cf. S_dia_top_mgc.csv): Start opens the chain, To gives
  // the value the preceding segment tweens to, and Hold appends another
  // segment. Without a To the segment holds its own value.
  enum class Key {
    Start, // "message,<frame start>,<interval>,..."
    Hold,  // "message+,<frame start>,<interval>,..."
    To,    // "-,,,..." destination of the preceding segment
  };

  CsvBuilder &panel(const AnimePanel &p);

  CsvBuilder &var(const FloatV &v);
  CsvBuilder &var(const StringV &v);
  CsvBuilder &var(const ColorV &v);

  // Definition rows for a run of variables, in the order given.
  template <class... V> CsvBuilder &vars(const V &...vs) {
    (var(vs), ...);
    return *this;
  }

  CsvBuilder &pos(const AnimePos &p);

  CsvBuilder &window(const AnimeWindow &w, Key k = Key::Start);

  CsvBuilder &message(const AnimeMessage &m);
  CsvBuilder &message(const AnimeMessageAbs &m, Key k = Key::Start);

  CsvBuilder &tex(const AnimeTex &t);
  CsvBuilder &tex(const AnimeTexAbs &t, Key k = Key::Start);

  CsvBuilder &frame(const AnimeFrame &f, Key k = Key::Start);
  CsvBuilder &frame(const AnimeFrameRel &f);

  CsvBuilder &transition(const AnimeTransition &t);

  CsvBuilder &menu(const AnimeMenuWidget &m);

  CsvBuilder &menuSet(const AnimeMenuWidget &m, int index, const char *varName,
                      const char *value);
  CsvBuilder &menuSet(const char *menuName, int index, const char *varName,
                      const char *value);

  CsvBuilder &set(const AnimePanel &p, const char *varName, const char *value);

  // Free-form forms for child anime vars ("ItmCatbtn01.FadeOutFlg"): 'set'
  // binds to another var, 'setVal' writes a literal.
  CsvBuilder &set(const std::string &target, const std::string &value);
  CsvBuilder &setVal(const std::string &target, double value);
  // Multi-value form ("setval,cfg_icon.pos,115,35,64,64,0").
  CsvBuilder &setVal(const std::string &target, const std::string &values);

  CsvBuilder &comment(const char *text);
  CsvBuilder &comment(const std::string &text);

  CsvBuilder &blank();

  std::string build() const { return csv_; }

private:
  void line(const std::string &content);
  std::string csv_;
};

class AnimeLayout {
public:
  virtual ~AnimeLayout() = default;

  virtual void build(CsvBuilder &b) = 0;

  std::string ToCSV();

  void SyncVars(AnimeData varBag);

  template <class V> void RegisterVar(V &v) { vars_.push_back(&v); }

  // Registers a run of variables without emitting them, for a value the CSV
  // declares elsewhere.
  template <class... V> void Bind(V &...vs) { (RegisterVar(vs), ...); }

  // Emits each variable's definition row and registers it for SyncVars. A var
  // the host never writes stays a plain b.var(), keeping a template's per-item
  // 'start' and 'alpha' out of the layout-wide sync.
  template <class... V> void Declare(CsvBuilder &b, V &...vs) {
    (b.var(vs), ...);
    (RegisterVar(vs), ...);
  }

protected:
  std::vector<AnimeVarRef> vars_;
};

// The skeleton every list row template shares: the pos, the full-cell window
// the engine drives through WndType for the cursor highlight, and a left label.
// A subclass declares the variables it adds and draws its value widgets.
//
// The label's vertical offset is derived from the row height rather than tuned
// per template, so changing a list's pitch moves every label with it. Six
// templates each carrying their own offset is how a pitch change ends up
// looking right in some lists and wrong in others.
class RowTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV name{"Name", ""};
  StringV wndType{"WndType", "NOWINDOW"};

  void build(CsvBuilder &b) final;

protected:
  // A nonzero pri gives the row's pos a depth of its own, which a template
  // whose elements state their depth against 'pos.pri' has to have.
  RowTemplate(int x, int y, int w, int h, int pri = 0)
      : pos_{"pos", x, y, w, h, pri, pri != 0} {}

  // Variables the row adds beyond start, alpha, Name and WndType.
  virtual void declareVars(CsvBuilder &) {}

  // Everything drawn to the right of the label.
  virtual void buildValue(CsvBuilder &) {}

  // Overridden where a row wants a narrower highlight than its cell, or none.
  virtual AnimeWindow rowWindow() const;
  virtual bool hasWindow() const { return true; }

  // Overridden for a different inset, font or color source.
  virtual AnimeMessage rowLabel() const;

  // Offset that centers a fontH-tall line in the row.
  int CenterY(int fontH) const { return (pos_.h - fontH) / 2; }

  AnimePos pos_;
};

// A row a list marks on or off. AnimeMenu::SetToggleRow writes Color, ChkOn
// and ChkOff by name every frame, and AnimeMenu_Init reads EnableColor and
// DisableColor off the template once, so a list that declared its own set could
// drift from the host's spelling with nothing to catch it.
class ToggleRowTemplate : public RowTemplate {
public:
  FloatV chkOn{"ChkOn", -1.0};
  FloatV chkOff{"ChkOff", -1.0};
  ColorV color{"Color", 255, 255, 255, 255};
  ColorV enableColor{"EnableColor", 140, 255, 140, 255};
  ColorV disableColor{"DisableColor", 127, 127, 127, 255};

  // The gate and the atlas tag of one state. A row draws its checkbox by
  // walking this, which keeps the '#ON' cell off the ChkOff float.
  struct CheckState {
    const char *gate, *tag;
  };
  static constexpr CheckState kCheckStates[] = {{"ChkOn", "#ON"},
                                                {"ChkOff", "#OFF"}};

protected:
  using RowTemplate::RowTemplate;

  void declareVars(CsvBuilder &b) override;

  // Tinted by Color, which carries SetToggleRow's per-row color into the row's
  // own text.
  AnimeMessage rowLabel() const override;
};

} // namespace bd::engine
