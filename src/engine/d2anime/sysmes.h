/**
 * @file    engine/d2anime/sysmes.h
 * @brief   Wrapper over the engine's yes/no confirmation popup.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include "engine/d2anime/anime_layout.h"
#include "engine/d2anime/sel_mes_win_task.h"
#include "engine/task.h"

#include <string>
#include <string_view>

#include <rex/types.h>

namespace bd::engine {

// Both popups read these out of their parent's AnimeVarBag, so a screen that
// raises one declares this block in its CSV. Past the text they are the
// popups' whole appearance: the engine's defaults leave them unsized.
struct SysMesVars {
  // SelMesWinConfig_LoadStrings("RBDEL").
  StringV sq1{"RBDEL_SQ1", ""};
  StringV sq2{"RBDEL_SQ2", ""};
  StringV sq3{"RBDEL_SQ3", ""};
  StringV sa1{"RBDEL_SA1", ""};
  StringV sa2{"RBDEL_SA2", ""};
  FloatV fs{"RBDEL_FS", 32.0};
  FloatV ln{"RBDEL_LN", 2.0};
  AnimePos ps{"RBDEL_PS", -1, 260, 870, 305, -100, true};
  AnimePos ofs{"RBDEL_OFS", 230, 185, 0, 0};
  ColorV wcl{"RBDEL_WCL", 0, 0, 0, 192};       // window
  ColorV ecl{"RBDEL_ECL", 192, 192, 192, 255}; // edge
  ColorV fcl{"RBDEL_FCL", 240, 240, 240, 255}; // frame

  // NormMesWinConfig_LoadStrings("RBNOT").
  StringV s1{"RBNOT_S1", ""};
  StringV s2{"RBNOT_S2", ""};
  StringV s3{"RBNOT_S3", ""};
  FloatV noticeFs{"RBNOT_FS", 32.0};
  FloatV noticeLn{"RBNOT_LN", 2.0};
  AnimePos noticePs{"RBNOT_PS", -1, 300, 870, 150, -100, true};
  ColorV noticeWcl{"RBNOT_WCL", 0, 0, 0, 192};
  ColorV noticeEcl{"RBNOT_ECL", 192, 192, 192, 255};
  ColorV noticeFcl{"RBNOT_FCL", 240, 240, 240, 255};

  void Emit(CsvBuilder &b);
};

// Wraps the engine's SelMesWinTask, the yes/no confirmation popup. The engine
// handles input.
class SysMesConfirm {
public:
  // Spawn a yes/no popup as a child of parent. Up to 3 UTF-8 question lines.
  // a1/a2 are answer labels, null for the catalog's own yes/no.
  bool Create(const Task &parent, const char *q1, const char *q2 = "",
              const char *q3 = "", const char *a1 = nullptr,
              const char *a2 = nullptr, int defaultSel = 1);

  // True once the user has made a choice (confirm or cancel).
  bool Poll() const;

  bool Confirmed() const;
  bool Canceled() const;
  int SelectedAnswer() const;

  void Kill();

  // Forget the handle without touching engine memory: when the popup's parent
  // task dies, the engine frees the child too, and a later Kill() would write
  // DEAD flags into a freed, possibly reused, engine heap block.
  void Drop() { task_.Reset(); }

private:
  SelMesWinTask task_;
};

// The engine's NormMesWinTask: the same window with no answers and no input,
// so the owner decides when it goes away.
class SysMesNotice {
public:
  // The engine copies the strings in at create, so changed text is a new
  // window. Text it already shows costs nothing.
  bool Show(const Task &parent, std::string_view line1,
            std::string_view line2 = {}, std::string_view line3 = {});

  void Kill();

  // See SysMesConfirm::Drop.
  void Drop();

private:
  Task task_;
  std::string shown1_, shown2_, shown3_;
};

} // namespace bd::engine
