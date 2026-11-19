// ui_m5.cpp  (ESP32 + M5Unified。パソコンのテストには入れない)
#include "ui_m5.h"
#include <M5Unified.h>
#include <string.h>
#include <stdio.h>
#include "symbols.h"

#define LINE_H     16
#define KEY_Y      (240 - 2 * LINE_H)   // ボタン表示の枠 (2行: 上=短押し、下=長押し)
#define KEY_W      92                   // 枠の幅
#define KEY_BORDER 0x055F               // 枠の色 (水色)
#define ICON_SLOT  18                   // ヘッダのアイコン1つの幅 (絵16px + すき間2px)
#define TITLE_W    (320 - UI_ICON_SLOTS * ICON_SLOT)   // 画面名の領域の幅

// アイコンの色
#define C_WHITE  0xFFFF
#define C_YELLOW 0xFFE0
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GREY   0x8410
#define C_DARK   0x4208
#define C_CYAN   0x07FF
#define C_ORANGE 0xFD20
#define BAR_X      104        // バーの左端 (ラベルは12桁=96pxまで)
#define BAR_W      208
#define BAR_H      12
#define BAR_TRACK  0x2104     // バーの背景 (暗い灰)

static UiView s_prev;
static bool   s_prevValid = false;

static uint16_t fg_of(uint8_t c) {
  switch (c) {
    case UI_COL_DIM:    return 0x8410;   // 灰
    case UI_COL_GOOD:   return 0x07E0;   // 緑
    case UI_COL_WARN:   return 0xFFE0;   // 黄
    case UI_COL_BAD:    return 0xF800;   // 赤
    case UI_COL_HEAD:   return 0xFFFF;
    case UI_COL_HILITE: return 0x07FF;   // シアン
    case UI_COL_SAKURA: return 0xEDD9;   // 撫子色 (#EEBBCB)
    default:            return 0xFFFF;   // 白
  }
}

// 先頭行(ヘッダ)だけ背景色をつける。送信中 (UI_COL_BAD) は赤、それ以外は濃い青
static uint16_t bg_of(int row, uint8_t c) {
  if (row != 0) return 0x0000;
  return (c == UI_COL_BAD) ? 0xA000 : 0x0010;
}

void ui_m5_begin() {
  M5.Display.setRotation(1);                       // 横向き (M5Stack Basic の標準)
  M5.Display.setBrightness(120);                  // 設定を読んだあと ui_m5_set_backlight で合わせる
  M5.Display.setFont(&fonts::lgfxJapanGothic_16);  // 日本語 (半角8px、全角16px)
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
  M5.Display.fillScreen(0x0000);
  s_prevValid = false;
}

void ui_m5_invalidate() { s_prevValid = false; }

void ui_m5_set_backlight(uint8_t value) {
  static int last = -1;
  if (value == 0) value = 1;
  if (last == value) return;
  last = value;
  M5.Display.setBrightness(value);
}

static void draw_bar(const UiBar* b, const UiBar* old) {
  int y = b->row * LINE_H + 2;
  int w = b->pct * BAR_W / 100;
  uint16_t col = fg_of(b->color);
  if (old && old->row == b->row && old->color == b->color) {          // 色が同じなら、変わった部分だけ
    int ow = old->pct * BAR_W / 100;
    if (w > ow)      M5.Display.fillRect(BAR_X + ow, y, w - ow, BAR_H, col);
    else if (w < ow) M5.Display.fillRect(BAR_X + w, y, ow - w, BAR_H, BAR_TRACK);
    return;
  }
  if (w > 0)     M5.Display.fillRect(BAR_X, y, w, BAR_H, col);
  if (w < BAR_W) M5.Display.fillRect(BAR_X + w, y, BAR_W - w, BAR_H, BAR_TRACK);
}

// ---- ヘッダのアイコン (どれも16x16のマスに描く) ----
static void slash(int x, uint16_t c) {                 // 赤い斜線 (受信なし・オフ・エラー)
  M5.Display.drawLine(x + 1, 15, x + 14, 0, c);
  M5.Display.drawLine(x + 2, 15, x + 15, 0, c);
}
static void ic_bolt(int x, uint16_t c) {                // 稲妻
  M5.Display.fillTriangle(x + 10, 0, x + 3, 9, x + 9, 9, c);
  M5.Display.fillTriangle(x + 7, 6, x + 13, 6, x + 5, 15, c);
}
static void ic_batt(int x, int8_t lv, bool chg) {
  uint16_t c = lv < 0 ? C_GREY : lv <= 1 ? C_RED : lv == 2 ? C_YELLOW : C_GREEN;
  M5.Display.drawRect(x, 4, 14, 8, lv < 0 ? C_GREY : C_WHITE);
  M5.Display.fillRect(x + 14, 6, 2, 4, lv < 0 ? C_GREY : C_WHITE);
  for (int i = 0; i < 4 && i < lv; i++) M5.Display.fillRect(x + 2 + i * 3, 6, 2, 4, c);
  if (chg) {                                             // 充電中: 小さい稲妻を重ねる
    M5.Display.fillTriangle(x + 8, 2, x + 4, 8, x + 8, 8, C_YELLOW);
    M5.Display.fillTriangle(x + 6, 7, x + 10, 7, x + 6, 13, C_YELLOW);
  }
}
static void ic_sd(int x, bool ok) {
  uint16_t c = ok ? C_GREEN : C_RED;
  M5.Display.drawLine(x + 3, 1, x + 9, 1, c);
  M5.Display.drawLine(x + 9, 1, x + 12, 4, c);
  M5.Display.drawLine(x + 12, 4, x + 12, 14, c);
  M5.Display.drawLine(x + 12, 14, x + 3, 14, c);
  M5.Display.drawLine(x + 3, 14, x + 3, 1, c);
  for (int i = 0; i < 3; i++) M5.Display.drawFastVLine(x + 5 + i * 2, 3, 3, c);
  if (!ok) slash(x, C_RED);
}
static void ic_vol(int x, uint8_t arcs) {               // 上向きのスピーカーと「)」の弧
  int cx = x + 7;
  M5.Display.fillRect(cx - 2, 13, 5, 3, C_WHITE);
  M5.Display.fillTriangle(cx - 5, 11, cx + 5, 11, cx - 2, 13, C_WHITE);
  M5.Display.fillTriangle(cx + 5, 11, cx - 2, 13, cx + 2, 13, C_WHITE);
  if (arcs == 0) {                                       // ミュート: 赤い×
    M5.Display.drawLine(cx - 3, 2, cx + 3, 8, C_RED); M5.Display.drawLine(cx - 2, 2, cx + 4, 8, C_RED);
    M5.Display.drawLine(cx - 3, 8, cx + 3, 2, C_RED); M5.Display.drawLine(cx - 2, 8, cx + 4, 2, C_RED);
    return;
  }
  static const int r[4] = { 3, 6, 9, 12 };
  for (int i = 0; i < 4; i++) M5.Display.drawArc(cx, 13, r[i], r[i], 235.0f, 305.0f, i < arcs ? C_WHITE : C_DARK);
}
static void ic_bell(int x, uint8_t lv) {               // 受信音: 上半分に白い鈴、下半分に音量 (外側ほど高い弧 = 蝶ネクタイ型)
  static const uint8_t L[7] = { 6, 4, 3, 2, 2, 1, 0 };  // 0〜6行目の左端 (右端は 14 - 左端)
  for (int r = 0; r < 7; r++) M5.Display.drawFastHLine(x + L[r], r, 15 - 2 * L[r], C_WHITE);
  int cx = x + 7;
  if (lv == 0) {                                         // 鳴らさない: 下半分に赤い× (モニター音のミュートと同じ描き方)
    M5.Display.drawLine(cx - 3, 9, cx + 3, 15, C_RED); M5.Display.drawLine(cx - 2, 9, cx + 4, 15, C_RED);
    M5.Display.drawLine(cx - 3, 15, cx + 3, 9, C_RED); M5.Display.drawLine(cx - 2, 15, cx + 4, 9, C_RED);
    return;
  }
  M5.Display.drawFastVLine(cx, 11, 2, C_WHITE);          // 1: 中央の短い線
  static const int8_t D[3] = { 3, 5, 7 }, T[3] = { 10, 9, 8 }, B[3] = { 13, 14, 15 };
  for (int k = 0; k < 3; k++) {                          // 2〜4: 両側の弧。まだ鳴っていない弧は暗い灰色
    uint16_t c = (k + 2 <= lv) ? C_WHITE : C_DARK;
    M5.Display.drawFastVLine(cx - D[k], T[k] + 1, B[k] - T[k] - 1, c);
    M5.Display.drawPixel(cx - D[k] + 1, T[k], c); M5.Display.drawPixel(cx - D[k] + 1, B[k], c);
    M5.Display.drawFastVLine(cx + D[k], T[k] + 1, B[k] - T[k] - 1, c);
    M5.Display.drawPixel(cx + D[k] - 1, T[k], c); M5.Display.drawPixel(cx + D[k] - 1, B[k], c);
  }
}
static void ic_sat(int x, uint16_t c) {                 // 人工衛星
  M5.Display.fillRect(x + 6, 4, 4, 5, c);
  M5.Display.fillRect(x + 0, 5, 5, 3, c);
  M5.Display.fillRect(x + 11, 5, 5, 3, c);
  M5.Display.drawFastVLine(x + 7, 9, 3, c);
  M5.Display.drawFastHLine(x + 4, 12, 8, c);
  M5.Display.drawLine(x + 4, 12, x + 7, 15, c);
  M5.Display.drawLine(x + 11, 12, x + 8, 15, c);
}
static void ic_pos(int x, uint8_t p, uint16_t bg) {
  switch (p) {
    case UI_POSICON_GPS_FIX:   ic_sat(x, C_GREEN); break;
    case UI_POSICON_GPS_STALE: ic_sat(x, C_YELLOW); break;
    case UI_POSICON_GPS_WAIT:  ic_sat(x, C_GREY); break;
    case UI_POSICON_GPS_NONE:  ic_sat(x, C_GREY); slash(x, C_RED); break;
    case UI_POSICON_LAST:                                // 最後の位置: 四角に L
      M5.Display.drawRect(x + 2, 1, 12, 14, C_ORANGE);
      M5.Display.fillRect(x + 5, 4, 2, 8, C_ORANGE);
      M5.Display.fillRect(x + 5, 10, 6, 2, C_ORANGE);
      break;
    default: {                                           // 固定位置: ピン
      M5.Display.fillCircle(x + 8, 5, 5, C_ORANGE);
      M5.Display.fillTriangle(x + 4, 8, x + 12, 8, x + 8, 15, C_ORANGE);
      M5.Display.fillCircle(x + 8, 5, 2, bg);
      break; }
  }
}
static void ic_auto(int x, uint8_t a) {
  if (a == UI_AUTOICON_ACK) {                            // Ackのみ: 文字「Ac」(半角2文字 = 16px)
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextPadding(0);
    M5.Display.setTextColor(C_CYAN);
    M5.Display.drawString("Ac", x, 0);
    return;
  }
  uint16_t c = (a == UI_AUTOICON_ON) ? C_CYAN : C_GREY;  // 砂時計
  M5.Display.drawFastHLine(x + 3, 1, 10, c);
  M5.Display.drawFastHLine(x + 3, 14, 10, c);
  M5.Display.drawLine(x + 4, 2, x + 7, 7, c);  M5.Display.drawLine(x + 11, 2, x + 8, 7, c);
  M5.Display.drawLine(x + 7, 8, x + 4, 13, c); M5.Display.drawLine(x + 8, 8, x + 11, 13, c);
  M5.Display.fillTriangle(x + 5, 3, x + 10, 3, x + 7, 6, c);
  M5.Display.fillTriangle(x + 7, 10, x + 5, 13, x + 10, 13, c);
  M5.Display.drawFastVLine(x + 7, 7, 3, c);
  if (a == UI_AUTOICON_OFF) slash(x, C_RED);
}
static void ic_mail(int x) {
  M5.Display.drawRect(x + 1, 3, 14, 10, C_WHITE);
  M5.Display.drawLine(x + 1, 3, x + 8, 9, C_WHITE);
  M5.Display.drawLine(x + 8, 9, x + 14, 3, C_WHITE);
}

// 右から 電池・SD・音量・受信音・GPS/位置・自動送信・送受信・メッセージ。位置は固定 (無いものは空ける)
static void draw_icons(const UiIcons* ic, uint16_t bg) {
  M5.Display.fillRect(TITLE_W, 0, 320 - TITLE_W, LINE_H, bg);
  int x[UI_ICON_SLOTS];
  for (int k = 0; k < UI_ICON_SLOTS; k++) x[k] = 320 - ICON_SLOT * (k + 1) + 1;
  ic_batt(x[0], ic->batt, ic->charging);
  ic_sd(x[1], ic->sdOk);
  ic_vol(x[2], ic->vol);
  ic_bell(x[3], ic->bell);
  ic_pos(x[4], ic->pos, bg);
  ic_auto(x[5], ic->autoTx);
  if (ic->act != UI_ACTICON_NONE) ic_bolt(x[6], ic->act == UI_ACTICON_TX ? C_YELLOW : C_GREEN);
  if (ic->unread) ic_mail(x[7]);
}

// ---- ボタン表示 ----
static const int KEY_X[3] = { 65, 160, 255 };          // A, B, C の中心
static void draw_key_text(int k, const char* text, bool isLong) {
  char buf[UI_SOFTKEY_MAX + 8];
  bool none = !text[0];
  if (isLong) snprintf(buf, sizeof(buf), "%s%s", K("長:"), none ? "-" : text);
  else snprintf(buf, sizeof(buf), "%s", none ? "-" : text);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextPadding(KEY_W - 4);
  M5.Display.setTextColor(none ? C_GREY : (isLong ? C_YELLOW : C_WHITE), 0x0000);
  M5.Display.drawString(buf, KEY_X[k], KEY_Y + (isLong ? LINE_H : 0));
}
static void draw_key_borders() {
  for (int k = 0; k < 3; k++) M5.Display.drawRoundRect(KEY_X[k] - KEY_W / 2, KEY_Y, KEY_W, 2 * LINE_H, 4, KEY_BORDER);
}

// ---- シンボルのアイコン (16x16。scale=2 で 32x32) ----
static uint16_t sym_color(uint8_t c) {
  switch (c) {
    case SYM_COL_CYAN:   return C_CYAN;
    case SYM_COL_YELLOW: return C_YELLOW;
    case SYM_COL_GREEN:  return C_GREEN;
    default:             return C_WHITE;
  }
}
static void draw_symbol_icon(int x, int y, int icon, int scale) {
  const uint8_t* b = symbol_icon_bitmap(icon);
  if (!b) return;
  uint16_t col = sym_color(symbol_icon_color(icon));
  M5.Display.fillRect(x, y, 16 * scale, 16 * scale, 0x0000);
  for (int r = 0; r < 16; r++) {
    uint16_t bits = (uint16_t)((b[r * 2] << 8) | b[r * 2 + 1]);
    for (int c = 0; c < 16; ) {                      // 横に続くドットはまとめて描く
      if (!(bits & (0x8000 >> c))) { c++; continue; }
      int c0 = c; while (c < 16 && (bits & (0x8000 >> c))) c++;
      M5.Display.fillRect(x + c0 * scale, y + r * scale, (c - c0) * scale, scale, col);
    }
  }
}
#define BIGICON_X (320 - 32 - 4)

// ---- レーダー ----
#define RADAR_TOP     LINE_H                  // 描く範囲 (ヘッダとボタンの間)
#define RADAR_BOTTOM  KEY_Y
#define RADAR_PLOT_W  200                     // 左の円の領域の幅 (右は文字)
#define RADAR_INFO_X  206
#define RADAR_GRID    0x4208                  // 円の色 (濃い灰)
#define RADAR_AXIS    0x2104
static void draw_radar(const UiRadar* r) {
  M5.Display.fillRect(0, RADAR_TOP, 320, RADAR_BOTTOM - RADAR_TOP, 0x0000);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextPadding(0);
  if (r->hasOwn) {
    M5.Display.drawFastHLine(r->cx - r->r, r->cy, 2 * r->r + 1, RADAR_AXIS);
    M5.Display.drawFastVLine(r->cx, r->cy - r->r, 2 * r->r + 1, RADAR_AXIS);
    M5.Display.drawCircle(r->cx, r->cy, r->r, RADAR_GRID);
    M5.Display.drawCircle(r->cx, r->cy, r->r / 2, RADAR_GRID);
    M5.Display.setFont(&fonts::Font0);                       // 小さい英数字 (6x8)
    M5.Display.setTextColor(C_GREY);
    M5.Display.drawString("N", r->cx + 3, r->cy - r->r + 2);
    M5.Display.drawString(r->ringLabel[0], r->cx + r->r / 2 + 2, r->cy + 2);
    M5.Display.drawString(r->ringLabel[1], r->cx + r->r - 6 * (int)strlen(r->ringLabel[1]) - 1, r->cy + 2);
    for (int i = 0; i < r->count; i++) {                     // 古い順に描く (新しい局が上)
      const UiRadarPt* p = &r->pts[i];
      int x = p->x, y = p->y;
      if (y < RADAR_TOP + 8) y = RADAR_TOP + 8;
      if (y > RADAR_BOTTOM - 9) y = RADAR_BOTTOM - 9;
      if (p->icon) draw_symbol_icon(x - 8, y - 8, p->icon, 1);
      else M5.Display.fillRect(x - 2, y - 2, 5, 5, C_WHITE);
      int w = 6 * (int)strlen(p->label);
      int lx = x + 9;
      if (lx + w > RADAR_PLOT_W) lx = x - 9 - w;             // 右にはみ出すときは左に出す
      if (lx < 0) lx = 0;
      M5.Display.setTextColor(fg_of(p->color));
      M5.Display.drawString(p->label, lx, y - 4);
    }
    M5.Display.drawFastHLine(r->cx - 3, r->cy, 7, C_RED);   // 自局
    M5.Display.drawFastVLine(r->cx, r->cy - 3, 7, C_RED);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
  }
  // 右側の文字。英字は文字ごとに幅が違うので、欄の外 (画面の外) に出ないよう描く範囲を区切る
  M5.Display.setClipRect(RADAR_INFO_X, RADAR_TOP, 320 - RADAR_INFO_X, RADAR_BOTTOM - RADAR_TOP);
  for (int i = 0; i < UI_RADAR_INFO && r->info[i][0]; i++) {
    M5.Display.setTextColor(fg_of(r->infoColor[i]), 0x0000);
    M5.Display.drawString(r->info[i], RADAR_INFO_X, RADAR_TOP + 4 + i * 18);
  }
  M5.Display.clearClipRect();
}

static bool bar_same(const UiBar* a, const UiBar* b) { return a->row == b->row && a->pct == b->pct && a->color == b->color; }

void ui_m5_draw(const UiView* v, bool barsOnly) {
  bool full = !s_prevValid || s_prev.screen != v->screen;
  M5.Display.startWrite();
  bool rowRedrawn[UI_MAX_LINES];
  memset(rowRedrawn, 0, sizeof(rowRedrawn));
  if (full) M5.Display.fillScreen(0x0000);

  if (full || !barsOnly) {
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextPadding(320);                  // 文字の背景を行の幅いっぱいまで塗る (前の文字を消す)
    int rows = v->count > s_prev.count ? v->count : s_prev.count;
    if (full) rows = v->count;
    // 前に32x32のアイコンがあった行の次の行は、文字が同じでも描き直す (アイコンの下半分を消す)
    bool force[UI_MAX_LINES + 1];
    memset(force, 0, sizeof(force));
    if (!full) for (int i = 0; i + 1 < UI_MAX_LINES && i < s_prev.count; i++) {
      bool nowBig = i < v->count && v->lines[i].iconBig && v->lines[i].icon;
      if (s_prev.lines[i].iconBig && s_prev.lines[i].icon && !(nowBig && v->lines[i].icon == s_prev.lines[i].icon)) { force[i] = true; force[i + 1] = true; }
    }
    if (!full && rows < UI_MAX_LINES) for (int i = rows; i < UI_MAX_LINES; i++) if (force[i]) rows = i + 1;
    for (int i = 0; i < rows && i < UI_MAX_LINES; i++) {
      static const UiLine empty = { "", UI_COL_NORMAL, 0, 0, 0 };
      const UiLine* nl = (i < v->count) ? &v->lines[i] : &empty;
      const UiLine* ol = (!full && i < s_prev.count) ? &s_prev.lines[i] : NULL;
      if (ol && !force[i] && ui_line_equal(nl, ol)) continue;
      M5.Display.setTextColor(i == 0 ? 0xFFFF : fg_of(nl->color), bg_of(i, nl->color));   // ヘッダの文字はいつも白 (送信中は背景だけ赤)
      M5.Display.setTextPadding(i == 0 ? TITLE_W : 320);   // ヘッダは画面名の領域だけ (右側はアイコン)
      M5.Display.drawString(nl->text, 0, i * LINE_H);
      rowRedrawn[i] = true;                          // 行を塗り直すとバーも消えるので、バーも描き直す
    }
    // レーダー: 中身が変わったときだけ描き直す
    if (v->radar.on && (full || !s_prev.radar.on || memcmp(&s_prev.radar, &v->radar, sizeof(v->radar)) != 0)) draw_radar(&v->radar);
    // シンボルのアイコン: その行 (32x32 は次の行も) を描き直したときに描く
    for (int i = 1; i < v->count && i < UI_MAX_LINES; i++) {
      const UiLine* l = &v->lines[i];
      if (!l->icon) continue;
      bool big = l->iconBig && i + 1 < UI_MAX_LINES;
      if (!(full || rowRedrawn[i] || (big && rowRedrawn[i + 1]))) continue;
      if (big) draw_symbol_icon(BIGICON_X, i * LINE_H, l->icon, 2);
      else draw_symbol_icon(l->iconCol * 8, i * LINE_H, l->icon, 1);
    }
  }

  // 横棒グラフ
  for (int k = 0; k < v->barCount; k++) {
    const UiBar* b = &v->bars[k];
    const UiBar* ob = NULL;
    if (!full && !(b->row >= 0 && b->row < UI_MAX_LINES && rowRedrawn[b->row]))
      for (int j = 0; j < s_prev.barCount; j++) if (s_prev.bars[j].row == b->row) ob = &s_prev.bars[j];
    if (ob && bar_same(b, ob)) continue;
    draw_bar(b, ob);
  }

  if (full || !barsOnly) {
    // ヘッダのアイコン: ヘッダを描き直したとき、またはアイコンが変わったとき
    uint8_t hc = v->count > 0 ? v->lines[0].color : (uint8_t)UI_COL_HEAD;
    if (full || rowRedrawn[0] || !ui_icons_equal(&s_prev.icons, &v->icons)) draw_icons(&v->icons, bg_of(0, hc));

    // ボタンの表示 (A, B, C の真上。枠で囲み、上段=短押し 下段=長押し)
    bool any = full;
    for (int k = 0; k < 3; k++) {
      if (full || strcmp(s_prev.keyShort[k], v->keyShort[k]) != 0) { draw_key_text(k, v->keyShort[k], false); any = true; }
      if (full || strcmp(s_prev.keyLong[k], v->keyLong[k]) != 0)   { draw_key_text(k, v->keyLong[k], true);  any = true; }
    }
    if (any) draw_key_borders();                     // 文字の背景で欠けた枠を描き直す
    M5.Display.setTextPadding(0);
  }
  M5.Display.endWrite();

  if (full || !barsOnly) s_prev = *v;
  else { memcpy(s_prev.bars, v->bars, sizeof(s_prev.bars)); s_prev.barCount = v->barCount; }
  s_prevValid = true;
}

void ui_m5_draw_icons(const UiIcons* icons) {
  if (!s_prevValid || ui_icons_equal(&s_prev.icons, icons)) return;
  uint8_t hc = s_prev.count > 0 ? s_prev.lines[0].color : (uint8_t)UI_COL_HEAD;
  M5.Display.startWrite();
  draw_icons(icons, bg_of(0, hc));
  M5.Display.endWrite();
  s_prev.icons = *icons;                             // 次の ui_m5_draw は、変わっていなければ描き直さない
}
