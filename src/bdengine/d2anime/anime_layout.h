/**
 * @file        bdengine/anime_layout.h
 * @brief       Typed DSL for d2anime UI layouts - CSV generation and
 *              runtime VarBag sync.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/d2anime/d2anime.h"
#include "bdengine/d2anime/d2anime_types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <rex/types.h>
#include <rex/memory/utils.h>

namespace bd {

/**
 * @brief AnimeVar<T> - named d2anime variable (string, float, color).
 */
struct AnimeColorVal {
  u8 r, g, b, a;
};

// Tag types for AnimeVar
struct FloatVar {};
struct StringVar {};
struct ColorVar {};

template <typename Tag> class AnimeVar;

template <> class AnimeVar<FloatVar> {
public:
  AnimeVar(const char *name, double defaultVal = 0.0)
      : name_(name), value_(defaultVal), default_(defaultVal) {}

  const char *name() const { return name_; }
  double value() const { return value_; }
  void set(double v) { value_ = v; }
  void reset() { value_ = default_; }

private:
  const char *name_;
  double value_;
  double default_;
};

template <> class AnimeVar<StringVar> {
public:
  AnimeVar(const char *name, const char *defaultVal = "")
      : name_(name), value_(defaultVal), default_(defaultVal) {}

  const char *name() const { return name_; }
  const std::string &value() const { return value_; }
  void set(const char *v) { value_ = v; }
  void set(const std::string &v) { value_ = v; }
  void reset() { value_ = default_; }

private:
  const char *name_;
  std::string value_;
  std::string default_;
};

template <> class AnimeVar<ColorVar> {
public:
  AnimeVar(const char *name, u8 r, u8 g, u8 b, u8 a = 255)
      : name_(name), value_{r, g, b, a}, default_{r, g, b, a} {}

  const char *name() const { return name_; }
  AnimeColorVal value() const { return value_; }
  u32 rgba() const {
    return (u32(value_.r) << 24) | (u32(value_.g) << 16) |
           (u32(value_.b) << 8) | u32(value_.a);
  }
  void set(u8 r, u8 g, u8 b, u8 a = 255) {
    value_ = {r, g, b, a};
  }
  void set(u32 rgba) {
    value_.r = (rgba >> 24) & 0xFF;
    value_.g = (rgba >> 16) & 0xFF;
    value_.b = (rgba >> 8) & 0xFF;
    value_.a = rgba & 0xFF;
  }
  void reset() { value_ = default_; }

private:
  const char *name_;
  AnimeColorVal value_;
  AnimeColorVal default_;
};

// Convenience aliases
using FloatV = AnimeVar<FloatVar>;
using StringV = AnimeVar<StringVar>;
using ColorV = AnimeVar<ColorVar>;

/**
 * @brief Named position block.
 */
struct AnimePos {
  const char *name;
  int x, y, w, h;
  int pri = 0;          // optional 5th value (used by sysmes _PS)
  bool hasPri = false;
};

/**
 * @brief UV rectangle for tex elements.
 */
struct UVRect {
  float u0, v0, u1, v1;
};

/**
 * @brief Text element (pos-relative).
 */
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
  const char *contentVar; // string var name whose value is rendered
};

/**
 * @brief Text element with absolute coordinates.
 */
struct AnimeMessageAbs {
  const char *frameStart;
  int frameInterval = 0;
  int x, y;
  int fontW, fontH;
  int priority = 1;
  int alpha = 255;
  int r = 255, g = 255, b = 255;
  const char *contentVar;
};

/**
 * @brief Texture element (pos-relative).
 */
struct AnimeTex {
  const char *frameStart; // float var name gating visibility
  int frameInterval = 0;
  const char *posRef; // position name
  int offsetX = 0, offsetY = 0;
  int w = 0, h = 0;
  int priority = 0;
  int alpha = 255;
  UVRect uv = {};
  const char *file;          // texture file path (e.g. "res\\mark_config")
  const char *tag = nullptr; // optional tag (e.g. "#ON")
};

/**
 * @brief Texture element with absolute coordinates.
 */
struct AnimeTexAbs {
  const char *frameStart;
  int frameInterval = 0;
  int x, y, w, h;
  int priority = 0;
  int alpha = 255;
  UVRect uv = {};
  const char *file;
};

/**
 * @brief Window/frame element.
 */
struct AnimeWindow {
  const char *frameStart; // float var name gating visibility
  int frameInterval = 0;
  const char *posRef; // position name, or nullptr for absolute coords
  int x = 0, y = 0, w = 0,
      h = 0; // absolute coords (used when posRef is nullptr)
  int priority = 0;
  const char *wndTypeVar; // string var name for window type (e.g. "WndType")
  const char *wndTypeLiteral =
      nullptr; // literal window type (used when wndTypeVar is nullptr)
  int alpha = 128;
  int r = 255, g = 255, b = 255;
};

/**
 * @brief Decorative frame/line (absolute coords).
 */
struct AnimeFrame {
  const char *frameStart;
  int frameInterval = 0;
  int x = 0, y = 0, w = 0, h = 0;
  int priority = 0;
  const char *alphaRef; // e.g. "alpha-127", "alpha"
  int r = 255, g = 255, b = 255;
  const char *tag = nullptr; // optional tag (e.g. "#line")
};

/**
 * @brief Decorative frame/line with pos-relative coordinates.
 */
struct AnimeFrameRel {
  const char *frameStart;
  int frameInterval = 0;
  const char *posRef;
  int offsetX = 0, offsetY = 0;
  int w = 0, h = 0;
  int priority = 0;
  const char *alphaRef;
  int r = 255, g = 255, b = 255;
};

/**
 * @brief Child d2anime task reference.
 */
struct AnimePanel {
  const char *name;    // instance name (e.g. "detail")
  const char *csvFile; // CSV file path (e.g. "l_modmgr_detail.csv")
  const char *frameStart = "start";
  int frameInterval = 0;
  int sortKey = 0;
};

/**
 * @brief Menu definition.
 */
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
  const char *templateCsv; // template CSV file path

  // Runtime: guest menu address (discovered after load)
  u32 guestAddr = 0;

  /**
   * @brief Iterate template VarBags. Callback receives (index, varBagAddr).
   */
  void forEach(u8 *base,
               std::function<void(int, u32)> callback) const;

  /**
   * @brief Set AnimeItemData.enabled for a specific item index.
   */
  void setItemEnabled(u8 *base, int index, bool enabled) const;

  /**
   * @brief Read cursor index from guest AnimeMenu.
   */
  int cursorIndex(u8 *base) const;

  /**
   * @brief Read enable/disable colors from guest AnimeMenu.
   */
  u32 enableColor(u8 *base) const;
  u32 disableColor(u8 *base) const;

  /**
   * @brief Set activeFlag, deselectAll, cursorShowA, needsRebuild on guest AnimeMenu.
   */
  void setActive(u8 *base, bool active) const;
};

/**
 * @brief Type-erased reference to any AnimeVar for sync.
 */
using AnimeVarRef = std::variant<FloatV *, StringV *, ColorV *>;

/**
 * @brief Accumulates CSV rows from typed members.
 */
class CsvBuilder {
public:
  CsvBuilder &panel(const AnimePanel &p);

  CsvBuilder &var(const FloatV &v);
  CsvBuilder &var(const StringV &v);
  CsvBuilder &var(const ColorV &v);

  CsvBuilder &pos(const AnimePos &p);

  CsvBuilder &window(const AnimeWindow &w);

  CsvBuilder &message(const AnimeMessage &m);
  CsvBuilder &message(const AnimeMessageAbs &m);

  CsvBuilder &tex(const AnimeTex &t);
  CsvBuilder &tex(const AnimeTexAbs &t);

  CsvBuilder &frame(const AnimeFrame &f);
  CsvBuilder &frame(const AnimeFrameRel &f);

  CsvBuilder &menu(const AnimeMenuWidget &m);

  CsvBuilder &menuSet(const AnimeMenuWidget &m, int index, const char *varName,
                      const char *value);

  CsvBuilder &set(const AnimePanel &p, const char *varName, const char *value);

  CsvBuilder &comment(const char *text);

  CsvBuilder &blank();

  std::string build() const { return csv_; }

private:
  void line(const std::string &content);
  std::string csv_;
};

/**
 * @brief Abstract base class.
 */
class AnimeLayout {
public:
  virtual ~AnimeLayout() = default;

  /**
   * @brief Override to define the layout structure using CsvBuilder.
   */
  virtual void build(CsvBuilder &b) = 0;

  /**
   * @brief Generate the CSV string. Calls build() internally.
   */
  std::string toCSV();

  /**
   * @brief Write all registered vars to the task's VarBag.
   */
  void syncVars(u32 taskAddr);

  void registerVar(FloatV &v) { vars_.push_back(&v); }
  void registerVar(StringV &v) { vars_.push_back(&v); }
  void registerVar(ColorV &v) { vars_.push_back(&v); }

protected:
  std::vector<AnimeVarRef> vars_;
};

} // namespace bd
