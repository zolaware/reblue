/**
 * @file    engine/menus/local_map.cpp
 * @brief   The area map on the world map screen. RT trades the world map for
 *          the dungeon map the compass draws, LT trades back.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/i18n.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime.h"
#include "engine/game.h"
#include "engine/gimmicks.h"
#include "engine/glyph_set.h"
#include "engine/guest_texlist.h"
#include "engine/menus/local_map.h"
#include "engine/menus/local_map_layout.h"
#include "engine/menus/map_markers.h"
#include "engine/menus/mechatt_map_main_task.h"
#include "engine/mini_map_task.h"
#include "engine/script.h"
#include "engine/settings.h"
#include "engine/sfx.h"
#include "engine/task.h"
#include "gpu/gpu.h"

REX_IMPORT(__imp__AnimeVarBag_FindChildByName, VarBagFindChild, u32(u32, u32));

REX_EXTERN(__imp__MechattMap__MainTask__Update);
REX_EXTERN(__imp__MechattMap__MainTask__Draw);
REX_EXTERN(__imp__WorldMapScreen_ApplyReduceLayout);

namespace bd::engine {

namespace {

// The two states that read the pad. Every other one is a transition.
constexpr u32 kStateReduced = 1;
constexpr u32 kStateEnlarged = 4;

constexpr int kLayoutCount = 2;

// d2anime\wrmap\L_wrmap.csv 'MapPos': the parchment window the world map image
// fills, which is the frame ours has to stay inside. Its priority is the prim
// z, since AnimeElement__Draw feeds a pri column through as one.
constexpr float kFrameX = 259.0f;
constexpr float kFrameY = 98.0f;
constexpr float kFrameW = 795.0f;
constexpr float kFrameH = 520.0f;
constexpr float kMapZ = 10.0f;
constexpr float kLegendZ = 9.95f; // its own band, so the markers keep theirs
// The legend column stands at x 905, inside the parchment's right third, so
// the map is centered left of the frame to clear it. Centering costs the
// offset on the far side too, which leaves the band the map is fitted into.
constexpr float kMapCenterOffsetX = 75.0f;
constexpr float kMapAreaW = kFrameW - 2.0f * kMapCenterOffsetX;
constexpr u32 kLegendSwatchAlpha = 204;

// L_wrmap.csv hangs its legend column, compass rose, graph paper and tick marks
// off one x origin, so parking that off screen clears everything the area map
// would otherwise be drawn under. -160 is the stock value.
constexpr double kLegendHome = -160.0;
constexpr double kLegendParked = -4000.0;

constexpr const char *kPromptMount = "ui:world-map-prompts";

// The art is fitted, not the texture: a rounded-up MapScale can leave the floor
// plan covering a twentieth of it, which magnifies to mush.
constexpr float kMaxMapMagnify = 3.0f;
// Rim markers can land a texel or two outside the scanned art.
constexpr float kContentMargin = 0.02f;

constexpr float kZoomSteps[] = {1.0f, 1.5f, 2.25f, 3.5f};
constexpr int kZoomCount = static_cast<int>(std::size(kZoomSteps));
constexpr float kPanRate = 0.03f;
constexpr float kStickDeadZone = 0.25f;

// Two kinds share a legend row when they draw the same swatch, which for the
// dots is their color alone: every kind without one of its own comes out the
// same plain dot.
bool SameSwatch(GimmickKind a, GimmickKind b) {
  if (a == GimmickKind::Chest || b == GimmickKind::Chest)
    return a == b;
  return DotColor(a) == DotColor(b);
}

// Every swatch the map can draw, in the order the legend lists them. Nothing
// stands for the plain dot, the fallback the colorless kinds share.
struct LegendEntry {
  GimmickKind kind;
  const char *key;
};
constexpr LegendEntry kLegendCatalog[] = {
    {GimmickKind::Chest, "map.legend.chest"},
    {GimmickKind::Barrier, "map.legend.barrier"},
    {GimmickKind::Item, "map.legend.item"},
    {GimmickKind::Gold, "map.legend.gold"},
    {GimmickKind::Medal, "map.legend.medal"},
    {GimmickKind::Param, "map.legend.param"},
    {GimmickKind::Heal, "map.legend.heal"},
    {GimmickKind::Grass, "map.legend.grass"},
    {GimmickKind::Nothing, "map.legend.other"},
};
static_assert(static_cast<int>(std::size(kLegendCatalog)) == kLegendRows);

struct LegendRow {
  GimmickKind kind;
  std::string label;
};

// Only the swatches this map still has left to show, so a floor of gold and
// chests reads as two rows rather than nine.
std::vector<LegendRow> BuildLegend(const std::vector<Marker> &markers) {
  std::vector<LegendRow> rows;
  for (const LegendEntry &entry : kLegendCatalog) {
    const bool present =
        std::any_of(markers.begin(), markers.end(), [&](const Marker &mk) {
          return MarkerVisible(mk) && SameSwatch(mk.kind, entry.kind);
        });
    if (present)
      rows.push_back({entry.kind, i18n::Text(entry.key)});
  }
  return rows;
}

// "Gimmicks 38/48   Chests 27/32", naming only what the map has. Found over
// total, which is how a bare x/y reads.
std::string CountLine(std::string_view stem) {
  const auto &g = Gimmicks::Get();
  const Tally points = g.Points(stem);
  const Tally chests = g.Chests(stem);
  const Tally barriers = g.Barriers(stem);
  if (points.total == 0 && chests.total == 0 && barriers.total == 0)
    return {};
  if (points.IsComplete() && chests.IsComplete() && barriers.IsComplete())
    return i18n::Text("map.cleared");

  std::string out;
  const auto add = [&out](const char *key, const Tally &t) {
    if (t.total == 0)
      return;
    if (!out.empty())
      out += "   ";
    out += i18n::Fmt(key, t.Found(), t.total);
  };
  add("map.gimmicks", points);
  add("map.chests", chests);
  add("map.barriers", barriers);
  return out;
}

// The world map screen's area map view. It owns the pad while it is up: the
// screen's own update never runs, so the stock zoom and the stock exit stay off
// until LT or B hands the screen back.
class AreaMap {
public:
  static AreaMap &Get() {
    static AreaMap map;
    return map;
  }

  // True when the area map consumed this frame's input.
  bool Update(const MechattMapMainTask &screen);
  void Draw(const MechattMapMainTask &screen);

private:
  struct HiddenAnime {
    D2AnimeTask task;
    bool visible;
  };

  void Enter();
  void Leave();
  void HideVanillaAnime();
  void RestoreVanillaAnime();
  void CollectFloors();
  MiniMapDB SelectedFloor() const;
  int SelectedIndex() const;
  void ApplyScreenVars(bool areaMap);
  gpu::TextureContent FloorContent(const MiniMapDB &db, u32 texture);
  void LoadPrompts(const MechattMapMainTask &screen);
  void SyncPrompts(bool available);
  bool StepFloor(int delta);
  void Pan();

  bool active_ = false;
  MechattMapMainTask screen_;
  std::string title_;
  std::string counts_;
  std::vector<Marker> markers_;
  std::vector<LegendRow> legend_;
  int zoom_ = 0;
  int floor_ = -1; // -1 follows the floor the engine itself picked
  float panU_ = 0.5f;
  float panV_ = 0.5f;
  std::vector<MiniMapDB> floors_;
  std::vector<std::pair<MiniMapDB, gpu::TextureContent>> content_;
  std::vector<HiddenAnime> hidden_;

  // Stock values saved on entry, one per layout, since which world map shows
  // and whether the screen offers zoom are the screen's own business.
  f32 worldFlg_[kLayoutCount] = {1.0f, 1.0f};
  f32 cubeFlg_[kLayoutCount] = {-1.0f, -1.0f};
  f32 alphaLBRB_[kLayoutCount] = {255.0f, 255.0f};

  D2AnimeTask prompts_;
  bool mounted_ = false;
  u32 glyphGen_ = 0;
};

void AreaMap::CollectFloors() {
  floors_.clear();
  const ScriptManTask root = Game::Get().ScriptManTask();
  const MiniMapTask miniMap = root.MiniMap();
  if (!miniMap.Floor())
    return;

  // Nothing calls LoadAreaFloors for the world map, so a dungeon's floors
  // outlive it. The identity triple names the stage they were loaded for.
  const engine::Script script = root.Script();
  if (!script || miniMap.Category() != script.Category() ||
      miniMap.AreaHi() != script.Area() || miniMap.AreaLo() != script.Sub())
    return;

  // MiniMapTask_SelectCurrentFloorDB walks the sub-floor vector and falls back
  // to the inline base map, so the traversal order is the base map first.
  const MiniMapDB base = miniMap.BaseFloor();
  if (base.Ready())
    floors_.push_back(base);

  for (const MiniMapDB &db : miniMap.Floors())
    if (db.Ready())
      floors_.push_back(db);
}

// One scan per floor per opening, so a floor measured before its texels landed
// is measured again the next time.
gpu::TextureContent AreaMap::FloorContent(const MiniMapDB &db, u32 texture) {
  for (const auto &entry : content_)
    if (entry.first.Address() == db.Address())
      return entry.second;

  gpu::TextureContent rect = gpu::TextureContent::Scan(texture);
  rect.u0 = std::max(0.0f, rect.u0 - kContentMargin);
  rect.v0 = std::max(0.0f, rect.v0 - kContentMargin);
  rect.u1 = std::min(1.0f, rect.u1 + kContentMargin);
  rect.v1 = std::min(1.0f, rect.v1 + kContentMargin);
  content_.push_back({db, rect});
  return rect;
}

MiniMapDB AreaMap::SelectedFloor() const {
  if (floors_.empty())
    return {};
  if (floor_ >= 0 && floor_ < static_cast<int>(floors_.size()))
    return floors_[floor_];

  const MiniMapDB live = Game::Get().ScriptManTask().MiniMap().Floor();
  for (const MiniMapDB &db : floors_)
    if (db.Address() == live.Address())
      return db;
  return floors_.front();
}

// Where the shown floor sits in the list, which is both what the prompt counts
// off and what a step moves from.
int AreaMap::SelectedIndex() const {
  if (floor_ >= 0 && floor_ < static_cast<int>(floors_.size()))
    return floor_;

  const MiniMapDB live = SelectedFloor();
  int index = 0;
  for (size_t i = 0; i < floors_.size(); ++i)
    if (floors_[i].Address() == live.Address())
      index = static_cast<int>(i);
  return index;
}

bool AreaMap::StepFloor(int delta) {
  if (floors_.size() < 2)
    return false;

  const int count = static_cast<int>(floors_.size());
  floor_ = (SelectedIndex() + delta + count) % count;
  panU_ = panV_ = 0.5f;
  sfx::Play(sfx::kToggle);
  return true;
}

void AreaMap::ApplyScreenVars(bool areaMap) {
  // The stock header names the world map, and the stock footer's zoom prompt
  // belongs to the world map's own LB/RB. Both are children of a layout.
  rex::ppc::stack_guard guard;
  const u32 headerName = rex::ppc::stack_push_string("wrmap_hdr");
  const u32 footerName = rex::ppc::stack_push_string("wrmap_ftr");

  const D2AnimeTask layouts[kLayoutCount] = {screen_.Fade(), screen_.Layout()};
  for (int i = 0; i < kLayoutCount; ++i) {
    const D2AnimeTask &layout = layouts[i];
    if (!layout)
      continue;
    AnimeData bag = layout.AnimeData();

    if (areaMap) {
      if (const auto v = bag.Float("WorldFlg"))
        worldFlg_[i] = *v;
      if (const auto v = bag.Float("CubeFlg"))
        cubeFlg_[i] = *v;
      bag.SetFloat("WorldFlg", -1.0);
      bag.SetFloat("CubeFlg", -1.0);
    } else {
      bag.SetFloat("WorldFlg", worldFlg_[i]);
      bag.SetFloat("CubeFlg", cubeFlg_[i]);
    }
    bag.SetFloat("posx", areaMap ? kLegendParked : kLegendHome);

    AnimeData header(VarBagFindChild(bag.Address(), headerName));
    if (header)
      header.SetFloat("alpha", areaMap ? 0.0 : 255.0);
    // Cube world has no zoom and its prompt is already dark, so a fixed 255
    // would light one for a zoom the screen refuses to run.
    AnimeData footer(VarBagFindChild(bag.Address(), footerName));
    if (footer) {
      if (areaMap) {
        if (const auto v = footer.Float("alpha_LBRB"))
          alphaLBRB_[i] = *v;
      }
      footer.SetFloat("alpha_LBRB", areaMap ? 0.0 : alphaLBRB_[i]);
    }
  }
}

void AreaMap::HideVanillaAnime() {
  hidden_.clear();
  const u32 layout = screen_.Layout().Address();
  const u32 vtable = Task(layout).Vtable();
  if (!vtable)
    return;

  const u32 fade = screen_.Fade().Address();
  const u32 keep = prompts_.Address();
  for (Task child : screen_.Children()) {
    const u32 addr = child.Address();
    if (addr == layout || addr == fade || addr == keep)
      continue;
    if (child.Vtable() != vtable)
      continue;
    D2AnimeTask anime(addr);
    hidden_.push_back({anime, anime.IsVisible()});
  }
  for (HiddenAnime &anime : hidden_)
    anime.task.SetVisible(false);
}

void AreaMap::RestoreVanillaAnime() {
  for (HiddenAnime &anime : hidden_) {
    if (anime.task)
      anime.task.SetVisible(anime.visible);
  }
  hidden_.clear();
}

void AreaMap::Enter() {
  active_ = true;
  content_.clear();
  zoom_ = 0;
  floor_ = -1;
  panU_ = panV_ = 0.5f;

  // MiniMapTask_LoadAreaFloors loads only the stage the player is in, so
  // the map on screen is always this stage's, and nothing on it changes while
  // it is up.
  const engine::Script script = Game::Get().ScriptManTask().Script();
  const std::string stem = script.Name();
  title_ = script.DisplayName();
  if (title_.empty())
    title_ = i18n::Text("map.area");
  counts_ = CountLine(stem);
  markers_ = Gimmicks::Get().Markers(stem);
  legend_ = Settings::Get().MapGimmickMarkers() ? BuildLegend(markers_)
                                                : std::vector<LegendRow>();

  sfx::Play(sfx::kOpen);
  ApplyScreenVars(true);
  HideVanillaAnime();
}

void AreaMap::Leave() {
  if (!active_)
    return;
  active_ = false;
  RestoreVanillaAnime();
  ApplyScreenVars(false);
}

void AreaMap::LoadPrompts(const MechattMapMainTask &screen) {
  // Before the CSV is generated: the prompt labels come from the catalog.
  i18n::SyncLocale();

  if (!mounted_) {
    LayoutMount mount;
    mount.Add(kLocalMapPromptCSV, BuildLocalMapPromptCSV);
    mount.Publish(kPromptMount);
    mounted_ = true;
  }

  prompts_ = D2AnimeTask::Load(screen, kLocalMapPromptCSV);
}

void AreaMap::SyncPrompts(bool available) {
  if (!prompts_)
    return;

  // The caps live in this layout's own vars, so a rebind or a device switch
  // reaches here rather than in the uv. bag the parse snapshotted.
  if (prompts_.IsReady()) {
    const u32 gen = Glyphs::Get().Generation();
    if (gen != glyphGen_) {
      glyphGen_ = gen;
      for (const PromptGlyph &g : kPromptGlyphs) {
        const UVRect r = Glyphs::Get().PromptUV(g);
        prompts_.SetFloat(g.uv.u0, r.u0);
        prompts_.SetFloat(g.uv.v0, r.v0);
        prompts_.SetFloat(g.uv.u1, r.u1);
        prompts_.SetFloat(g.uv.v1, r.v1);
      }
    }
  }

  prompts_.SetFloat(kPromptOpenFlg, !active_ && available ? 1.0 : -1.0);
  prompts_.SetFloat(kPromptAreaFlg, active_ ? 1.0 : -1.0);

  for (int row = 0; row < kLegendRows; ++row) {
    const bool shown = active_ && row < static_cast<int>(legend_.size());
    prompts_.SetFloat(kPromptLegendFlg[row], shown ? 1.0 : -1.0);
    if (shown)
      prompts_.SetText(kPromptLegendLabel[row], legend_[row].label);
  }

  if (!active_)
    return;

  prompts_.SetText(kPromptOpenLabel, i18n::Text("map.area"));
  prompts_.SetText(kPromptBackLabel, i18n::Text("map.world"));
  prompts_.SetText(kPromptZoomLabel, i18n::Text("map.zoom"));

  prompts_.SetText(kPromptTitleLabel, title_);
  prompts_.SetText(kPromptCountLabel, counts_);

  // No sub-floor minimap ships, so this is always a single map. The pair stays
  // wired anyway.
  const bool steppable = floors_.size() > 1;
  prompts_.SetFloat(kPromptFloorFlg, steppable ? 1.0 : -1.0);
  if (!steppable)
    return;

  prompts_.SetText(kPromptFloorLabel,
                   i18n::Fmt("map.floor", SelectedIndex() + 1, floors_.size()));
}

void AreaMap::Pan() {
  const float scale = kPanRate / kZoomSteps[zoom_];
  const float x = StickValue(StickAxis::RightX);
  const float y = StickValue(StickAxis::RightY);
  if (std::fabs(x) > kStickDeadZone)
    panU_ += x * scale;
  // The stick reads positive upward and the texture's v grows downward.
  if (std::fabs(y) > kStickDeadZone)
    panV_ -= y * scale;
  panU_ = std::clamp(panU_, 0.0f, 1.0f);
  panV_ = std::clamp(panV_, 0.0f, 1.0f);
}

bool AreaMap::Update(const MechattMapMainTask &screen) {
  // A new field scene builds a new screen, and nothing of ours survives it.
  // The heap recycles screen addresses, so the check is on identity.
  if (screen_.Rebind(screen.Address())) {
    active_ = false;
    floors_.clear();
    content_.clear();
    hidden_.clear();
    markers_.clear();
    legend_.clear();
    prompts_ = D2AnimeTask();
    glyphGen_ = 0;
    // Ahead of the state gate, so the CSV and its textures get the whole open
    // transition to settle. Loading on the first interactive frame instead
    // costs the prompt its first moments on screen.
    LoadPrompts(screen);
  }

  const u32 state = screen.State();
  const bool live = state == kStateReduced || state == kStateEnlarged;
  if (!live) {
    // The screen has left the two states that read the pad, so whatever veil
    // the area view put on it has to come off with it.
    if (active_)
      Leave();
    SyncPrompts(false);
    return false;
  }

  CollectFloors();

  if (!active_) {
    const bool available = !floors_.empty();
    if (available && CheckButton(Button::RT)) {
      Enter();
      SyncPrompts(available);
      return true;
    }
    SyncPrompts(available);
    return false;
  }

  if (floors_.empty() || CheckButton(Button::LT)) {
    const bool available = !floors_.empty();
    Leave();
    if (available)
      sfx::Play(sfx::kCancel);
    SyncPrompts(available);
    return true;
  }

  // The footer keeps the screen's own cancel prompt, so cancel has to keep
  // closing the screen. Standing down first hands the frame back intact.
  if (CheckAction(Action::Cancel)) {
    Leave();
    return false;
  }

  // The wheel is the mouse's own zoom, and the only one a default keyboard
  // layout has: the shoulder buttons carry it on a pad, and RB ships unbound.
  int zoomStep = 0;
  if (CheckButton(Button::RB))
    zoomStep = 1;
  else if (CheckButton(Button::LB))
    zoomStep = -1;
  else
    zoomStep = MenuMouse::Get().TakeWheelDetents();

  const int zoomed = std::clamp(zoom_ + zoomStep, 0, kZoomCount - 1);
  if (zoomed != zoom_) {
    zoom_ = zoomed;
    sfx::Play(sfx::kToggle);
  }

  if (CheckButton(Button::Up))
    StepFloor(-1);
  else if (CheckButton(Button::Down))
    StepFloor(1);

  Pan();
  SyncPrompts(true);
  return true;
}

void AreaMap::Draw(const MechattMapMainTask &screen) {
  if (!active_ || !screen_.Is(screen.Address()))
    return;
  const MiniMapDB db = SelectedFloor();
  if (!db.Ready())
    return;

  PrimSelectTexture(0, db.TexHolderAddress());
  const u32 floorTex = mem::load<u32>(PrimState() + kPrim_Texture);
  const gpu::TextureContent content = FloorContent(db, floorTex);

  const f32 rot = db.TexRot() * kDegToRad;
  const float cosA = std::cos(rot);
  const float sinA = std::sin(rot);
  const float absCos = std::fabs(cosA);
  const float absSin = std::fabs(sinA);

  const f32 artW = db.TexW() * content.Width();
  const f32 artH = db.TexH() * content.Height();
  const float fit =
      std::min({kMapAreaW / (artW * absCos + artH * absSin),
                kFrameH / (artW * absSin + artH * absCos), kMaxMapMagnify});
  const float centerX = kFrameX + kFrameW * 0.5f - kMapCenterOffsetX;
  const float centerY = kFrameY + kFrameH * 0.5f;

  const float zoom = kZoomSteps[zoom_];
  const bool sideways = absSin > absCos;
  const float roomW = sideways ? kFrameH : kMapAreaW;
  const float roomH = sideways ? kMapAreaW : kFrameH;
  const float drawW = std::min(artW * fit * zoom, roomW);
  const float drawH = std::min(artH * fit * zoom, roomH);
  const float halfW = drawW * 0.5f;
  const float halfH = drawH * 0.5f;
  const float halfU = drawW / (artW * fit * zoom) * 0.5f;
  const float halfV = drawH / (artH * fit * zoom) * 0.5f;
  const float panU = std::clamp(panU_, halfU, 1.0f - halfU);
  const float panV = std::clamp(panV_, halfV, 1.0f - halfV);
  const float windowU = content.u0 + panU * content.Width();
  const float windowV = content.v0 + panV * content.Height();
  const float u0 = content.u0 + (panU - halfU) * content.Width();
  const float u1 = content.u0 + (panU + halfU) * content.Width();
  const float v0 = content.v0 + (panV - halfV) * content.Height();
  const float v1 = content.v0 + (panV + halfV) * content.Height();

  // Screen space from the map's own, matching XMMatrixRotationY on the row
  // vector bdMatrixRotateAxis hands the compass.
  const auto place = [&](float x, float y, float *outX, float *outY) {
    *outX = centerX + x * cosA + y * sinA;
    *outY = centerY - x * sinA + y * cosA;
  };
  // The body draws at half alpha and ramps to nothing at the left and right
  // edges. The engine 2D path has no gradient of its own, so the ramp is three
  // quads whose vertex colors meet.
  constexpr u32 kMapBodyColor = 0x80FFFFFFu;
  constexpr u32 kMapEdgeColor = 0x00FFFFFFu;
  constexpr float kFadeRatio = 0.05f;

  const auto corner = [&](float x, float y, float u, float v, u32 color) {
    float sx = 0.0f;
    float sy = 0.0f;
    place(x, y, &sx, &sy);
    PrimPushVertex2D(sx, sy, u, v, 0, 0, 0, 0, color);
  };

  // World to texture by the map's offset and scale, then into the zoom and pan
  // window. False when it falls outside.
  const float offsetX = db.OffsetX();
  const float offsetZ = db.OffsetZ();
  const float invScaleX = 1.0f / db.ScaleX();
  const float invScaleZ = 1.0f / db.ScaleZ();
  const float spanX = halfW * 2.0f / (u1 - u0);
  const float spanY = halfH * 2.0f / (v1 - v0);
  const auto toScreen = [&](float worldX, float worldZ, float *outX,
                            float *outY) {
    const float mapU = (offsetX + worldX) * invScaleX;
    const float mapV = (offsetZ + worldZ) * invScaleZ;
    if (mapU < u0 || mapU > u1 || mapV < v0 || mapV > v1)
      return false;
    place((mapU - windowU) * spanX, (mapV - windowV) * spanY, outX, outY);
    return true;
  };

  const auto band = [&](float x0, float x1, float uLeft, float uRight,
                        u32 colorLeft, u32 colorRight) {
    PrimBegin(kTexturedQuad2D, kMapZ, 0, 0);
    PrimSetTexture(0, floorTex);
    corner(x0, -halfH, uLeft, v0, colorLeft);
    corner(x0, halfH, uLeft, v1, colorLeft);
    corner(x1, halfH, uRight, v1, colorRight);
    corner(x1, -halfH, uRight, v0, colorRight);
    PrimEnd();
  };

  const float fadeX = halfW * 2.0f * kFadeRatio;
  const float fadeU = (u1 - u0) * kFadeRatio;
  band(-halfW, -halfW + fadeX, u0, u0 + fadeU, kMapEdgeColor, kMapBodyColor);
  band(-halfW + fadeX, halfW - fadeX, u0 + fadeU, u1 - fadeU, kMapBodyColor,
       kMapBodyColor);
  band(halfW - fadeX, halfW, u1 - fadeU, u1, kMapBodyColor, kMapEdgeColor);

  const MiniMapTask miniMap = Game::Get().ScriptManTask().MiniMap();
  if (!miniMap)
    return;

  if (Settings::Get().MapGimmickMarkers()) {
    PrimSelectTexture(kChromeMarker, miniMap.ChromeTexAddress());
    const u32 shapeTex = mem::load<u32>(PrimState() + kPrim_Texture);

    // The legend swatch is a key rather than a marker, so it holds the
    // pulse's resting frame while the map's own markers flash.
    QuadWriter legend{shapeTex, kLegendZ, kMarkerZ, kLegendSwatchAlpha};
    for (int row = 0; row < static_cast<int>(legend_.size()); ++row)
      DrawMarkerShape(legend, legend_[row].kind,
                      static_cast<float>(kLegendSwatchX),
                      static_cast<float>(LegendSwatchY(row)), 0.0f,
                      kLegendMarkerHalf);

    QuadWriter quads{shapeTex};
    for (const Marker &mk : markers_) {
      if (!MarkerVisible(mk))
        continue;
      float dotX = 0.0f;
      float dotY = 0.0f;
      if (!toScreen(mk.x, mk.z, &dotX, &dotY))
        continue;
      DrawMarkerShape(quads, mk.kind, dotX, dotY, MarkerPulse(mk), kMarkerHalf);
    }
  }

  const Player leader = Game::Get().FieldPlayerEntity().Leader().Chara();
  if (!leader)
    return;
  const Vec3 player = leader.Position();

  float markerX = 0.0f;
  float markerY = 0.0f;
  if (!toScreen(player[0], player[2], &markerX, &markerY))
    return;

  // PlyRot defaults to OffSet.rot, which the map is not turned by.
  const float heading =
      db.TexRot() * kDegToRad + leader.Rotation()[1] - kHalfPi;
  PrimSelectTexture(kChromeArrow, miniMap.ChromeTexAddress());
  PrimDrawRectRotated(markerX, markerY, kPlayerZ, kMarkerSize, kMarkerSize,
                      heading, 0, 0, 0, 0, 0, 0, kOpaqueWhite);
}

// ApplyReduceLayout resets the rest of the zoom crossfade but not its alphas.
// L_wrmap draws the cube-world map on Map1Alpha alone, so a stranded 0 blanks
// it, while the overworld map, drawn on both alphas, never shows the fault.
void RestoreMapCrossfade(const MechattMapMainTask &screen) {
  const D2AnimeTask layout = screen.Layout();
  if (!layout)
    return;
  AnimeData bag = layout.AnimeData();
  bag.SetFloat("Map1Alpha", 255.0);
  bag.SetFloat("Map2Alpha", 0.0);
}

} // namespace

void AreaMapTick() {
  // Nothing loads while the markers are off, so a session that never turns them
  // on never pays for the sheets.
  if (!Settings::Get().MapGimmickMarkers())
    return;
  for (IconSheet *sheet : kIconSheets)
    sheet->list.Poll();
}

} // namespace bd::engine

// The hooks stay raw: they call engine code on the hook's own stack, which
// only ctx carries.

REX_HOOK_RAW(MechattMap__MainTask__Update) {
  const bd::engine::MechattMapMainTask screen(ctx.r3.u32);
  if (bd::engine::AreaMap::Get().Update(screen))
    return;
  __imp__MechattMap__MainTask__Update(ctx, base);
}

REX_HOOK_RAW(MechattMap__MainTask__Draw) {
  const bd::engine::MechattMapMainTask screen(ctx.r3.u32);
  __imp__MechattMap__MainTask__Draw(ctx, base);
  bd::engine::AreaMap::Get().Draw(screen);
}

REX_HOOK_RAW(WorldMapScreen_ApplyReduceLayout) {
  const bd::engine::MechattMapMainTask screen(ctx.r3.u32);
  __imp__WorldMapScreen_ApplyReduceLayout(ctx, base);
  bd::engine::RestoreMapCrossfade(screen);
}
