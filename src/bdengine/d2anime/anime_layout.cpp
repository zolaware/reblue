/**
 * @file        bdengine/anime_layout.cpp
 * @brief       CsvBuilder serialization, AnimeLayout sync, AnimeMenuWidget
 *              runtime helpers.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/d2anime/anime_layout.h"
#include "bdengine/common/logging.h"

#include <format>
#include <string>

#include <rex/types.h>
#include <rex/memory/utils.h>

using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

namespace bd {

/**
 * @brief CsvBuilder: line helper.
 */
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

// CsvBuilder element emitters
CsvBuilder &CsvBuilder::panel(const AnimePanel &p) {
  line(std::format("d2anime,{},{},{},{},{}", p.frameStart, p.frameInterval,
                   p.name, p.csvFile, p.sortKey));
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
  // CSV format: color,name,alpha,red,green,blue (matches engine parser)
  line(std::format("color,{},{},{},{},{}", v.name(), c.a, c.r, c.g, c.b));
  return *this;
}

CsvBuilder &CsvBuilder::pos(const AnimePos &p) {
  if (p.hasPri)
    line(std::format("pos,{},{},{},{},{},{}", p.name, p.x, p.y, p.w, p.h, p.pri));
  else
    line(std::format("pos,{},{},{},{},{}", p.name, p.x, p.y, p.w, p.h));
  return *this;
}

CsvBuilder &CsvBuilder::window(const AnimeWindow &w) {
  if (w.posRef) {
    const char *wndType = w.wndTypeVar ? w.wndTypeVar : w.wndTypeLiteral;
    line(std::format("window,{},{},{}.x,{}.y,{}.w,{}.h,{}.pri,{},{},{},{},{}",
                     w.frameStart, w.frameInterval, w.posRef, w.posRef,
                     w.posRef, w.posRef, w.posRef, wndType, w.alpha, w.r, w.g,
                     w.b));
  } else {
    const char *wndType =
        w.wndTypeVar ? w.wndTypeVar
                     : (w.wndTypeLiteral ? w.wndTypeLiteral : "BTN01_OF");
    line(std::format("window,{},{},{},{},{},{},{},{},{},{},{},{}", w.frameStart,
                     w.frameInterval, w.x, w.y, w.w, w.h, w.priority, wndType,
                     w.alpha, w.r, w.g, w.b));
  }
  return *this;
}

CsvBuilder &CsvBuilder::message(const AnimeMessage &m) {
  line(std::format("message,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{},{}",
                   m.frameStart, m.frameInterval, m.posRef, m.offsetX, m.posRef,
                   m.offsetY, m.fontW, m.fontH, m.priority, m.alphaRef,
                   m.colorR, m.colorG, m.colorB, m.contentVar));
  return *this;
}

CsvBuilder &CsvBuilder::message(const AnimeMessageAbs &m) {
  line(std::format("message,{},{},{},{},{},{},{},{},{},{},{},{}", m.frameStart,
                   m.frameInterval, m.x, m.y, m.fontW, m.fontH, m.priority,
                   m.alpha, m.r, m.g, m.b, m.contentVar));
  return *this;
}

CsvBuilder &CsvBuilder::tex(const AnimeTex &t) {
  std::string row = std::format(
      "tex,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{},{},{}", t.frameStart,
      t.frameInterval, t.posRef, t.offsetX, t.posRef, t.offsetY, t.w, t.h,
      t.priority, t.alpha, t.uv.u0, t.uv.v0, t.uv.u1, t.uv.v1, t.file);
  if (t.tag) {
    row += ',';
    row += t.tag;
  }
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::tex(const AnimeTexAbs &t) {
  line(std::format("tex,{},{},{},{},{},{},{},{},{},{},{},{},{}", t.frameStart,
                   t.frameInterval, t.x, t.y, t.w, t.h, t.priority, t.alpha,
                   t.uv.u0, t.uv.v0, t.uv.u1, t.uv.v1, t.file));
  return *this;
}

CsvBuilder &CsvBuilder::frame(const AnimeFrame &f) {
  std::string row = std::format("frame,{},{},{},{},{},{},{},{},{},{},{}",
                                f.frameStart, f.frameInterval, f.x, f.y, f.w,
                                f.h, f.priority, f.alphaRef, f.r, f.g, f.b);
  if (f.tag) {
    row += ',';
    row += f.tag;
  }
  line(row);
  return *this;
}

CsvBuilder &CsvBuilder::frame(const AnimeFrameRel &f) {
  line(std::format("frame,{},{},{}.x+{},{}.y+{},{},{},{},{},{},{},{}",
                   f.frameStart, f.frameInterval, f.posRef, f.offsetX, f.posRef,
                   f.offsetY, f.w, f.h, f.priority, f.alphaRef, f.r, f.g, f.b));
  return *this;
}

CsvBuilder &CsvBuilder::menu(const AnimeMenuWidget &m) {
  line(std::format("menu,{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                   m.x, m.y, m.w, m.h, m.priority, m.name, m.startCurX,
                   m.startCurY, m.curDir, m.itemW, m.itemH, m.itemAlpha,
                   m.itemOnType, m.itemOffType, m.rows, m.cols, m.defaultItem,
                   m.templateCsv));
  return *this;
}

CsvBuilder &CsvBuilder::menuSet(const AnimeMenuWidget &m, int index,
                                const char *varName, const char *value) {
  line(std::format("menu_set,{},{},{},{}", m.name, index, varName, value));
  return *this;
}

CsvBuilder &CsvBuilder::set(const AnimePanel &p, const char *varName,
                            const char *value) {
  line(std::format("set,{}.{},{}", p.name, varName, value));
  return *this;
}

CsvBuilder &CsvBuilder::comment(const char *text) {
  line(std::format("#{}", text));
  return *this;
}

CsvBuilder &CsvBuilder::blank() {
  csv_ += "\r\n";
  return *this;
}

// AnimeLayout
std::string AnimeLayout::toCSV() {
  vars_.clear();
  CsvBuilder b;
  build(b);
  return b.build();
}

void AnimeLayout::syncVars(u32 taskAddr) {
  u32 varBag = taskAddr + kAnimeVarBagOffset;
  for (auto &ref : vars_) {
    std::visit(
        [&](auto *var) {
          using T = std::remove_pointer_t<decltype(var)>;
          if constexpr (std::is_same_v<T, FloatV>) {
            VarBagSetFloat(varBag, var->name(), var->value());
          } else if constexpr (std::is_same_v<T, StringV>) {
            VarBagSetString(varBag, var->name(), var->value().c_str());
          } else if constexpr (std::is_same_v<T, ColorV>) {
            VarBagSetColor(varBag, var->name(), var->rgba());
          }
        },
        ref);
  }
}

// AnimeMenuWidget runtime helpers
void AnimeMenuWidget::forEach(
    u8 *base,
    std::function<void(int, u32)> callback) const {
  if (!guestAddr)
    return;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  u32 tplBegin = menu->templateBegin;
  u32 tplEnd = menu->templateEnd;
  if (!tplBegin || tplBegin == tplEnd)
    return;

  u32 count = (tplEnd - tplBegin) / 4;
  for (u32 i = 0; i < count; ++i) {
    u32 tplTask = load_and_swap<u32>(base + tplBegin + i * 4);
    if (!tplTask)
      continue;
    u32 varBag = tplTask + kAnimeVarBagOffset;
    callback(static_cast<int>(i), varBag);
  }
}

void AnimeMenuWidget::setItemEnabled(u8 *base, int index,
                                     bool enabled) const {
  if (!guestAddr)
    return;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  u32 idBegin = menu->itemDataBegin;
  u32 idEnd = menu->itemDataEnd;
  if (!idBegin || idBegin == idEnd)
    return;

  u32 count = (idEnd - idBegin) / 4;
  if (static_cast<u32>(index) >= count)
    return;

  u32 itemPtr = load_and_swap<u32>(base + idBegin + index * 4);
  auto *item = reinterpret_cast<AnimeItemData_t *>(base + itemPtr);
  item->enabled = enabled ? 1 : 0;
}

int AnimeMenuWidget::cursorIndex(u8 *base) const {
  if (!guestAddr)
    return 0;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  return static_cast<int>(menu->cursorIndex);
}

u32 AnimeMenuWidget::enableColor(u8 *base) const {
  if (!guestAddr)
    return 0xFFFFFFFF;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  return static_cast<u32>(menu->enableColor);
}

u32 AnimeMenuWidget::disableColor(u8 *base) const {
  if (!guestAddr)
    return 0x7F7F7FFF;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  return static_cast<u32>(menu->disableColor);
}

void AnimeMenuWidget::setActive(u8 *base, bool active) const {
  if (!guestAddr)
    return;
  auto *menu = reinterpret_cast<AnimeMenu_t *>(base + guestAddr);
  menu->activeFlag = active ? 1 : 0;
  menu->deselectAll = active ? 0 : 1;
  menu->cursorShowA = active ? 1 : 0;
  menu->needsRebuild = 1;
}

} // namespace bd
