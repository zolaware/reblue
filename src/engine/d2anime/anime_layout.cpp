/**
 * @file    engine/d2anime/anime_layout.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_layout.h"
#include "engine/d2anime/anime_data.h"

#include <format>
#include <string>

#include <rex/types.h>

namespace bd::engine {

namespace {

// The 'type,frame start,frame interval' prefix every element row carries. A
// To row drops the timing columns and names no type, so the engine reads it as
// the destination of the segment above it.
std::string KeyHead(CsvBuilder::Key k, const char *type, const char *frameStart,
                    int frameInterval) {
  switch (k) {
  case CsvBuilder::Key::Hold:
    return std::format("{}+,{},{}", type, frameStart, frameInterval);
  case CsvBuilder::Key::To:
    return "-,,";
  default:
    return std::format("{},{},{}", type, frameStart, frameInterval);
  }
}

} // namespace

void CsvBuilder::line(const std::string &content) {
  csv_ += content;
  int commas = 0;
  for (char c : content)
    if (c == ',')
      ++commas;
  for (int i = commas; i < 18; ++i)
    csv_ += ',';
  csv_ += "\r\n";
}

CsvBuilder &CsvBuilder::panel(const AnimePanel &p) {
  line(std::format("d2anime,{},{},{},{},{}", p.frameStart, p.frameInterval,
                   p.name, p.csvFile, p.loop));
  return *this;
}

CsvBuilder &CsvBuilder::var(const FloatV &v) {
  line(std::format("float,{},{}", v.name(), v.value()));
  return *this;
}

CsvBuilder &CsvBuilder::var(const StringV &v) {
  line(std::format("string,{},{}", v.name(), v.value()));
  return *this;
}

CsvBuilder &CsvBuilder::var(const ColorV &v) {
  auto c = v.value();
  // Engine parser order: color,name,alpha,red,green,blue
  line(std::format("color,{},{},{},{},{}", v.name(), c.a, c.r, c.g, c.b));
  return *this;
}

CsvBuilder &CsvBuilder::pos(const AnimePos &p) {
  if (p.hasPri)
    line(std::format("pos,{},{},{},{},{},{}", p.name, p.x, p.y, p.w, p.h,
                     p.pri));
  else
    line(std::format("pos,{},{},{},{},{}", p.name, p.x, p.y, p.w, p.h));
  return *this;
}

CsvBuilder &CsvBuilder::window(const AnimeWindow &w, Key k) {
  std::string alpha =
      w.alphaExpr ? std::string(w.alphaExpr) : std::format("{}", w.alpha);
  if (w.posRef) {
    const char *wndType = w.wndTypeVar ? w.wndTypeVar : w.wndTypeLiteral;
    std::string xf = w.offsetX ? std::format("{}.x+{}", w.posRef, w.offsetX)
                               : std::format("{}.x", w.posRef);
    std::string yf = w.offsetY ? std::format("{}.y+{}", w.posRef, w.offsetY)
                               : std::format("{}.y", w.posRef);
    std::string wf = w.relWExpr ? std::string(w.relWExpr)
                     : w.relW   ? std::format("{}", w.relW)
                                : std::format("{}.w", w.posRef);
    std::string hf =
        w.relH ? std::format("{}", w.relH) : std::format("{}.h", w.posRef);
    line(std::format("{},{},{},{},{},{}.pri,{},{},{},{},{}",
                     KeyHead(k, "window", w.frameStart, w.frameInterval), xf,
                     yf, wf, hf, w.posRef, wndType, alpha, w.r, w.g, w.b));
  } else {
    const char *wndType =
        w.wndTypeVar ? w.wndTypeVar
                     : (w.wndTypeLiteral ? w.wndTypeLiteral : "BTN01_OF");
    std::string yf =
        w.yExpr ? std::string(w.yExpr) : std::format("{}", w.y);
    line(std::format("{},{},{},{},{},{},{},{},{},{},{}",
                     KeyHead(k, "window", w.frameStart, w.frameInterval), w.x,
                     yf, w.w, w.h, w.priority, wndType, alpha, w.r, w.g, w.b));
  }
  return *this;
}

CsvBuilder &CsvBuilder::message(const AnimeMessage &m) {
  std::string pri = m.priorityExpr ? std::string(m.priorityExpr)
                                   : std::format("{}", m.priority);
  std::string row = std::format(
      "message,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{},{}", m.frameStart,
      m.frameInterval, m.posRef, m.offsetX, m.posRef, m.offsetY, m.fontW,
      m.fontH, pri, m.alphaRef, m.colorR, m.colorG, m.colorB, m.contentVar);
  if (m.font || m.posType) {
    row += ',';
    row += m.font ? m.font : "";
    row += ',';
    row += m.posType ? m.posType : "";
  }
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::message(const AnimeMessageAbs &m, Key k) {
  std::string alpha =
      m.alphaExpr ? std::string(m.alphaExpr) : std::format("{}", m.alpha);
  std::string yf = m.yExpr ? std::string(m.yExpr) : std::format("{}", m.y);
  std::string r = std::format("{}", m.r);
  std::string g = std::format("{}", m.g);
  std::string bl = std::format("{}", m.b);
  if (m.colorVar) {
    alpha = std::format("{}.a", m.colorVar);
    r = std::format("{}.r", m.colorVar);
    g = std::format("{}.g", m.colorVar);
    bl = std::format("{}.b", m.colorVar);
  }
  std::string row = std::format(
      "{},{},{},{},{},{},{},{},{},{},{}",
      KeyHead(k, "message", m.frameStart, m.frameInterval), m.x, yf, m.fontW,
      m.fontH, m.priority, alpha, r, g, bl, m.contentVar);
  // Column 13 is the font name, 14 the alignment, then LineCnt, OffSetY, Ruby.
  if (m.posType || m.font || m.ruby) {
    row += ',';
    row += m.font ? m.font : "";
    row += ',';
    row += m.posType ? m.posType : "";
  }
  if (m.ruby)
    row += ",0,0,TRUE";
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::tex(const AnimeTex &t) {
  std::string wf = t.wExpr ? std::string(t.wExpr) : std::format("{}", t.w);
  std::string u0f =
      t.u0Expr ? std::string(t.u0Expr) : std::format("{}", t.uv.u0);
  std::string v0f =
      t.v0Expr ? std::string(t.v0Expr) : std::format("{}", t.uv.v0);
  std::string u1f =
      t.u1Expr ? std::string(t.u1Expr) : std::format("{}", t.uv.u1);
  std::string v1f =
      t.v1Expr ? std::string(t.v1Expr) : std::format("{}", t.uv.v1);
  std::string pri = t.priorityExpr ? std::string(t.priorityExpr)
                                   : std::format("{}", t.priority);
  std::string alpha =
      t.alphaExpr ? std::string(t.alphaExpr) : std::format("{}", t.alpha);
  std::string row =
      std::format("tex,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{},{},{}",
                  t.frameStart, t.frameInterval, t.posRef, t.offsetX, t.posRef,
                  t.offsetY, wf, t.h, pri, alpha, u0f, v0f, u1f, v1f, t.file);
  if (t.tag) {
    row += ',';
    row += t.tag;
  }
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::tex(const AnimeTexAbs &t, Key k) {
  std::string alpha =
      t.alphaExpr ? std::string(t.alphaExpr) : std::format("{}", t.alpha);
  std::string xf = t.xExpr ? std::string(t.xExpr) : std::format("{}", t.x);
  std::string yf = t.yExpr ? std::string(t.yExpr) : std::format("{}", t.y);
  std::string wf = t.wExpr ? std::string(t.wExpr) : std::format("{}", t.w);
  std::string u0f = t.helpUv ? std::string(t.helpUv->u0)
                             : std::format("{}", t.uv.u0);
  std::string v0f = t.helpUv ? std::string(t.helpUv->v0)
                             : std::format("{}", t.uv.v0);
  std::string u1f = t.helpUv  ? std::string(t.helpUv->u1)
                    : t.u1Expr ? std::string(t.u1Expr)
                               : std::format("{}", t.uv.u1);
  std::string v1f = t.helpUv ? std::string(t.helpUv->v1)
                             : std::format("{}", t.uv.v1);
  std::string pri = t.priorityExpr ? std::string(t.priorityExpr)
                                   : std::format("{}", t.priority);
  line(std::format("{},{},{},{},{},{},{},{},{},{},{},{}",
                   KeyHead(k, "tex", t.frameStart, t.frameInterval), xf, yf, wf,
                   t.h, pri, alpha, u0f, v0f, u1f, v1f, t.file));
  return *this;
}

CsvBuilder &CsvBuilder::frame(const AnimeFrame &f, Key k) {
  std::string yf = f.yExpr ? std::string(f.yExpr) : std::format("{}", f.y);
  std::string wf = f.wExpr ? std::string(f.wExpr) : std::format("{}", f.w);
  std::string row =
      std::format("{},{},{},{},{},{},{},{},{},{}",
                  KeyHead(k, "frame", f.frameStart, f.frameInterval), f.x, yf,
                  wf, f.h, f.priority, f.alphaRef, f.r, f.g, f.b);
  if (f.tag) {
    row += ',';
    row += f.tag;
  }
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::frame(const AnimeFrameRel &f) {
  std::string xExpr =
      f.offsetXExpr ? f.offsetXExpr : std::format("{}", f.offsetX);
  std::string wExpr = f.wExpr ? f.wExpr : std::format("{}", f.w);
  std::string pri = f.priorityExpr ? std::string(f.priorityExpr)
                                   : std::format("{}", f.priority);
  line(std::format("frame,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{}",
                   f.frameStart, f.frameInterval, f.posRef, xExpr, f.posRef,
                   f.offsetY, wExpr, f.h, pri, f.alphaRef, f.r, f.g, f.b));
  return *this;
}

CsvBuilder &CsvBuilder::transition(const AnimeTransition &t) {
  line(std::format("trans,{},{},{},{},{},{},{}", t.frameStart, t.frameInterval,
                   t.kind, t.var, t.from, t.to, t.enable));
  return *this;
}

CsvBuilder &CsvBuilder::menu(const AnimeMenuWidget &m) {
  line(std::format("menu,{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                   m.x, m.y, m.w, m.h, m.priority, m.name, m.startCurX,
                   m.startCurY, m.curDir, m.itemW, m.itemH, m.itemAlpha,
                   m.itemOnType, m.itemOffType, m.rows, m.cols, m.defaultItem,
                   m.templateCSV));
  return *this;
}

CsvBuilder &CsvBuilder::menuSet(const AnimeMenuWidget &m, int index,
                                const char *varName, const char *value) {
  return menuSet(m.name, index, varName, value);
}

CsvBuilder &CsvBuilder::menuSet(const char *menuName, int index,
                                const char *varName, const char *value) {
  line(std::format("menu_set,{},{},{},{}", menuName, index, varName, value));
  return *this;
}

CsvBuilder &CsvBuilder::set(const AnimePanel &p, const char *varName,
                            const char *value) {
  line(std::format("set,{}.{},{}", p.name, varName, value));
  return *this;
}

CsvBuilder &CsvBuilder::set(const std::string &target,
                            const std::string &value) {
  line(std::format("set,{},{}", target, value));
  return *this;
}

CsvBuilder &CsvBuilder::setVal(const std::string &target, double value) {
  line(std::format("setval,{},{}", target, value));
  return *this;
}

CsvBuilder &CsvBuilder::setVal(const std::string &target,
                               const std::string &values) {
  line(std::format("setval,{},{}", target, values));
  return *this;
}

CsvBuilder &CsvBuilder::comment(const char *text) {
  line(std::format("#{}", text));
  return *this;
}

CsvBuilder &CsvBuilder::comment(const std::string &text) {
  line(std::format("#{}", text));
  return *this;
}

CsvBuilder &CsvBuilder::blank() {
  csv_ += "\r\n";
  return *this;
}

std::string AnimeLayout::ToCSV() {
  vars_.clear();
  CsvBuilder b;
  build(b);
  return b.build();
}

void AnimeLayout::SyncVars(AnimeData varBag) {
  for (auto &ref : vars_) {
    std::visit(
        [&](auto *var) {
          using T = std::remove_pointer_t<decltype(var)>;
          if constexpr (std::is_same_v<T, FloatV>) {
            varBag.SetFloat(var->name(), var->value());
          } else if constexpr (std::is_same_v<T, StringV>) {
            varBag.SetText(var->name(), var->value());
          } else if constexpr (std::is_same_v<T, ColorV>) {
            varBag.SetColor(var->name(), var->argb());
          }
        },
        ref);
  }
}

AnimeWindow RowTemplate::rowWindow() const {
  return {.frameStart = "start",
          .posRef = "pos",
          .wndTypeVar = "WndType",
          .alpha = 128};
}

AnimeMessage RowTemplate::rowLabel() const {
  constexpr int kInset = 20;
  AnimeMessage m{.frameStart = "start",
                 .posRef = "pos",
                 .offsetX = kInset,
                 .alphaRef = "alpha",
                 .colorR = "255",
                 .colorG = "255",
                 .colorB = "255",
                 .contentVar = "Name"};
  m.offsetY = CenterY(m.fontH);
  return m;
}

void RowTemplate::build(CsvBuilder &b) {
  b.comment("variable definitions").vars(start, alpha).blank().vars(name,
                                                                    wndType);
  declareVars(b);
  b.blank().comment(kColsPos).pos(pos_).blank();

  if (hasWindow())
    b.comment(kColsWindow).window(rowWindow()).blank();

  b.comment(kColsMessage).message(rowLabel()).blank();
  buildValue(b);
}

void ToggleRowTemplate::declareVars(CsvBuilder &b) {
  b.vars(chkOn, chkOff, color, enableColor, disableColor);
}

AnimeMessage ToggleRowTemplate::rowLabel() const {
  AnimeMessage m = RowTemplate::rowLabel();
  m.alphaRef = "Color.a";
  m.colorR = "Color.r";
  m.colorG = "Color.g";
  m.colorB = "Color.b";
  return m;
}

} // namespace bd::engine
