// ui_core.cpp
#include "ui_core.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>

// ---------------------------------------------------------------
// 文字列
// ---------------------------------------------------------------
static int cp_width(uint32_t cp) {
  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
      (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6)) return 2;
  return 1;
}

int ui_fit(char* s, int cols) {
  char tmp[UI_TEXT_MAX];
  size_t o = 0;
  int w = 0;
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    uint32_t cp; int n;
    if (*p < 0x80) { cp = *p; n = 1; }
    else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((*p & 0x1Fu) << 6) | (p[1] & 0x3Fu); n = 2; if (cp < 0x80) { cp = '?'; } }
    else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((*p & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); n = 3; if (cp < 0x800) cp = '?';
    }
    else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) { cp = '?'; n = 4; }
    else { cp = '?'; n = 1; }
    if (cp < 0x20 || cp == 0x7F) cp = '?';
    int cw = cp_width(cp);
    if (w + cw > cols) break;
    if (cp == '?' && !(n == 1 && *p == '?')) { if (o + 1 >= sizeof(tmp)) break; tmp[o++] = '?'; }
    else { if (o + n >= sizeof(tmp)) break; memcpy(tmp + o, p, n); o += n; }
    w += cw;
    p += n;
  }
  tmp[o] = 0;
  strcpy(s, tmp);
  return w;
}

const char* ui_send_refused_text(PosReason r) {
  switch (r) {
    case POS_GPS_NO_DATA:  return L("GPS受信なし: 送信しません");
    case POS_GPS_NO_FIX:   return L("未測位のため送信しません");
    case POS_LAST_NONE:    return L("保存位置がないため送信しません");
    case POS_FIXED_UNSET:  return L("固定位置が未設定です");
    default:               return L("位置がないため送信しません");
  }
}

// JA1SJS (SSID は問わない) は撫子色で出す。分かる人だけ分かればよい小さな印
static bool sakura_call(const char* call) { return call && call[0] && msg_call_base_equal(call, "JA1SJS"); }

bool ui_line_equal(const UiLine* a, const UiLine* b) {
  return a->color == b->color && a->icon == b->icon && a->iconCol == b->iconCol && a->iconBig == b->iconBig && strcmp(a->text, b->text) == 0;
}

bool ui_icons_equal(const UiIcons* a, const UiIcons* b) {
  return a->batt == b->batt && a->charging == b->charging && a->sdOk == b->sdOk && a->vol == b->vol && a->bell == b->bell &&
         a->pos == b->pos && a->autoTx == b->autoTx && a->act == b->act && a->unread == b->unread;
}

// ---------------------------------------------------------------
// 状態
// ---------------------------------------------------------------
void ui_init(UiState* ui) {
  memset(ui, 0, sizeof(*ui));
  ui->screen = UI_SCREEN_OWN;
  ui->returnScreen = UI_SCREEN_OWN;
  ui->calibMode = AFSK_TONE_MARK;
}

void ui_clear(UiState* ui, uint8_t t) {
  bool all = (t == UI_CLEAR_ALL);
  if (all || t == UI_CLEAR_RX) {
    memset(ui->rx, 0, sizeof(ui->rx)); ui->rxHead = ui->rxCount = 0; ui->rxTotal = 0;
    ui->hasLastRx = false; ui->rxDetail = false; ui->rxSel = 0;
  }
  if (all || t == UI_CLEAR_WX) { memset(ui->wx, 0, sizeof(ui->wx)); ui->wxDetail = false; ui->wxSel[0] = 0; }
  if ((all || t == UI_CLEAR_MSG) && ui->msgs) { msg_clear(ui->msgs); ui->msgDetail = false; ui->msgSel = 0; }
  if (all || t == UI_CLEAR_RADAR) memset(ui->st, 0, sizeof(ui->st));
}
static int clear_count(const UiState* ui, uint8_t t) {
  int n = 0;
  switch (t) {
    case UI_CLEAR_RX:  return ui->rxCount;
    case UI_CLEAR_WX:  for (int i = 0; i < WX_STATION_COUNT; i++) if (ui->wx[i].used) n++; return n;
    case UI_CLEAR_MSG: if (ui->msgs) for (int i = 0; i < MSG_STORE_COUNT; i++) if (ui->msgs->e[i].used) n++; return n;
    case UI_CLEAR_RADAR: for (int i = 0; i < RADAR_STATION_COUNT; i++) if (ui->st[i].used) n++; return n;
    default: for (int k = 0; k < UI_CLEAR_ALL; k++) n += clear_count(ui, (uint8_t)k); return n;
  }
}
static const char* const CLEAR_NAMES[UI_CLEAR_COUNT] = { "受信一覧", "WX局", "メッセージ", "レーダーの局", "すべて" };

void ui_set_tx_busy(UiState* ui, bool busy) { ui->txBusy = busy; }
void ui_set_msgs(UiState* ui, MsgCore* m) { ui->msgs = m; }
void ui_set_my_call(UiState* ui, const char* call) { snprintf(ui->myCall, sizeof(ui->myCall), "%s", call ? call : ""); }

// ---------------------------------------------------------------
// メッセージ: 並びと選択、作成
// ---------------------------------------------------------------
// 詳細で順にたどる並び: 先頭が「新しいメッセージを作成」(seq 0)、続いて新しい順
static int msg_list(const UiState* ui, uint32_t seqs[MSG_STORE_COUNT + 1]) {
  int n = 0; seqs[n++] = 0;
  if (ui->msgs) {
    uint8_t o[MSG_STORE_COUNT]; int k = msg_order(ui->msgs, o);
    for (int i = 0; i < k; i++) seqs[n++] = ui->msgs->e[o[i]].seq;
  }
  return n;
}
static void msg_select(UiState* ui, uint32_t seq) {
  ui->msgSel = seq;
  if (ui->msgs && seq) { int i = msg_find_seq(ui->msgs, seq); if (i >= 0) msg_mark_read(ui->msgs, i); }   // 表示したら既読
}
// 受信一覧の並び: i 番目に新しいものの添字
static int rx_idx(const UiState* ui, int i) { return (ui->rxHead + RX_LIST_COUNT - 1 - i) % RX_LIST_COUNT; }

static void cmp_open(UiState* ui, const char* to, const char* text, uint8_t step, const char* replyTo) {
  memset(&ui->cmp, 0, sizeof(ui->cmp));
  ui->cmp.active = true; ui->cmp.step = step;
  snprintf(ui->cmp.to, sizeof(ui->cmp.to), "%s", to ? to : "");
  snprintf(ui->cmp.text, sizeof(ui->cmp.text), "%s", text ? text : "");
  snprintf(ui->cmp.replyTo, sizeof(ui->cmp.replyTo), "%s", replyTo ? replyTo : "");
}
void ui_compose_done(UiState* ui, int msgIdx) {
  memset(&ui->cmp, 0, sizeof(ui->cmp));
  ui->screen = UI_SCREEN_MSG; ui->msgDetail = true;
  if (ui->msgs && msgIdx >= 0 && msgIdx < MSG_STORE_COUNT && ui->msgs->e[msgIdx].used) ui->msgSel = ui->msgs->e[msgIdx].seq;
}

static void add_item(char out[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1], int* n, const char* s, bool isCall) {
  if (!s || !s[0] || *n >= UI_CMP_ITEMS_MAX) return;
  for (int i = 0; i < *n; i++) if (isCall ? msg_call_equal(out[i], s) : !strcmp(out[i], s)) return;   // 重複は出さない
  snprintf(out[*n], APRS_MSG_TEXT_MAX + 1, "%s", s);
  (*n)++;
}

int ui_compose_items(const UiState* ui, char out[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1]) {
  int n = 0;
  const UiCompose* c = &ui->cmp;
  if (c->step == UI_CMP_TO) {
    add_item(out, &n, c->input, true);                         // キーボードで入力中の宛先 (打ったものはそのまま出す)
    // 返信の相手・登録した局・受信した局 (新しい順)。今の自局 (デジピーターで折り返した自分の送信など) は出さない
    const char* cand[1 + MSG_FRIEND_MAX + RX_LIST_COUNT];
    int nc = 0;
    cand[nc++] = c->replyTo;
    for (int i = 0; i < g_cfg.friendCount && i < MSG_FRIEND_MAX; i++) cand[nc++] = g_cfg.friends[i];   // config.txt の friend1〜8
    for (int i = 0; i < (int)ui->rxCount; i++) cand[nc++] = ui->rx[(ui->rxHead + RX_LIST_COUNT - 1 - i) % RX_LIST_COUNT].src;
    for (int i = 0; i < nc; i++) if (!(ui->myCall[0] && msg_call_equal(cand[i], ui->myCall))) add_item(out, &n, cand[i], true);
  } else if (c->step == UI_CMP_TEXT) {
    add_item(out, &n, c->input, false);                        // キーボードで入力中の本文
    for (int i = 0; i < g_cfg.msgPresetCount && i < MSG_PRESET_MAX; i++) add_item(out, &n, g_cfg.msgPresets[i], false);   // config.txt の msg1〜8
  }
  return n;
}

static UiAction cmp_choose(UiState* ui) {
  UiCompose* c = &ui->cmp;
  if (c->step == UI_CMP_CONFIRM) return (c->to[0] && c->text[0]) ? UI_ACT_MSG_SEND : UI_ACT_NONE;
  char items[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1];
  int n = ui_compose_items(ui, items);
  if (n == 0) return UI_ACT_NONE;
  if (c->sel >= n) c->sel = 0;
  if (c->step == UI_CMP_TO) { snprintf(c->to, sizeof(c->to), "%.9s", items[c->sel]); c->step = UI_CMP_TEXT; }
  else { snprintf(c->text, sizeof(c->text), "%s", items[c->sel]); c->step = UI_CMP_CONFIRM; }
  c->sel = 0; c->input[0] = 0;
  return UI_ACT_NONE;
}

static void cmp_back(UiState* ui) {
  UiCompose* c = &ui->cmp;
  c->input[0] = 0; c->sel = 0;
  if (c->step == UI_CMP_CONFIRM) c->step = UI_CMP_TEXT;
  else if (c->step == UI_CMP_TEXT) c->step = UI_CMP_TO;
  else c->active = false;
}

static UiAction cmp_short(UiState* ui, int btn) {
  UiCompose* c = &ui->cmp;
  if (btn == 1) return cmp_choose(ui);
  if (c->step == UI_CMP_CONFIRM) return UI_ACT_NONE;
  char items[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1];
  int n = ui_compose_items(ui, items);
  if (n > 0) c->sel = (c->sel + (btn == 0 ? n - 1 : 1)) % n;
  return UI_ACT_NONE;
}

UiAction ui_key(UiState* ui, char ch, uint32_t nowMs) {
  (void)nowMs;
  if (!ui->cmp.active || ui->txBusy || ui->calibOn) return UI_ACT_NONE;
  UiCompose* c = &ui->cmp;
  unsigned char k = (unsigned char)ch;
  if (k == 0x1B) { cmp_back(ui); return UI_ACT_NONE; }                      // Esc
  if (k == '\r' || k == '\n') return cmp_choose(ui);                         // Enter
  char* buf = (c->step == UI_CMP_CONFIRM) ? c->text : c->input;
  size_t len = strlen(buf);
  if (k == 0x08 || k == 0x7F) { if (len) buf[len - 1] = 0; c->sel = 0; return UI_ACT_NONE; }   // BS
  if (k < 0x20 || k > 0x7E) return UI_ACT_NONE;
  if (c->step == UI_CMP_TO) {
    k = (unsigned char)toupper(k);
    if (!isalnum(k) && k != '-') return UI_ACT_NONE;
    if (len >= 9) return UI_ACT_NONE;
  } else {
    if (k == '|' || k == '~' || k == '{') k = '?';                              // APRSのメッセージで使えない文字
    if (len >= APRS_MSG_TEXT_MAX) return UI_ACT_NONE;
  }
  buf[len] = (char)k; buf[len + 1] = 0;
  c->sel = 0;
  return UI_ACT_NONE;
}

void ui_set_calib_on(UiState* ui, bool on, uint32_t nowMs) {
  if (on && !ui->calibOn) ui->calibSinceMs = nowMs;
  ui->calibOn = on;
}

void ui_show_message(UiState* ui, const char* text, uint32_t nowMs, uint32_t durationMs) {
  snprintf(ui->msg, sizeof(ui->msg), "%s", text ? text : "");
  ui->msgUntilMs = nowMs + durationMs;
  if (ui->msgUntilMs == 0) ui->msgUntilMs = 1;
}

// メニュー項目を表示するか。固定位置の選択は、取得元=固定位置のときだけ
static bool menu_visible(int item, const SettingsMgr* s) {
  if (item == UI_MENU_FIXED) return s->cur.positionSource == POSITION_SOURCE_FIXED;
  return true;
}
static int menu_step(int item, int dir, const SettingsMgr* s) {
  for (int k = 0; k < UI_MENU_COUNT; k++) {
    item = (item + dir + UI_MENU_COUNT) % UI_MENU_COUNT;
    if (menu_visible(item, s)) return item;
  }
  return item;
}

static void menu_change(UiState* ui, SettingsMgr* s) {
  switch (ui->menuItem) {
    case UI_MENU_AUTOTX:       settings_next_auto_tx(s);
                               if (auto_tx_is_flex(s->cur.autoTx) && !app_config_flex_ok(&g_cfg)) settings_next_auto_tx(s);   // config.txt に設定がなければ飛ばす
                               break;
    case UI_MENU_AMBIGUITY:    settings_next_ambiguity(s); break;
    case UI_MENU_DIGIPATH:     settings_next_digipath(s); break;
    case UI_MENU_SYMBOL:       settings_next_symbol(s); break;
    case UI_MENU_SSID:         settings_next_ssid(s); break;
    case UI_MENU_COURSE_SPEED: settings_toggle_course_speed(s); break;
    case UI_MENU_ALTITUDE:     settings_toggle_altitude(s); break;
    case UI_MENU_MONITOR:      settings_next_monitor(s); break;
    case UI_MENU_CHIME:        settings_next_chime(s); break;
    case UI_MENU_BRIGHTNESS:   settings_next_brightness(s); break;
    case UI_MENU_LANGUAGE:     settings_next_language(s); ui_set_lang(s->cur.language); break;   // すぐ切り替える
    case UI_MENU_POSSRC:       settings_next_position_source(s); break;
    case UI_MENU_FIXED:        settings_next_fixed(s); break;
    case UI_MENU_CALIB:        ui->screen = UI_SCREEN_CALIB; break;      // 調整トーンの画面を開く (ここでは送信しない)
    case UI_MENU_CLEAR:        ui->screen = UI_SCREEN_CLEAR; ui->clearConfirm = false; break;   // 記録の消去の画面を開く (ここでは消さない)
    case UI_MENU_VERSION:      break;                                     // 表示だけ (切り替える値はない)
  }
}

// ボタンの共通ルール (仕様4.5):
//   短押し = その画面の中の操作   A=前  B=決定(開く/切り替え/開始)  C=次
//   長押し = どこでも同じ働き     A=ビーコン送信  B=戻る(1つ上へ)  C=設定メニュー(開く/閉じる)
static uint8_t calib_step(uint8_t m, int dir) {
  int n = AFSK_TONE_ALT - AFSK_TONE_MARK + 1;
  int i = ((int)m - AFSK_TONE_MARK + dir + n) % n;
  return (uint8_t)(AFSK_TONE_MARK + i);
}

static UiAction on_short(UiState* ui, SettingsMgr* s, int btn) {
  if (ui->txBusy || ui->calibOn) return UI_ACT_NONE;
  if (ui->cmp.active && ui->screen == UI_SCREEN_MSG) return cmp_short(ui, btn);
  if (ui->screen == UI_SCREEN_CALIB) {                 // 停止中: A/C=モード  B=開始
    if (btn == 1) return UI_ACT_CALIB_TOGGLE;
    ui->calibMode = calib_step(ui->calibMode, btn == 0 ? -1 : +1);
    return UI_ACT_NONE;
  }
  if (ui->screen == UI_SCREEN_CLEAR) {                 // A/C=対象、B=確認 → もう一度 B で消す。確認中の A/C はやめる
    if (ui->clearConfirm) {
      ui->clearConfirm = false;
      if (btn != 1) return UI_ACT_NONE;
      ui_clear(ui, ui->clearTarget); ui->clearDone = ui->clearTarget;
      return UI_ACT_CLEARED;
    }
    if (btn == 1) ui->clearConfirm = true;
    else ui->clearTarget = (uint8_t)((ui->clearTarget + (btn == 0 ? UI_CLEAR_COUNT - 1 : 1)) % UI_CLEAR_COUNT);
    return UI_ACT_NONE;
  }
  if (ui->screen == UI_SCREEN_MENU) {
    if (btn == 0) ui->menuItem = (uint8_t)menu_step(ui->menuItem, -1, s);
    else if (btn == 2) ui->menuItem = (uint8_t)menu_step(ui->menuItem, +1, s);
    else menu_change(ui, s);
    return UI_ACT_NONE;
  }
  if (ui->screen == UI_SCREEN_MSG) {                   // メッセージ: B短=詳細を開く。詳細ではA/C=前後、B短=作成/返信/再送
    uint32_t seqs[MSG_STORE_COUNT + 1]; int n = msg_list(ui, seqs);
    if (ui->msgDetail) {
      int cur = 0;
      for (int i = 0; i < n; i++) if (seqs[i] == ui->msgSel) cur = i;
      if (btn != 1) { msg_select(ui, seqs[(cur + (btn == 0 ? n - 1 : 1)) % n]); return UI_ACT_NONE; }
      int idx = (ui->msgs && ui->msgSel) ? msg_find_seq(ui->msgs, ui->msgSel) : -1;
      if (idx < 0) { cmp_open(ui, "", "", UI_CMP_TO, ""); return UI_ACT_NONE; }      // 新しいメッセージ
      const MsgEntry* e = &ui->msgs->e[idx];
      if (e->dir == MSG_DIR_OUT) cmp_open(ui, e->to, e->text, UI_CMP_CONFIRM, "");      // もう一度送る
      else cmp_open(ui, e->from, "", UI_CMP_TEXT, e->from);                             // 返信
      return UI_ACT_NONE;
    }
    if (btn == 1) { ui->msgDetail = true; msg_select(ui, n > 1 ? seqs[1] : 0); return UI_ACT_NONE; }
  }
  if (ui->screen == UI_SCREEN_RX) {                    // 受信一覧: B短=新しい1件の詳細。詳細ではA/C=前後、B短=その局へメッセージを作る
    int n = ui->rxCount;
    if (ui->rxDetail) {
      int cur = -1;
      for (int i = 0; i < n; i++) if (ui->rx[rx_idx(ui, i)].seq == ui->rxSel) cur = i;
      if (btn != 1) {
        if (n > 0) { cur = (cur < 0) ? 0 : (cur + (btn == 0 ? n - 1 : 1)) % n; ui->rxSel = ui->rx[rx_idx(ui, cur)].seq; }
        return UI_ACT_NONE;
      }
      if (cur >= 0) {
        const UiRxEntry* e = &ui->rx[rx_idx(ui, cur)];
        if (!(ui->myCall[0] && msg_call_equal(e->src, ui->myCall))) {
          ui->screen = UI_SCREEN_MSG; ui->msgDetail = true; ui->msgSel = 0;
          cmp_open(ui, e->src, "", UI_CMP_TEXT, e->src);          // 宛先を入れた状態で本文の選択から
        }
      }
      return UI_ACT_NONE;
    }
    if (btn == 1) {
      if (n > 0) { ui->rxDetail = true; ui->rxSel = ui->rx[rx_idx(ui, 0)].seq; }
      return UI_ACT_NONE;
    }
  }
  if (ui->screen == UI_SCREEN_WX) {                    // WX: B短=新しい局の詳細を開く。詳細ではA/C=前後の局
    uint8_t ord[WX_STATION_COUNT]; int n = ui_wx_order(ui, ord);
    if (ui->wxDetail) {
      int cur = 0;
      for (int i = 0; i < n; i++) if (!strcmp(ui->wx[ord[i]].src, ui->wxSel)) cur = i;
      if (n > 0 && btn != 1) {
        cur = (cur + (btn == 0 ? n - 1 : 1)) % n;
        snprintf(ui->wxSel, sizeof(ui->wxSel), "%s", ui->wx[ord[cur]].src);
      }
      return UI_ACT_NONE;
    }
    if (btn == 1) {
      if (n > 0) { ui->wxDetail = true; snprintf(ui->wxSel, sizeof(ui->wxSel), "%s", ui->wx[ord[0]].src); }
      return UI_ACT_NONE;
    }
  }
  if (ui->screen == UI_SCREEN_RADAR) {                 // レーダー: B短=切り替えモードに入る/抜ける。モード中は A=距離 C=時間
    if (btn == 1) { ui->radarMode = !ui->radarMode; return UI_ACT_NONE; }
    if (ui->radarMode) {
      if (btn == 0) settings_next_radar_range(s); else settings_next_radar_time(s);
      return UI_ACT_NONE;
    }
  }
  ui->radarMode = false;
  if (btn == 0) ui->screen = (uint8_t)((ui->screen + UI_SCREEN_COUNT - 1) % UI_SCREEN_COUNT);
  else if (btn == 2) ui->screen = (uint8_t)((ui->screen + 1) % UI_SCREEN_COUNT);
  // B短 (決定): 詳細のない画面では何もしない
  return UI_ACT_NONE;
}

static UiAction on_long(UiState* ui, int btn) {
  if (ui->txBusy || ui->calibOn) return UI_ACT_NONE;
  if (ui->cmp.active && ui->screen == UI_SCREEN_MSG) {  // 作成中: B長=1つ戻る。送信・メニューは使わない
    if (btn == 1) cmp_back(ui);
    return UI_ACT_NONE;
  }
  if (ui->screen == UI_SCREEN_CALIB || ui->screen == UI_SCREEN_CLEAR) {   // B長=メニューへ戻る。A長(送信)・C長は使わない
    if (btn == 1) { ui->screen = UI_SCREEN_MENU; ui->clearConfirm = false; }
    return UI_ACT_NONE;
  }
  if (ui->screen == UI_SCREEN_MENU) {                  // B長/C長=閉じる。メニューの中からは送信しない
    if (btn == 1 || btn == 2) ui->screen = ui->returnScreen;
    return UI_ACT_NONE;
  }
  if (btn == 0) return UI_ACT_SEND;                    // A長=ビーコン送信
  if (btn == 1) {                                      // B長=戻る
    if (ui->screen == UI_SCREEN_WX && ui->wxDetail) ui->wxDetail = false;
    if (ui->screen == UI_SCREEN_RX && ui->rxDetail) ui->rxDetail = false;
    if (ui->screen == UI_SCREEN_MSG && ui->msgDetail) ui->msgDetail = false;
    return UI_ACT_NONE;
  }
  if (btn == 2) {                                      // C長=設定メニュー
    ui->returnScreen = ui->screen;
    ui->screen = UI_SCREEN_MENU;
    ui->menuItem = 0;
  }
  return UI_ACT_NONE;                                  // B長: 通常画面では戻る先がない
}

UiAction ui_buttons(UiState* ui, SettingsMgr* s, bool a, bool b, bool c, uint32_t nowMs) {
  const bool in[3] = { a, b, c };
  UiAction act = UI_ACT_NONE;
  for (int i = 0; i < 3; i++) {
    if (in[i] && !ui->down[i]) {
      ui->down[i] = true; ui->downSince[i] = nowMs;
      ui->consumed[i] = ui->txBusy;                  // 送信中に押し始めたものは無効
      if (ui->calibOn) { ui->consumed[i] = true; act = UI_ACT_CALIB_TOGGLE; }   // 調整トーン中は、どのボタンでも押した瞬間に停止
      int others = 0;
      for (int j = 0; j < 3; j++) if (j != i && ui->down[j]) others++;
      if (others) for (int j = 0; j < 3; j++) if (ui->down[j]) ui->consumed[j] = true;   // 同時押しは無効
    }
  }
  for (int i = 0; i < 3; i++) {
    if (in[i]) {
      if (!ui->consumed[i] && (uint32_t)(nowMs - ui->downSince[i]) >= LONG_PRESS_MS) {
        ui->consumed[i] = true;
        UiAction r = on_long(ui, i);
        if (r != UI_ACT_NONE) act = r;
      }
    } else if (ui->down[i]) {
      ui->down[i] = false;
      if (!ui->consumed[i]) {
        UiAction r = on_short(ui, s, i);
        if (r != UI_ACT_NONE) act = r;
      }
      ui->consumed[i] = false;
    }
  }
  return act;
}

// レーダー用の局の表: 局ごとに最後の位置。自局 (今のコールサインとSSID) は入れない。いっぱいなら受信がいちばん古い局と入れ替える
static void station_update(UiState* ui, const AprsPacket* pkt, const LogTime* t, uint32_t nowMs) {
  if (!pkt->src[0] || (ui->myCall[0] && msg_call_equal(pkt->src, ui->myCall))) return;
  int slot = -1;
  for (int i = 0; i < RADAR_STATION_COUNT; i++) if (ui->st[i].used && msg_call_equal(ui->st[i].call, pkt->src)) { slot = i; break; }
  if (slot < 0) for (int i = 0; i < RADAR_STATION_COUNT; i++) if (!ui->st[i].used) { slot = i; break; }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < RADAR_STATION_COUNT; i++) if ((int32_t)(ui->st[i].ms - ui->st[slot].ms) < 0) slot = i;
  }
  UiStation* s = &ui->st[slot];
  memset(s, 0, sizeof(*s));
  s->used = true;
  snprintf(s->call, sizeof(s->call), "%s", pkt->src);
  s->symbol[0] = pkt->symTable; s->symbol[1] = pkt->symCode;
  s->latDeg = pkt->latDeg; s->lonDeg = pkt->lonDeg;
  s->ms = nowMs; s->utc = log_time_epoch(t);
}

void ui_rx_add(UiState* ui, const AprsPacket* pkt, bool ownValid, double ownLat, double ownLon, const LogTime* t, uint32_t nowMs) {
  if (pkt->kind == APRS_KIND_TELEMETRY) return;      // テレメトリは一覧に出さない (受信ログには残る)
  UiRxEntry* e = &ui->rx[ui->rxHead];
  memset(e, 0, sizeof(*e));
  e->seq = ++ui->rxTotal;
  snprintf(e->src, sizeof(e->src), "%s", pkt->src);
  if (t && t->valid) snprintf(e->time, sizeof(e->time), "%02u:%02u:%02u", t->hour % 100u, t->minute % 100u, t->second % 100u);
  else snprintf(e->time, sizeof(e->time), "--:--:--");
  e->ms = nowMs; e->utc = log_time_epoch(t);
  e->kind = (uint8_t)pkt->kind;
  e->weather = pkt->weather.present;
  e->hasPos = pkt->hasPos;
  snprintf(e->via, sizeof(e->via), "%s", pkt->via);
  if (pkt->kind == APRS_KIND_MICE) snprintf(e->micEMsg, sizeof(e->micEMsg), "%s", pkt->micEMessage ? pkt->micEMessage : "");
  snprintf(e->model, sizeof(e->model), "%s", pkt->model);       // 機種 (Mic-E の印、または宛先から)
  if (pkt->hasPos) {
    e->latDeg = pkt->latDeg; e->lonDeg = pkt->lonDeg;
    e->symbol[0] = pkt->symTable; e->symbol[1] = pkt->symCode;
    if (pkt->hasAltitude) { e->hasAlt = true; e->altM = (int32_t)lround(pkt->altitudeM); }
    if (pkt->hasSpeed) { e->hasSpeed = true; e->speedKmh = (int32_t)lround(aprs_knots_to_kmh(pkt->speedKnots)); }
    if (pkt->hasCourse) { e->hasCourse = true; e->courseDeg = pkt->courseDeg; }
    if (ownValid) {
      e->hasDist = true;
      e->distKm = geo_distance_km(ownLat, ownLon, pkt->latDeg, pkt->lonDeg);
      e->bearingDeg = geo_bearing_deg(ownLat, ownLon, pkt->latDeg, pkt->lonDeg);
    }
  }
  // 本文: 種類ごとに整える
  if (pkt->kind == APRS_KIND_MESSAGE && pkt->msg.present) {
    snprintf(e->to, sizeof(e->to), "%s", pkt->msg.addressee);
    if (pkt->msg.isAck || pkt->msg.isRej) snprintf(e->comment, sizeof(e->comment), "%s%s", pkt->msg.isAck ? "Ack " : "Rej ", pkt->msg.ackNo);
    else snprintf(e->comment, sizeof(e->comment), "%s", pkt->msg.text);
  } else if (pkt->kind == APRS_KIND_WEATHER && !pkt->hasPos) {      // 位置なしの気象: 主な値を短く
    const AprsWeather* w = &pkt->weather;
    char b[PARSE_COMMENT_MAX] = "", x[32];
    if (w->hasTemp)      { snprintf(x, sizeof(x), "%.1f°C ", w->tempC); strncat(b, x, sizeof(b) - strlen(b) - 1); }
    if (w->hasHumidity)  { snprintf(x, sizeof(x), L("湿度%u%% "), (unsigned)w->humidityPct); strncat(b, x, sizeof(b) - strlen(b) - 1); }
    if (w->hasPressure)  { snprintf(x, sizeof(x), "%.1fhPa ", w->pressureHpa); strncat(b, x, sizeof(b) - strlen(b) - 1); }
    if (w->hasWindSpeed) { snprintf(x, sizeof(x), L("風%.1fm/s "), w->windSpeedMs); strncat(b, x, sizeof(b) - strlen(b) - 1); }
    if (w->hasRain1h)    { snprintf(x, sizeof(x), L("雨%.1fmm "), w->rain1hMm); strncat(b, x, sizeof(b) - strlen(b) - 1); }
    size_t n = strlen(b); if (n && b[n - 1] == ' ') b[n - 1] = 0;
    snprintf(e->comment, sizeof(e->comment), "%s", b);
  } else {
    snprintf(e->comment, sizeof(e->comment), "%s", pkt->comment);
  }
  if (pkt->hasPos) station_update(ui, pkt, t, nowMs);
  ui->rxHead = (uint16_t)((ui->rxHead + 1) % RX_LIST_COUNT);
  if (ui->rxCount < RX_LIST_COUNT) ui->rxCount++;
  ui->hasLastRx = true; ui->lastRxMs = nowMs;
  if (pkt->weather.present) ui_wx_add(ui, pkt, ownValid, ownLat, ownLon, t, nowMs);
}

int ui_wx_order(const UiState* ui, uint8_t out[WX_STATION_COUNT]) {
  int n = 0;
  for (int i = 0; i < WX_STATION_COUNT; i++) if (ui->wx[i].used) out[n++] = (uint8_t)i;
  for (int a = 1; a < n; a++) {                     // 新しい順 (挿入ソート。最大8件)
    uint8_t x = out[a]; int b = a - 1;
    while (b >= 0 && (int32_t)(ui->wx[out[b]].lastMs - ui->wx[x].lastMs) < 0) { out[b + 1] = out[b]; b--; }
    out[b + 1] = x;
  }
  return n;
}

void ui_after_restore(UiState* ui, uint32_t nowMs) {
  uint8_t ord[WX_STATION_COUNT]; int n = ui_wx_order(ui, ord);   // 保存されていた順 (新しい順) を保つ
  for (int i = 0; i < n; i++) {
    UiWxEntry* e = &ui->wx[ord[i]];
    e->restored = true;
    e->lastMs = nowMs - 0x40000000u - (uint32_t)i * 1000u;      // 起動後に受信した局より古い
  }
  for (int i = 0; i < RX_LIST_COUNT; i++) { ui->rx[i].restored = true; ui->rx[i].ms = nowMs - 0x40000000u; }
  for (int i = 0; i < RADAR_STATION_COUNT; i++) {        // 経過時間は保存したUTCから出す。入れ替えの順 (ms) は受信の古い順を保つ
    if (!ui->st[i].used) continue;
    uint32_t rank = 0;
    for (int j = 0; j < RADAR_STATION_COUNT; j++)
      if (ui->st[j].used && (ui->st[j].utc < ui->st[i].utc || (ui->st[j].utc == ui->st[i].utc && j < i))) rank++;
    ui->st[i].restored = true;
    ui->st[i].ms = nowMs - 0x40000000u + rank;
  }
  ui->radarMode = false;
  ui->hasLastRx = false;
  ui->wxDetail = false; ui->msgDetail = false; ui->rxDetail = false;
}

void ui_wx_add(UiState* ui, const AprsPacket* pkt, bool ownValid, double ownLat, double ownLon, const LogTime* t, uint32_t nowMs) {
  if (!pkt->weather.present) return;
  int slot = -1;
  for (int i = 0; i < WX_STATION_COUNT; i++) if (ui->wx[i].used && !strcmp(ui->wx[i].src, pkt->src)) { slot = i; break; }
  bool same = (slot >= 0);
  if (slot < 0) for (int i = 0; i < WX_STATION_COUNT; i++) if (!ui->wx[i].used) { slot = i; break; }
  if (slot < 0) {                                   // いっぱい: 受信がいちばん古い局と入れ替え (詳細表示中の局は残す)
    for (int i = 0; i < WX_STATION_COUNT; i++) {
      if (ui->wxDetail && !strcmp(ui->wx[i].src, ui->wxSel)) continue;
      if (slot < 0 || (int32_t)(ui->wx[i].lastMs - ui->wx[slot].lastMs) < 0) slot = i;
    }
  }
  UiWxEntry* e = &ui->wx[slot];
  bool keepPos = same && e->hasPos && !pkt->hasPos;  // 位置なしの気象レポートなら、前の位置を残す
  UiWxEntry old = *e;
  memset(e, 0, sizeof(*e));
  e->used = true;
  snprintf(e->src, sizeof(e->src), "%s", pkt->src);
  if (t && t->valid) snprintf(e->time, sizeof(e->time), "%02u:%02u:%02u", t->hour % 100u, t->minute % 100u, t->second % 100u);
  else snprintf(e->time, sizeof(e->time), "--:--:--");
  e->lastMs = nowMs;
  e->utc = log_time_epoch(t);
  e->count = same ? old.count + 1 : 1;
  e->w = pkt->weather;
  if (pkt->hasPos) {
    e->hasPos = true; e->latDeg = pkt->latDeg; e->lonDeg = pkt->lonDeg;
    if (ownValid) { e->hasDist = true; e->distKm = geo_distance_km(ownLat, ownLon, pkt->latDeg, pkt->lonDeg); e->bearingDeg = geo_bearing_deg(ownLat, ownLon, pkt->latDeg, pkt->lonDeg); }
  } else if (keepPos) {
    e->hasPos = true; e->latDeg = old.latDeg; e->lonDeg = old.lonDeg;
    e->hasDist = old.hasDist; e->distKm = old.distKm; e->bearingDeg = old.bearingDeg;
  }
  snprintf(e->comment, sizeof(e->comment), "%.47s", pkt->comment);
}

// ---------------------------------------------------------------
// 表示の組み立て
// ---------------------------------------------------------------
static void add(UiView* v, uint8_t color, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void add(UiView* v, uint8_t color, const char* fmt, ...) {
  if (v->count >= UI_MAX_LINES) return;
  UiLine* l = &v->lines[v->count++];
  va_list ap; va_start(ap, fmt);
  vsnprintf(l->text, sizeof(l->text), fmt, ap);
  va_end(ap);
  ui_fit(l->text, UI_COLS);
  l->color = color;
  l->icon = 0; l->iconCol = 0; l->iconBig = 0;
}
// 最後に足した行にアイコンを付ける
static void set_icon(UiView* v, int icon, int col, bool big) {
  if (v->count == 0 || icon <= 0) return;
  UiLine* l = &v->lines[v->count - 1];
  l->icon = (uint8_t)icon; l->iconCol = (uint8_t)col; l->iconBig = big ? 1 : 0;
  if (big) ui_fit(l->text, UI_COLS - UI_BIGICON_COLS);
}

// ヘッダ: 左に画面名だけ。右側の状態はアイコン (ui_render の make_icons、描画は ui_m5)
static void header(UiView* v, const UiState* ui, const UiSnapshot* sn, const char* title) {
  (void)sn;
  char left[UI_TEXT_MAX];
  snprintf(left, sizeof(left), "%s", title);
  ui_fit(left, UI_TITLE_COLS);
  add(v, ui->txBusy ? UI_COL_BAD : UI_COL_HEAD, "%s", left);
}

static void make_icons(const UiState* ui, const UiSnapshot* sn, UiIcons* ic) {
  memset(ic, 0, sizeof(*ic));
  if (sn->battLevel < 0) ic->batt = -1;
  else { int b = (sn->battLevel + 12) / 25; ic->batt = (int8_t)(b > 4 ? 4 : b); }
  ic->charging = sn->battCharging ? 1 : 0;
  ic->sdOk = sn->sdOk ? 1 : 0;
  int lv = sn->settings ? sn->settings->cur.monitorLevel : 0;
  int vol = MONITOR_LEVEL_MAX > 0 ? (lv * 4 + MONITOR_LEVEL_MAX - 1) / MONITOR_LEVEL_MAX : 0;
  ic->vol = (uint8_t)(vol > 4 ? 4 : vol);
  int bl = sn->settings ? sn->settings->cur.chimeLevel : 0;
  int bell = MONITOR_LEVEL_MAX > 0 ? (bl * 4 + MONITOR_LEVEL_MAX - 1) / MONITOR_LEVEL_MAX : 0;
  ic->bell = (uint8_t)(bell > 4 ? 4 : bell);
  if (sn->posSource == POSITION_SOURCE_LAST) ic->pos = UI_POSICON_LAST;
  else if (sn->posSource == POSITION_SOURCE_FIXED) ic->pos = UI_POSICON_FIXED;
  else switch (sn->gps) {
    case GPS_NO_DATA: ic->pos = UI_POSICON_GPS_NONE; break;
    case GPS_NO_FIX:  ic->pos = UI_POSICON_GPS_WAIT; break;
    case GPS_STALE:   ic->pos = UI_POSICON_GPS_STALE; break;
    default:          ic->pos = UI_POSICON_GPS_FIX; break;
  }
  // フレックスで config.txt に値がないときは、ビーコンは送らず Ack だけ返す
  ic->autoTx = (auto_tx_interval_min(sn->autoTx) || (auto_tx_is_flex(sn->autoTx) && app_config_flex_ok(&g_cfg))) ? UI_AUTOICON_ON :
               (auto_tx_ack_enabled(sn->autoTx) ? UI_AUTOICON_ACK : UI_AUTOICON_OFF);
  ic->act = (ui->txBusy || ui->calibOn) ? UI_ACTICON_TX : (sn->rxCarrier ? UI_ACTICON_RX : UI_ACTICON_NONE);
  ic->unread = (ui->msgs && msg_unread(ui->msgs) > 0) ? 1 : 0;
}

// "35.68123N 139.76712E"
static void fmt_pos(char* out, size_t n, double lat, double lon) {
  snprintf(out, n, "%.5f%c %.5f%c", fabs(lat), lat >= 0 ? 'N' : 'S', fabs(lon), lon >= 0 ? 'E' : 'W');
}

static void wrap_add(UiView* v, uint8_t color, const char* s, int cols, int maxLines) {
  size_t len = strlen(s);
  size_t i = 0;
  for (int k = 0; k < maxLines && i < len; k++) {
    char buf[UI_TEXT_MAX];
    size_t n = len - i; if (n > (size_t)cols) n = (size_t)cols;
    memcpy(buf, s + i, n); buf[n] = 0;
    add(v, color, "%s", buf);
    i += n;
  }
}

static void render_own(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  header(v, ui, sn, L("自局"));
  if (sn->posSource == POSITION_SOURCE_GPS) {
    switch (sn->gps) {
      case GPS_NO_DATA: add(v, UI_COL_BAD, "%s", L("GPS受信なし (配線・電源を確認)")); break;
      case GPS_NO_FIX:  add(v, UI_COL_WARN, L("GPS測位待ち  衛星%u"), (unsigned)sn->satellites); break;
      case GPS_STALE:   add(v, UI_COL_WARN, L("GPS途切れ 最終位置 %um%02us前"), (unsigned)(sn->posAgeSec / 60), (unsigned)(sn->posAgeSec % 60)); break;
      default:          add(v, UI_COL_GOOD, L("GPS測位中  衛星%u"), (unsigned)sn->satellites); break;
    }
  } else {
    // GPS以外の取得元: 現在地ではないことが分かるよう目立つ色で、取得元を表示。GPSの状態も短く添える
    const char* gs = (sn->gps == GPS_NO_DATA) ? L("受信なし") : (sn->gps == GPS_NO_FIX) ? L("測位待ち") : (sn->gps == GPS_STALE) ? L("途切れ") : L("測位中");
    if (sn->posSource == POSITION_SOURCE_LAST) add(v, UI_COL_HILITE, L("位置:保存位置  GPS:%s"), gs);
    else add(v, UI_COL_HILITE, L("位置:固定 %.10s  GPS:%s"), sn->posLabel, gs);
  }
  char t[UI_TEXT_MAX];
  if (sn->hasPos) { fmt_pos(t, sizeof(t), sn->latDeg, sn->lonDeg); add(v, UI_COL_NORMAL, L("位置 %s"), t); }
  else add(v, UI_COL_DIM, "%s", L("位置 ---"));
  if (sn->hasUtc) add(v, UI_COL_NORMAL, "UTC %04u-%02u-%02u %02u:%02u:%02u", sn->utc.year, sn->utc.month, sn->utc.day, sn->utc.hour, sn->utc.minute, sn->utc.second);
  else add(v, UI_COL_DIM, "UTC ---");
  if (ui->txBusy) add(v, UI_COL_BAD, "%s", sn->ptt ? L("PTT ON (送信中)") : L("PTT OFF (送信終了処理中)"));
  else if (!app_config_callsign_ok(&g_cfg)) add(v, UI_COL_BAD, "%s", L("コールサイン未設定 (config.txt)"));
  else if (!sn->previewValid) {
    switch (sn->posReason) {
      case POS_LAST_NONE:   add(v, UI_COL_BAD, "%s", L("保存された位置がありません")); break;
      case POS_FIXED_UNSET: add(v, UI_COL_BAD, "%s", L("固定位置が未設定です")); break;
      default:              add(v, UI_COL_BAD, "%s", L("未測位のため送信できません")); break;
    }
  }
  else if (sn->posSource == POSITION_SOURCE_LAST)  add(v, UI_COL_WARN, "%s", L("保存位置で送信します"));
  else if (sn->posSource == POSITION_SOURCE_FIXED) add(v, UI_COL_WARN, "%s", L("固定位置で送信します"));
  else if (sn->previewStale)  add(v, UI_COL_WARN, "%s", L("最終測位位置で送信します"));
  else                        add(v, UI_COL_GOOD, "%s", L("送信可 (PTT自動)"));
  // 「送信内容:」の行の先頭に自局のシンボルのアイコン
  const SymbolChoice* sym = settings_symbol(sn->settings);
  if (auto_tx_is_flex(sn->autoTx) && !app_config_flex_ok(&g_cfg)) add(v, UI_COL_WARN, "  %s", L("送信内容:  フレックスの設定なし"));
  else if (!auto_tx_beacon(sn->autoTx)) add(v, UI_COL_DIM, "  %s", L("送信内容:"));
  else if (sn->autoWaiting) add(v, UI_COL_WARN, "  %s", L("送信内容:  自動送信は他局の送信待ち"));
  else { uint32_t r = (sn->autoRemainMs + 999) / 1000; char t2[UI_TEXT_MAX]; snprintf(t2, sizeof(t2), L("送信内容:  次の自動送信まで %u:%02u"), (unsigned)(r / 60), (unsigned)(r % 60)); add(v, UI_COL_DIM, "  %s", t2); }
  set_icon(v, sym->icon, 0, false);
  if (sn->previewValid) {
    wrap_add(v, UI_COL_NORMAL, sn->previewHeader, UI_COLS, 2);
    wrap_add(v, UI_COL_NORMAL, sn->previewInfo, UI_COLS, 3);
  } else {
    uint8_t ssid = settings_ssid(sn->settings);
    if (ssid) add(v, UI_COL_DIM, "%s-%u %c%c %s", g_cfg.callsign, (unsigned)ssid, sym->table, sym->code, L(sym->name));
    else add(v, UI_COL_DIM, "%s %c%c %s", g_cfg.callsign, sym->table, sym->code, L(sym->name));
  }
  if (ui->msgUntilMs && (int32_t)(ui->msgUntilMs - nowMs) > 0) add(v, UI_COL_WARN, "%s", ui->msg);
}

static void age_or_dash(char* out, size_t n, const UiSnapshot* sn, bool restored, uint32_t ms, uint32_t utc, uint32_t nowMs);
static const char* rx_kind_label(const UiRxEntry* e) {
  switch (e->kind) {
    case APRS_KIND_MICE:       return "Mic-E";
    case APRS_KIND_POSITION:   return L("位置");
    case APRS_KIND_WEATHER:    return L("気象");
    case APRS_KIND_MESSAGE:    return L("メッセージ");
    case APRS_KIND_STATUS:     return L("ステータス");
    case APRS_KIND_CAPS:       return L("機能");
    case APRS_KIND_THIRDPARTY: return L("iGate経由");
    default:                   return L("その他");
  }
}

// 文字列を cols 桁ずつ (UTF-8の切れ目で) 最大 maxLines 行に分けて出す。戻り値: 出した行数
static int wrap_cols(UiView* v, uint8_t color, const char* indent, const char* s, int cols, int maxLines) {
  int lines = 0;
  while (*s && lines < maxLines && v->count < UI_MAX_LINES) {
    char buf[UI_TEXT_MAX]; snprintf(buf, sizeof(buf), "%s", s);
    ui_fit(buf, cols);
    size_t n = strlen(buf);
    if (n == 0) break;
    add(v, color, "%s%s", indent, buf);
    s += n; lines++;
  }
  return lines;
}

static void render_rx_list(const UiState* ui, const UiSnapshot* sn, UiView* v) {
  char title[32]; snprintf(title, sizeof(title), L("受信一覧(計%u)"), (unsigned)(ui->rxTotal > 9999 ? 9999 : ui->rxTotal));
  header(v, ui, sn, title);
  if (ui->rxCount == 0) { add(v, UI_COL_DIM, "%s", L("まだ受信していません")); return; }
  int shown = (UI_MAX_LINES - 1) / 2;
  for (int i = 0; i < shown && i < (int)ui->rxCount; i++) {
    const UiRxEntry* e = &ui->rx[rx_idx(ui, i)];
    bool sk = sakura_call(e->src);
    if (!e->hasPos) {
      add(v, sk ? UI_COL_SAKURA : UI_COL_DIM, "  %s %-9s %s", e->time, e->src, rx_kind_label(e));
      if (e->kind == APRS_KIND_MESSAGE) add(v, sk ? UI_COL_SAKURA : UI_COL_DIM, "  →%s %s", e->to, e->comment);
      else add(v, sk ? UI_COL_SAKURA : UI_COL_DIM, "  %s", e->comment[0] ? e->comment : L("(空)"));
      continue;
    }
    char info[48];
    if (e->hasDist) {
      int b = (int)lround(e->bearingDeg) % 360;
      if (e->distKm < 10.0) snprintf(info, sizeof(info), "%.1fkm %03d°", e->distKm, b);
      else snprintf(info, sizeof(info), "%.0fkm %03d°", e->distKm, b);
    } else snprintf(info, sizeof(info), "%s", L("距離不明"));
    char spd[24];
    if (e->hasSpeed) snprintf(spd, sizeof(spd), " %ldkm/h", (long)e->speedKmh); else snprintf(spd, sizeof(spd), " -km/h");
    // 先頭の2桁はシンボルのアイコン。入りきらないときは時刻の秒を省く
    int icon = symbol_icon(e->symbol[0], e->symbol[1]);
    char l1[UI_TEXT_MAX]; snprintf(l1, sizeof(l1), "  %s %-9s %s%s", e->time, e->src, info, spd);
    char fit[UI_TEXT_MAX]; snprintf(fit, sizeof(fit), "%s", l1);
    if (ui_fit(fit, 1000) > UI_COLS) snprintf(l1, sizeof(l1), "  %.5s %-9s %s%s", e->time, e->src, info, spd);
    add(v, sk ? UI_COL_SAKURA : UI_COL_NORMAL, "%s", l1);
    set_icon(v, icon, 0, false);
    char alt[16] = "";
    if (e->hasAlt) snprintf(alt, sizeof(alt), " %ldm", (long)e->altM);
    if (icon) add(v, sk ? UI_COL_SAKURA : UI_COL_DIM, " %s %s", alt, e->comment);         // シンボルはアイコンで分かるので文字は出さない
    else add(v, sk ? UI_COL_SAKURA : UI_COL_DIM, "  %c%c%s %s", e->symbol[0], e->symbol[1], alt, e->comment);
  }
}

static void render_rx_detail(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  int n = ui->rxCount, cur = -1;
  for (int i = 0; i < n; i++) if (ui->rx[rx_idx(ui, i)].seq == ui->rxSel) cur = i;
  char title[32];
  if (cur < 0) { header(v, ui, sn, L("受信詳細")); add(v, UI_COL_DIM, "%s", L("この受信は一覧から消えました")); return; }
  snprintf(title, sizeof(title), L("受信詳細 %d/%d"), cur + 1, n);
  header(v, ui, sn, title);
  const UiRxEntry* e = &ui->rx[rx_idx(ui, cur)];
  char a[24]; age_or_dash(a, sizeof(a), sn, e->restored, e->ms, e->utc, nowMs);
  int icon = e->hasPos ? symbol_icon(e->symbol[0], e->symbol[1]) : 0;   // 32x32 で、この2行の右端に描く
  add(v, sakura_call(e->src) ? UI_COL_SAKURA : UI_COL_HILITE, "%s %sZ %s", e->src, e->time, a);   // 右端に大きいアイコンを置くので短く (Z = UTC)
  set_icon(v, icon, 0, true);
  if (e->kind == APRS_KIND_MICE && e->micEMsg[0]) add(v, UI_COL_NORMAL, L("種類: Mic-E  %s"), e->micEMsg);
  else add(v, UI_COL_NORMAL, L("種類: %s%s"), rx_kind_label(e), (e->weather && e->kind != APRS_KIND_WEATHER) ? L(" (気象)") : "");
  if (icon) ui_fit(v->lines[v->count - 1].text, UI_COLS - UI_BIGICON_COLS);
  if (e->model[0]) add(v, UI_COL_NORMAL, L("機種: %s"), e->model);
  if (e->hasPos) {
    add(v, UI_COL_NORMAL, L("位置: %.5f%c %.5f%c"), fabs(e->latDeg), e->latDeg >= 0 ? 'N' : 'S', fabs(e->lonDeg), e->lonDeg >= 0 ? 'E' : 'W');
    if (e->hasDist) add(v, UI_COL_NORMAL, L("距離: %.1fkm  方位: %03d°"), e->distKm, (int)lround(e->bearingDeg) % 360);
    else add(v, UI_COL_DIM, "%s", L("距離: 不明 (自局の位置なし)"));
    if (e->hasSpeed) {
      char c[8]; if (e->hasCourse) snprintf(c, sizeof(c), "%03u°", (unsigned)e->courseDeg); else snprintf(c, sizeof(c), "---");
      add(v, UI_COL_NORMAL, L("速度: %ldkm/h  進路: %s"), (long)e->speedKmh, c);
    }
    char alt[16]; if (e->hasAlt) snprintf(alt, sizeof(alt), "%ldm", (long)e->altM); else snprintf(alt, sizeof(alt), "---");
    add(v, UI_COL_NORMAL, L("高度: %-6s シンボル: %c%c"), alt, e->symbol[0], e->symbol[1]);
    add(v, UI_COL_NORMAL, L("経路: %s"), e->via[0] ? e->via : L("(直接)"));
    if (e->comment[0]) {
      add(v, UI_COL_DIM, "%s", L("コメント:"));
      wrap_cols(v, UI_COL_NORMAL, "  ", e->comment, UI_COLS - 2, UI_MAX_LINES - v->count);
    }
    return;
  }
  if (e->kind == APRS_KIND_MESSAGE) add(v, UI_COL_NORMAL, L("宛先: %s"), e->to);
  add(v, UI_COL_NORMAL, L("経路: %s"), e->via[0] ? e->via : L("(直接)"));
  add(v, UI_COL_NORMAL, "%s", "");
  wrap_cols(v, UI_COL_NORMAL, "", e->comment[0] ? e->comment : L("(空)"), UI_COLS, UI_MAX_LINES - v->count);
}

static void render_rx(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  if (ui->rxDetail) render_rx_detail(ui, sn, v, nowMs); else render_rx_list(ui, sn, v);
}

static const char* onoff(bool b) { return b ? L("オン") : L("オフ"); }

// ---- WX(気象)画面 ----
static void age_text(char* out, size_t n, uint32_t ageSec);
// 受信からの経過秒。再起動前のものは、保存したUTCと今のGPSのUTCから出す。分からなければ false
static bool age_of(const UiSnapshot* sn, bool restored, uint32_t ms, uint32_t utc, uint32_t nowMs, uint32_t* out) {
  if (!restored) { *out = (uint32_t)(nowMs - ms) / 1000u; return true; }
  if (!utc || !sn->hasUtc) return false;
  LogTime t; t.valid = true; t.year = sn->utc.year; t.month = sn->utc.month; t.day = sn->utc.day;
  t.hour = sn->utc.hour; t.minute = sn->utc.minute; t.second = sn->utc.second;
  uint32_t now = log_time_epoch(&t);
  if (!now) return false;
  *out = now >= utc ? now - utc : 0;
  return true;
}
static void age_or_dash(char* out, size_t n, const UiSnapshot* sn, bool restored, uint32_t ms, uint32_t utc, uint32_t nowMs) {
  uint32_t a;
  if (age_of(sn, restored, ms, utc, nowMs, &a)) age_text(out, n, a); else snprintf(out, n, "--");
}
// 60分以上受信がない (再起動前で経過時間が分からない局も古い扱い)
static bool wx_stale(const UiSnapshot* sn, const UiWxEntry* e, uint32_t nowMs) {
  uint32_t a;
  if (!age_of(sn, e->restored, e->lastMs, e->utc, nowMs, &a)) return true;
  return (uint64_t)a * 1000u >= WX_STALE_MS;
}
static void dist_text(char* out, size_t n, const UiWxEntry* e) {
  if (!e->hasPos) { snprintf(out, n, "%s", L("位置不明")); return; }
  if (!e->hasDist) { snprintf(out, n, "%s", L("距離不明")); return; }
  int b = (int)lround(e->bearingDeg) % 360;
  if (e->distKm < 10.0) snprintf(out, n, "%.1fkm %03d°", e->distKm, b);
  else snprintf(out, n, "%.0fkm %03d°", e->distKm, b);
}
static const char* dir16(uint16_t deg) {
  static const char* const d[16] = { "北", "北北東", "北東", "東北東", "東", "東南東", "南東", "南南東", "南", "南南西", "南西", "西南西", "西", "西北西", "北西", "北北西" };
  return L(d[((deg % 360) * 2 + 22) / 45 % 16]);
}
static void fmt_or(char* out, size_t n, bool has, const char* fmt, double v) {
  if (has) snprintf(out, n, fmt, v); else snprintf(out, n, "--");
}

static void render_wx_list(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  uint8_t ord[WX_STATION_COUNT]; int n = ui_wx_order(ui, ord);
  char title[32]; snprintf(title, sizeof(title), L("WX(%d局)"), n);
  header(v, ui, sn, title);
  if (n == 0) { add(v, UI_COL_DIM, "%s", L("気象データの受信なし")); add(v, UI_COL_DIM, "%s", L("(気象局の受信を待っています)")); return; }
  int shown = 0;
  for (int i = 0; i < n && shown < 4; i++, shown++) {
    const UiWxEntry* e = &ui->wx[ord[i]];
    bool stale = wx_stale(sn, e, nowMs);
    char d[32], a[24]; dist_text(d, sizeof(d), e); age_or_dash(a, sizeof(a), sn, e->restored, e->lastMs, e->utc, nowMs);
    add(v, stale ? UI_COL_DIM : UI_COL_HILITE, "%-9s %.5s %s %s", e->src, e->time, d, a);
    char t[16], h[8], p[16];
    fmt_or(t, sizeof(t), e->w.hasTemp, "%.1f°C", e->w.tempC);
    if (e->w.hasHumidity) snprintf(h, sizeof(h), "%u%%", (unsigned)e->w.humidityPct); else snprintf(h, sizeof(h), "--");
    fmt_or(p, sizeof(p), e->w.hasPressure, "%.1fhPa", e->w.pressureHpa);
    add(v, stale ? UI_COL_DIM : UI_COL_NORMAL, L(" %s  湿度%s  %s"), t, h, p);
    char wd[8], ws[12], g[12], r[12];
    if (e->w.hasWindDir) snprintf(wd, sizeof(wd), "%03u°", (unsigned)e->w.windDirDeg); else snprintf(wd, sizeof(wd), "---");
    fmt_or(ws, sizeof(ws), e->w.hasWindSpeed, "%.1fm/s", e->w.windSpeedMs);
    fmt_or(g, sizeof(g), e->w.hasGust, "%.1f", e->w.gustMs);
    fmt_or(r, sizeof(r), e->w.hasRain1h, "%.1fmm", e->w.rain1hMm);
    add(v, stale ? UI_COL_DIM : UI_COL_NORMAL, L(" 風%s %s 突風%s 雨1h %s"), wd, ws, g, r);
  }
}

static void render_wx_detail(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  uint8_t ord[WX_STATION_COUNT]; int n = ui_wx_order(ui, ord);
  int cur = -1;
  for (int i = 0; i < n; i++) if (!strcmp(ui->wx[ord[i]].src, ui->wxSel)) cur = i;
  char title[32];
  if (cur < 0) { snprintf(title, sizeof(title), "%s", L("WX詳細")); header(v, ui, sn, title); add(v, UI_COL_DIM, "%s", L("この局のデータはありません")); return; }
  snprintf(title, sizeof(title), L("WX詳細 %d/%d"), cur + 1, n);
  header(v, ui, sn, title);
  const UiWxEntry* e = &ui->wx[ord[cur]];
  bool stale = wx_stale(sn, e, nowMs);
  uint8_t c = stale ? UI_COL_DIM : UI_COL_NORMAL;
  char a[24]; age_or_dash(a, sizeof(a), sn, e->restored, e->lastMs, e->utc, nowMs);
  add(v, stale ? UI_COL_DIM : UI_COL_HILITE, "%s  %s UTC (%s)", e->src, e->time, a);
  char d[32]; dist_text(d, sizeof(d), e);
  if (e->hasPos) add(v, c, L("位置: %.4f%c %.4f%c  %s"), fabs(e->latDeg), e->latDeg >= 0 ? 'N' : 'S', fabs(e->lonDeg), e->lonDeg >= 0 ? 'E' : 'W', e->hasDist ? d : "");
  else add(v, c, "%s", L("位置: 不明"));
  char x[24], y[24], z[24];
  fmt_or(x, sizeof(x), e->w.hasTemp, "%.1f°C", e->w.tempC);
  if (e->w.hasHumidity) snprintf(y, sizeof(y), "%u%%", (unsigned)e->w.humidityPct); else snprintf(y, sizeof(y), "--");
  add(v, c, L("気温: %-9s 湿度: %s"), x, y);
  fmt_or(x, sizeof(x), e->w.hasPressure, "%.1fhPa", e->w.pressureHpa);
  add(v, c, L("気圧: %s"), x);
  if (e->w.hasWindDir) add(v, c, L("風向: %03u° (%s)"), (unsigned)e->w.windDirDeg, dir16(e->w.windDirDeg));
  else add(v, c, "%s", L("風向: --"));
  fmt_or(x, sizeof(x), e->w.hasWindSpeed, "%.1fm/s", e->w.windSpeedMs);
  fmt_or(y, sizeof(y), e->w.hasGust, "%.1fm/s", e->w.gustMs);
  add(v, c, L("風速: %-9s 突風: %s"), x, y);
  fmt_or(x, sizeof(x), e->w.hasRain1h, "%.1fmm", e->w.rain1hMm);
  fmt_or(y, sizeof(y), e->w.hasRain24h, "%.1fmm", e->w.rain24hMm);
  fmt_or(z, sizeof(z), e->w.hasRainMid, "%.1fmm", e->w.rainMidMm);
  add(v, c, L("雨量: 1時間 %s  24時間 %s"), x, y);
  add(v, c, L("      0時から %s"), z);
  if (e->w.hasLuminosity) add(v, c, L("日射: %uW/m2"), (unsigned)e->w.luminosityWm2);
  uint32_t ax; bool known = age_of(sn, e->restored, e->lastMs, e->utc, nowMs, &ax);
  add(v, c, L("受信 %u回%s"), (unsigned)e->count, !known ? L("  (再起動前のデータ)") : stale ? L("  (60分以上前のデータ)") : "");
  if (e->comment[0]) add(v, UI_COL_DIM, "%s", e->comment);
}

// ---- メッセージ画面 ----
static const char* msg_state_text(const MsgEntry* e) {
  switch (e->state) {
    case MSG_ST_SENDING:  return e->sends == 0 ? L("送信待ち") : L("Ack待ち");
    case MSG_ST_ACKED:    return L("Ack受信");
    case MSG_ST_REJECTED: return L("拒否(Rej)");
    case MSG_ST_FAILED:   return L("応答なし");
    default:              return "";
  }
}
static uint8_t msg_state_color(const MsgEntry* e) {
  switch (e->state) {
    case MSG_ST_ACKED:    return UI_COL_GOOD;
    case MSG_ST_REJECTED: case MSG_ST_FAILED: return UI_COL_BAD;
    default:              return UI_COL_WARN;
  }
}
// 本文を cols 桁ずつに分けて最大 maxLines 行
static void wrap_text(UiView* v, uint8_t color, const char* indent, const char* s, int cols, int maxLines) {
  size_t len = strlen(s), i = 0;
  for (int k = 0; k < maxLines && i < len; k++) {
    char buf[UI_TEXT_MAX]; size_t n = len - i; if (n > (size_t)cols) n = (size_t)cols;
    memcpy(buf, s + i, n); buf[n] = 0;
    add(v, color, "%s%s", indent, buf);
    i += n;
  }
}

static void render_msg_list(const UiState* ui, const UiSnapshot* sn, UiView* v) {
  uint8_t o[MSG_STORE_COUNT]; int n = ui->msgs ? msg_order(ui->msgs, o) : 0;
  char title[32]; snprintf(title, sizeof(title), L("メッセージ(%d)"), n);
  header(v, ui, sn, title);
  if (n == 0) { add(v, UI_COL_DIM, "%s", L("メッセージはありません")); return; }
  int shown = 0;
  for (int i = 0; i < n && shown < 6; i++, shown++) {
    const MsgEntry* e = &ui->msgs->e[o[i]];
    if (e->dir == MSG_DIR_OUT) {                       // 相手が JA1SJS なら撫子色 (送信の状態や未読の色があるときはそちらを優先)
      uint8_t sc = msg_state_color(e);
      add(v, (sc == UI_COL_NORMAL && sakura_call(e->to)) ? (uint8_t)UI_COL_SAKURA : sc, L("送 %.5s >%-9s %s"), e->time, e->to, msg_state_text(e));
    } else if (e->dir == MSG_DIR_BLN) {
      add(v, sakura_call(e->from) ? UI_COL_SAKURA : UI_COL_NORMAL, L("通 %.5s %-9s (%s)"), e->time, e->from, e->to);
    } else {
      add(v, e->unread ? UI_COL_HILITE : sakura_call(e->from) ? UI_COL_SAKURA : UI_COL_NORMAL, L("受 %.5s %-9s%s%s"), e->time, e->from, e->exact ? "" : L(" (別SSID宛)"), e->unread ? L(" *未読") : "");
    }
    add(v, e->unread ? UI_COL_NORMAL : UI_COL_DIM, "  %s", e->text);
  }
}

static void render_msg_detail(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  uint32_t seqs[MSG_STORE_COUNT + 1]; int n = msg_list(ui, seqs);
  int cur = 0;
  for (int i = 0; i < n; i++) if (seqs[i] == ui->msgSel) cur = i;
  int idx = (ui->msgs && ui->msgSel) ? msg_find_seq(ui->msgs, ui->msgSel) : -1;
  char title[32];
  if (idx < 0) {
    snprintf(title, sizeof(title), "%s", L("メッセージ 新規"));
    header(v, ui, sn, title);
    add(v, UI_COL_HILITE, "%s", L("新しいメッセージを作成"));
    add(v, UI_COL_NORMAL, "%s", "");
    add(v, UI_COL_NORMAL, "%s", L("宛先 → 本文 → 送信 の順に選びます"));
    add(v, UI_COL_DIM, "%s", L("キーボード(CardKB2)でも入力できます"));
    return;
  }
  snprintf(title, sizeof(title), L("メッセージ %d/%d"), cur, n - 1);
  header(v, ui, sn, title);
  const MsgEntry* e = &ui->msgs->e[idx];
  char a[24]; age_or_dash(a, sizeof(a), sn, e->restored, e->ms, e->utc, nowMs);
  const char* kind = e->dir == MSG_DIR_OUT ? L("送信") : e->dir == MSG_DIR_BLN ? L("一斉通知") : L("受信");
  add(v, UI_COL_HILITE, "%s  %s UTC (%s)", kind, e->time, a);
  add(v, UI_COL_NORMAL, "From: %s", e->dir == MSG_DIR_OUT ? L("(自局)") : e->from);
  add(v, UI_COL_NORMAL, "To:   %s%s", e->to, (e->dir == MSG_DIR_IN && !e->exact) ? L("  (別のSSID宛て)") : "");
  add(v, UI_COL_DIM, "%s", "");
  wrap_text(v, UI_COL_NORMAL, "", e->text, UI_COLS, 2);
  add(v, UI_COL_DIM, "%s", "");
  if (e->dir == MSG_DIR_OUT) {
    add(v, msg_state_color(e), L("状態: %s  (送信 %u/%u回)"), msg_state_text(e), (unsigned)e->sends, (unsigned)(MSG_RETRY_COUNT + 1));
    add(v, UI_COL_DIM, L("番号: %s"), e->msgNo);
  } else {
    if (e->msgNo[0]) add(v, UI_COL_DIM, L("番号: %s%s"), e->msgNo, e->exact ? L("  (Ackを返します)") : "");
    if (e->repeats) add(v, UI_COL_DIM, L("同じ内容を %u回 再受信"), (unsigned)e->repeats);
  }
}

static void render_compose(const UiState* ui, const UiSnapshot* sn, UiView* v) {
  const UiCompose* c = &ui->cmp;
  header(v, ui, sn, L("メッセージ作成"));
  if (c->step == UI_CMP_CONFIRM) {
    add(v, UI_COL_NORMAL, L("宛先: %s"), c->to);
    add(v, UI_COL_NORMAL, L("本文: (%u/%u文字)"), (unsigned)strlen(c->text), (unsigned)APRS_MSG_TEXT_MAX);
    char t[APRS_MSG_TEXT_MAX + 2]; snprintf(t, sizeof(t), "%s_", c->text);
    wrap_text(v, UI_COL_HILITE, "", t, UI_COLS, 2);
    add(v, UI_COL_NORMAL, "%s", "");
    add(v, UI_COL_DIM, "%s", L("Ackが来るまで送り直します"));
    return;
  }
  char items[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1];
  int n = ui_compose_items(ui, items);
  if (c->step == UI_CMP_TO) add(v, UI_COL_NORMAL, "%s", L("1/3 宛先を選ぶ"));
  else { add(v, UI_COL_NORMAL, L("宛先: %s"), c->to); add(v, UI_COL_NORMAL, "%s", L("2/3 本文を選ぶ")); }
  if (n == 0) add(v, UI_COL_DIM, "%s", c->step == UI_CMP_TO ? L("候補がありません (受信した局が出ます)") : L("定型文がありません"));
  int rows = 8;
  int top = c->sel - rows / 2; if (top > n - rows) top = n - rows; if (top < 0) top = 0;
  for (int i = top; i < n && i < top + rows; i++) {
    bool cur = (i == c->sel);
    bool typed = c->input[0] && i == 0;
    add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %s%s%s", cur ? ">" : " ", typed ? L("入力: ") : "", items[i], typed ? "_" : "");
  }
}

static void render_msg(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  if (ui->cmp.active) render_compose(ui, sn, v);
  else if (ui->msgDetail) render_msg_detail(ui, sn, v, nowMs);
  else render_msg_list(ui, sn, v);
}

// ---- レーダー画面 ----
// 自局を中心、北を上に、時間内に受信した局をシンボルのアイコンとコールサインで描く。描くのは ui_m5 (UiView の radar)
#define RADAR_CX   100      // 円の中心 (ドット)
#define RADAR_CY   112
#define RADAR_R    92       // 外側の円の半径
static void time_text(char* out, size_t n, uint16_t mins) {
  if (mins < 60) snprintf(out, n, L("%u分"), (unsigned)mins); else snprintf(out, n, L("%u時間"), (unsigned)(mins / 60));
}
static void radar_info(UiRadar* r, uint8_t color, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void radar_info(UiRadar* r, uint8_t color, const char* fmt, ...) {
  int i = 0; while (i < UI_RADAR_INFO && r->info[i][0]) i++;
  if (i >= UI_RADAR_INFO) return;
  va_list ap; va_start(ap, fmt); vsnprintf(r->info[i], sizeof(r->info[i]), fmt, ap); va_end(ap);
  if (!r->info[i][0]) snprintf(r->info[i], sizeof(r->info[i]), " ");   // 空行も1行と数える
  ui_fit(r->info[i], UI_RADAR_INFO_COLS);
  r->infoColor[i] = color;
}
static void render_radar(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  static const uint16_t KM[] = RADAR_RANGE_KM;
  const SettingsMgr* m = sn->settings;
  uint16_t mins = settings_radar_time_min(m), km = settings_radar_range_km(m);
  bool autoRange = (km == 0);
  // 時間内の局 (経過時間が分からない局は出さない)
  struct Cand { int idx; double d, b; uint32_t age; } c[RADAR_STATION_COUNT];
  int nc = 0;
  for (int i = 0; i < RADAR_STATION_COUNT; i++) {
    const UiStation* s = &ui->st[i];
    uint32_t a;
    if (!s->used || !age_of(sn, s->restored, s->ms, s->utc, nowMs, &a) || a > (uint32_t)mins * 60u) continue;
    c[nc].idx = i; c[nc].age = a; c[nc].d = 0; c[nc].b = 0;
    if (sn->hasPos) { c[nc].d = geo_distance_km(sn->latDeg, sn->lonDeg, s->latDeg, s->lonDeg); c[nc].b = geo_bearing_deg(sn->latDeg, sn->lonDeg, s->latDeg, s->lonDeg); }
    nc++;
  }
  for (int a = 1; a < nc; a++) {                    // 古い順 (新しい局を上に描く)
    Cand x = c[a]; int b = a - 1;
    while (b >= 0 && c[b].age < x.age) { c[b + 1] = c[b]; b--; }
    c[b + 1] = x;
  }
  if (autoRange) {                                  // 自動: いちばん遠い局が入る、いちばん小さい距離
    double far = 0; for (int i = 0; i < nc; i++) if (c[i].d > far) far = c[i].d;
    km = KM[RADAR_RANGE_AUTO - 1];
    for (int i = 0; i < RADAR_RANGE_AUTO; i++) if (far <= KM[i]) { km = KM[i]; break; }
    if (nc == 0) km = 10;
  }
  char tt[16]; time_text(tt, sizeof(tt), mins);
  char title[48]; snprintf(title, sizeof(title), L("レーダー %ukm/%s"), (unsigned)km, tt);
  header(v, ui, sn, title);
  UiRadar* r = &v->radar;
  r->on = true; r->hasOwn = sn->hasPos;
  r->cx = RADAR_CX; r->cy = RADAR_CY; r->r = RADAR_R;
  if (km >= 2) snprintf(r->ringLabel[0], sizeof(r->ringLabel[0]), "%u", (unsigned)(km / 2)); else snprintf(r->ringLabel[0], sizeof(r->ringLabel[0]), "0.5");
  snprintf(r->ringLabel[1], sizeof(r->ringLabel[1]), "%u", (unsigned)km);
  uint8_t mc = ui->radarMode ? UI_COL_HILITE : UI_COL_NORMAL;
  if (autoRange) radar_info(r, mc, L("自動 %ukm"), (unsigned)km); else radar_info(r, mc, L("距離 %ukm"), (unsigned)km);
  radar_info(r, mc, L("時間 %s"), tt);
  if (!sn->hasPos) { radar_info(r, UI_COL_WARN, "%s", L("自局位置なし")); return; }
  int out = 0, near = -1;
  for (int i = 0; i < nc; i++) {
    if (near < 0 || c[i].d < c[near].d) near = i;
    if (c[i].d > km) { out++; continue; }
    const UiStation* s = &ui->st[c[i].idx];
    double rr = c[i].d / km * RADAR_R, rad = c[i].b * M_PI / 180.0;
    UiRadarPt* p = &r->pts[r->count++];
    p->x = (int16_t)lround(RADAR_CX + rr * sin(rad));
    p->y = (int16_t)lround(RADAR_CY - rr * cos(rad));
    p->icon = (uint8_t)symbol_icon(s->symbol[0], s->symbol[1]);
    p->color = sakura_call(s->call) ? UI_COL_SAKURA : UI_COL_NORMAL;
    snprintf(p->label, sizeof(p->label), "%s", s->call);
  }
  if (nc == 0) { radar_info(r, UI_COL_DIM, "%s", L("受信局なし")); return; }
  radar_info(r, UI_COL_NORMAL, L("%d局"), (int)r->count);
  if (out) radar_info(r, UI_COL_DIM, L("範囲外 %d局"), out); else radar_info(r, UI_COL_DIM, "%s", "");
  const UiStation* ns = &ui->st[c[near].idx];
  radar_info(r, UI_COL_DIM, "%s", L("最寄り"));
  radar_info(r, sakura_call(ns->call) ? UI_COL_SAKURA : UI_COL_NORMAL, "%s", ns->call);
  radar_info(r, UI_COL_NORMAL, " %.1fkm %03d°", c[near].d, (int)lround(c[near].b) % 360);
}

static void render_wx(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  if (ui->wxDetail) render_wx_detail(ui, sn, v, nowMs); else render_wx_list(ui, sn, v, nowMs);
}

static void bar(UiView* v, uint8_t color, int pct, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void bar(UiView* v, uint8_t color, int pct, const char* fmt, ...) {
  if (v->count >= UI_MAX_LINES || v->barCount >= UI_MAX_BARS) return;
  UiLine* l = &v->lines[v->count];
  va_list ap; va_start(ap, fmt);
  vsnprintf(l->text, sizeof(l->text), fmt, ap);
  va_end(ap);
  ui_fit(l->text, 12);                      // 12桁。バーはその右側 (ui_m5 が描く)
  l->color = UI_COL_NORMAL;
  UiBar* b = &v->bars[v->barCount++];
  b->row = (int8_t)v->count; b->color = color;
  b->pct = (uint8_t)(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
  v->count++;
}

static int amp_pct(unsigned amp) { return (int)((amp * 100u + UI_LEVEL_FULL / 2) / UI_LEVEL_FULL); }

static void age_text(char* out, size_t n, uint32_t ageSec) {
  if (ageSec < 60) snprintf(out, n, L("%u秒前"), (unsigned)ageSec);
  else if (ageSec < 3600) snprintf(out, n, L("%u分%02u秒前"), (unsigned)(ageSec / 60), (unsigned)(ageSec % 60));
  else snprintf(out, n, L("%u時間前"), (unsigned)(ageSec / 3600));
}

static void render_rxstat(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  header(v, ui, sn, L("受信状態"));
  const AfskHwRxStatus* r = &sn->rx;
  bool resultShown = r->lastResult != AFSK_RESULT_NONE && (int32_t)(nowMs - r->lastResultMs) < (int32_t)RX_RESULT_SHOW_MS;
  if (ui->txBusy)                          add(v, UI_COL_BAD, "%s", L("送信中 (受信停止)"));
  else if (r->phase == AFSK_PHASE_DATA)    add(v, UI_COL_GOOD, L("データ受信中 %uバイト"), (unsigned)r->frameBytes);
  else if (resultShown && r->lastResult == AFSK_RESULT_OK)        add(v, UI_COL_GOOD, "%s", L("受信OK"));
  else if (resultShown && r->lastResult == AFSK_RESULT_FCS_ERROR) add(v, UI_COL_BAD, "%s", L("FCS不良"));
  else if (r->phase == AFSK_PHASE_SIGNAL)  add(v, UI_COL_WARN, "%s", L("信号あり (AFSKは未確認)"));
  else                                     add(v, UI_COL_DIM, "%s", L("無信号"));

  // 入力レベル: 振り切れは赤、受信中に小さすぎるときは黄
  bool active = r->phase != AFSK_PHASE_IDLE;
  uint8_t lc = r->clipped ? UI_COL_BAD : ((active && r->peak < UI_LEVEL_LOW) ? UI_COL_WARN : (active ? UI_COL_GOOD : UI_COL_DIM));
  bar(v, lc, amp_pct(r->peak), "%s", L("入力レベル"));
  bar(v, active ? UI_COL_GOOD : UI_COL_DIM, amp_pct(r->markAmp), "MARK  1200");
  bar(v, active ? UI_COL_GOOD : UI_COL_DIM, amp_pct(r->spaceAmp), "SPACE 2200");

  int dcAbs = r->dcOffset < 0 ? -r->dcOffset : r->dcOffset;
  if (r->clipped)                          add(v, UI_COL_BAD, "%s", L("入力が大きすぎます (音量を下げる)"));
  else if (dcAbs >= UI_DC_WARN)            add(v, UI_COL_WARN, "%s", L("ADC入力の直流が範囲外 (配線・バイアス確認)"));
  else if (active && r->peak < UI_LEVEL_LOW) add(v, UI_COL_WARN, "%s", L("入力が小さい (音量を上げる)"));
  else                                     add(v, UI_COL_DIM, "%s", "");
  if (r->markAmp >= 40 && r->spaceAmp >= 40 && r->markAvgAmp && r->spaceAvgAmp) {   // 両方のトーンが来ているとき。比は平均から出す
    double ratio = (double)r->markAvgAmp / (double)r->spaceAvgAmp;
    uint8_t rc = (ratio >= UI_RATIO_GOOD_LO && ratio <= UI_RATIO_GOOD_HI) ? UI_COL_GOOD
               : ((ratio >= UI_RATIO_LIMIT_LO && ratio <= UI_RATIO_LIMIT_HI) ? UI_COL_WARN : UI_COL_BAD);
    add(v, rc, L("強さの比 MARK/SPACE = %.1f"), ratio);
  } else add(v, UI_COL_DIM, "%s", L("強さの比 MARK/SPACE = ---"));

  add(v, UI_COL_NORMAL, L("受信OK %u  CRC不良 %u  雑音 %u"), (unsigned)sn->rxFramesOk, (unsigned)sn->rxFcsErrors, (unsigned)sn->rxFragments);
  add(v, UI_COL_NORMAL, L("フラグ検出 %u  途中破棄 %u"), (unsigned)sn->rxFlags, (unsigned)sn->rxDiscards);
  add(v, (sn->rxOverruns || sn->queueDropped) ? UI_COL_WARN : UI_COL_NORMAL, L("取りこぼし %u  待ち行列破棄 %u"), (unsigned)sn->rxOverruns, (unsigned)sn->queueDropped);
  if (ui->hasLastRx && ui->rxCount > 0) {
    const UiRxEntry* e = &ui->rx[(ui->rxHead + RX_LIST_COUNT - 1) % RX_LIST_COUNT];
    char a[24]; age_text(a, sizeof(a), (uint32_t)(nowMs - ui->lastRxMs) / 1000u);
    if (r->lastOkMarkAmp && r->lastOkSpaceAmp) {           // 最後に受けたパケットの MARK/SPACE の比 (調整トーンでなく実際の局の値)
      double lr = (double)r->lastOkMarkAmp / (double)r->lastOkSpaceAmp;
      uint8_t lc = (lr >= UI_RATIO_GOOD_LO && lr <= UI_RATIO_GOOD_HI) ? UI_COL_NORMAL
                 : ((lr >= UI_RATIO_LIMIT_LO && lr <= UI_RATIO_LIMIT_HI) ? UI_COL_WARN : UI_COL_BAD);
      add(v, lc, L("直近: %s %s 比%.1f"), e->src, a, lr);
    } else add(v, UI_COL_NORMAL, L("直近: %s %s"), e->src, a);
  } else add(v, UI_COL_DIM, "%s", L("直近: なし"));
}

static void render_txstat(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  header(v, ui, sn, L("送信・設定"));
  const SettingsMgr* m = sn->settings;
  const SymbolChoice* sym = settings_symbol(m);
  uint8_t ssid = settings_ssid(m);
  const char* dp = settings_digipath(m);
  if (ssid) add(v, UI_COL_NORMAL, L("局: %s-%u"), g_cfg.callsign, (unsigned)ssid);
  else add(v, UI_COL_NORMAL, L("局: %s"), g_cfg.callsign);
  if (ui->txBusy)                add(v, UI_COL_BAD, "%s", sn->ptt ? L("PTT ON (送信中)") : "PTT OFF");
  else if (sn->pttTimeouts > 0)  add(v, UI_COL_WARN, L("PTT時間超過で中止 %u回"), (unsigned)sn->pttTimeouts);
  add(v, UI_COL_NORMAL, L("シンボル: %c%c %s"), sym->table, sym->code, L(sym->name));
  add(v, UI_COL_NORMAL, L("デジパス: %s"), dp[0] ? dp : L("なし"));
  add(v, UI_COL_NORMAL, L("曖昧化: Lv%u"), (unsigned)m->cur.ambiguity);
  add(v, UI_COL_NORMAL, L("進行方向・速度: %s  高度: %s"), onoff(m->cur.sendCourseSpeed), onoff(m->cur.sendAltitude));
  { char at[40]; uint16_t mi = auto_tx_interval_min(m->cur.autoTx); uint32_t r = (sn->autoRemainMs + 999) / 1000;
    bool flex = auto_tx_is_flex(m->cur.autoTx), flexOk = flex && app_config_flex_ok(&g_cfg);
    if (mi) snprintf(at, sizeof(at), L("%u分 (次 %u:%02u)"), (unsigned)mi, (unsigned)(r / 60), (unsigned)(r % 60));
    else if (flexOk) snprintf(at, sizeof(at), L("フレックス (次 %u:%02u)"), (unsigned)(r / 60), (unsigned)(r % 60));
    else if (flex) snprintf(at, sizeof(at), "%s", L("フレックス (設定なし)"));
    else snprintf(at, sizeof(at), "%s", m->cur.autoTx == AUTO_TX_ACK_ONLY ? L("Ackのみ") : L("オフ"));
    add(v, (mi || flexOk) ? UI_COL_HILITE : (flex ? UI_COL_WARN : UI_COL_NORMAL), L("自動送信: %s"), sn->autoWaiting ? L("他局の送信終了待ち") : at); }
  if (sn->hasLastTx) { char a[24]; age_text(a, sizeof(a), (uint32_t)(nowMs - sn->lastTxMs) / 1000u); add(v, UI_COL_NORMAL, L("送信 %u回  最後 %s"), (unsigned)sn->txCount, a); }
  else add(v, UI_COL_NORMAL, L("送信 %u回"), (unsigned)sn->txCount);
  add(v, settings_dirty(m) ? UI_COL_WARN : UI_COL_NORMAL, L("設定保存: %s"), settings_dirty(m) ? L("待ち") : L("済み"));
  uint32_t s = sn->uptimeMs / 1000;
  add(v, UI_COL_DIM, L("稼働 %u:%02u:%02u"), (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

// __DATE__ ("Oct 18 2026", 日が1桁なら "Oct  8 2026") を "2026-10-18" にする。形が違えばそのまま
void ui_build_date(char* out, size_t n, const char* date) {
  static const char* const mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int m = 0;
  if (date && strlen(date) == 11) for (int i = 0; i < 12; i++) if (!strncmp(date, mon + i * 3, 3)) { m = i + 1; break; }
  if (!m) { snprintf(out, n, "%s", date ? date : ""); return; }
  int day = atoi(date + 4), year = atoi(date + 7);
  snprintf(out, n, "%04d-%02d-%02d", year, m, day);
}

static void render_menu(const UiState* ui, const UiSnapshot* sn, UiView* v) {
  static const char* const names[UI_MENU_COUNT] = { "ビーコン自動送信", "位置情報の曖昧化", "デジパス", "シンボル", "SSID", "進行方向・速度の送信", "高度の送信", "モニター音量", "受信音の音量", "画面の明るさ", "位置の取得元", "固定位置の選択", "調整トーン (PTT送信)", "記録の消去", "言語 / Language", "バージョン情報" };
  const SettingsMgr* m = sn->settings;
  int pos = 0, total = 0;                    // 表示される項目の中での番号 (固定位置の選択は取得元=固定のときだけ数える)
  for (int i = 0; i < UI_MENU_COUNT; i++) if (menu_visible(i, m)) { total++; if (i <= ui->menuItem) pos++; }
  char title[48]; snprintf(title, sizeof(title), L("設定 %d/%d"), pos, total);
  header(v, ui, sn, title);
  add(v, UI_COL_HILITE, "%s", L(names[ui->menuItem]));
  add(v, UI_COL_NORMAL, "%s", "");
  switch (ui->menuItem) {
    case UI_MENU_AUTOTX: {                   // 11個。入りきらないので、選択中の前後を出す
      int rows = UI_MAX_LINES - v->count;
      int top = (int)m->cur.autoTx - rows / 2; if (top > AUTO_TX_CHOICES - rows) top = AUTO_TX_CHOICES - rows; if (top < 0) top = 0;
      for (int i = top; i < top + rows && i < AUTO_TX_CHOICES; i++) {
        bool cur = (i == m->cur.autoTx);
        if (i == AUTO_TX_OFF) add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, L("%s オフ"), cur ? ">" : " ");
        else if (i == AUTO_TX_ACK_ONLY) add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, L("%s Ackのみ (メッセージ受信時だけ)"), cur ? ">" : " ");
        else if (auto_tx_is_flex((uint8_t)i)) {
          if (app_config_flex_ok(&g_cfg)) add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, L("%s フレックス (速度と向きで変える)"), cur ? ">" : " ");
          else add(v, cur ? UI_COL_WARN : UI_COL_DIM, L("%s フレックス (config.txt に設定なし)"), cur ? ">" : " ");
        }
        else add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, L("%s %u分ごと"), cur ? ">" : " ", (unsigned)auto_tx_interval_min((uint8_t)i));
      }
      break; }
    case UI_MENU_AMBIGUITY: {
      static const char* const d[] = { "オフ", "約0.2km", "約2km", "約19km", "約111km" };
      for (int i = 0; i <= MAX_AMBIGUITY_LEVEL && i < 5; i++) {
        bool cur = (i == m->cur.ambiguity);
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s Lv%d  %s", cur ? ">" : " ", i, L(d[i]));
      }
      break; }
    case UI_MENU_DIGIPATH:
      for (int i = 0; i < DIGIPATH_CHOICE_COUNT; i++) {
        bool cur = (i == m->cur.digipathIndex);
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %s", cur ? ">" : " ", g_cfg.digipath[i][0] ? g_cfg.digipath[i] : L("なし"));
      }
      break;
    case UI_MENU_SYMBOL: {                   // 21個。入りきらないので、選択中の前後を出す (3桁目からアイコン)
      int cur = m->cur.symbolIndex, rows = UI_MAX_LINES - v->count;
      int top = cur - rows / 2; if (top > SYMBOL_CHOICE_COUNT - rows) top = SYMBOL_CHOICE_COUNT - rows; if (top < 0) top = 0;
      for (int i = top; i < top + rows && i < SYMBOL_CHOICE_COUNT; i++) {
        const SymbolChoice* c = &SYMBOL_CHOICES[i];
        add(v, i == cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s    %c%c  %s", i == cur ? ">" : " ", c->table, c->code, L(c->name));
        set_icon(v, c->icon, 2, false);
      }
      break; }
    case UI_MENU_SSID: {                     // 0〜15。選択中の前後を出す
      int cur = m->cur.ssid, rows = UI_MAX_LINES - v->count;
      int top = cur - rows / 2; if (top > 16 - rows) top = 16 - rows; if (top < 0) top = 0;
      for (int i = top; i < top + rows && i < 16; i++)
        add(v, i == cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %2d  %s", i == cur ? ">" : " ", i, L(ssid_use((uint8_t)i)));
      break; }
    case UI_MENU_COURSE_SPEED:
    case UI_MENU_ALTITUDE: {
      bool on = (ui->menuItem == UI_MENU_COURSE_SPEED) ? m->cur.sendCourseSpeed : m->cur.sendAltitude;
      add(v, !on ? UI_COL_HILITE : UI_COL_NORMAL, L("%s オフ"), !on ? ">" : " ");
      add(v, on ? UI_COL_HILITE : UI_COL_NORMAL, L("%s オン"), on ? ">" : " ");
      break; }
    case UI_MENU_MONITOR:
      for (int i = 0; i <= MONITOR_LEVEL_MAX; i++) {
        bool cur = (i == m->cur.monitorLevel);
        const char* d = (i == 0) ? L(" ミュート") : (i == MONITOR_LEVEL_MAX ? L(" 最大") : "");
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %d%s", cur ? ">" : " ", i, d);
      }
      break;
    case UI_MENU_CHIME:                      // 受信音: パケット・折り返し・Ack・メッセージで音を変える (選ぶと試しに鳴らす)
      for (int i = 0; i <= MONITOR_LEVEL_MAX; i++) {
        bool cur = (i == m->cur.chimeLevel);
        const char* d = (i == 0) ? L(" オフ (鳴らさない)") : (i == MONITOR_LEVEL_MAX ? L(" 最大") : "");
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %d%s", cur ? ">" : " ", i, d);
      }
      break;
    case UI_MENU_BRIGHTNESS:
      for (int i = 0; i < BRIGHTNESS_LEVEL_COUNT; i++) {
        bool cur = (i == m->cur.brightness);
        const char* d = (i == 0) ? L(" 夜間") : (i == BRIGHTNESS_LEVEL_COUNT - 1) ? L(" 直射日光の下") : (i == DEFAULT_BRIGHTNESS_LEVEL) ? L(" 日中") : "";
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %d%s", cur ? ">" : " ", i + 1, d);
      }
      break;
    case UI_MENU_LANGUAGE:
      add(v, m->cur.language == UI_LANG_JA ? UI_COL_HILITE : UI_COL_NORMAL, "%s 日本語", m->cur.language == UI_LANG_JA ? ">" : " ");
      add(v, m->cur.language == UI_LANG_EN ? UI_COL_HILITE : UI_COL_NORMAL, "%s English", m->cur.language == UI_LANG_EN ? ">" : " ");
      break;
    case UI_MENU_POSSRC: {
      static const char* const d[] = { "GPS (現在の測位)", "最後の位置 (保存済み)", "固定位置" };
      for (int i = 0; i <= POSITION_SOURCE_FIXED; i++) {
        bool cur = (i == m->cur.positionSource);
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %s", cur ? ">" : " ", L(d[i]));
      }
      add(v, UI_COL_NORMAL, "%s", "");
      if (m->cur.positionSource == POSITION_SOURCE_LAST) {
        if (sn->lastPosValid) { char t[UI_TEXT_MAX]; snprintf(t, sizeof(t), L("保存: %.5f%c %.5f%c"), fabs(sn->lastPosLat), sn->lastPosLat >= 0 ? 'N' : 'S', fabs(sn->lastPosLon), sn->lastPosLon >= 0 ? 'E' : 'W'); add(v, UI_COL_NORMAL, "%s", t); }
        else add(v, UI_COL_WARN, "%s", L("保存された位置はまだありません"));
      } else if (m->cur.positionSource == POSITION_SOURCE_FIXED) add(v, UI_COL_DIM, "%s", L("次の項目で固定位置を選びます"));
      else add(v, UI_COL_DIM, "%s", L("自動では切り替わりません"));
      if (fixed_position_count_set() == 0) add(v, UI_COL_DIM, "%s", L("固定位置は config.txt に設定します"));
      break; }
    case UI_MENU_FIXED:
      for (int i = 0; i < FIXED_POSITION_COUNT; i++) {
        if (!fixed_position_set(i)) continue;
        const FixedPosition* f = &g_cfg.fixed[i];
        bool cur = (i == m->cur.fixedIndex);
        add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %-9.9s%7.3f%c %8.3f%c", cur ? ">" : " ", f->name,
            fabs(f->latDeg), f->latDeg >= 0 ? 'N' : 'S', fabs(f->lonDeg), f->lonDeg >= 0 ? 'E' : 'W');
      }
      if (fixed_position_count_set() == 0) add(v, UI_COL_WARN, "%s", L("設定済みの固定位置がありません"));
      break;
    case UI_MENU_CLEAR:
      add(v, UI_COL_NORMAL, "%s", L("受信一覧・WX局・メッセージ・レーダーの局を消します"));
      add(v, UI_COL_DIM, "%s", L("消す前に確認します"));
      break;
    case UI_MENU_CALIB:
      add(v, UI_COL_WARN, "%s", L("PTTを入れて連続トーンを出します"));
      add(v, UI_COL_WARN, "%s", L("無線機はダミーロードにつなぐこと"));
      break;
    case UI_MENU_VERSION: {
      char d[16]; ui_build_date(d, sizeof(d), FW_BUILD_DATE);
      add(v, UI_COL_GOOD,   "%s  ver %s", FW_NAME, FW_VERSION);
      add(v, UI_COL_DIM,    "%s", FW_COPYRIGHT);
      add(v, UI_COL_NORMAL, L("ビルド: %s %s"), d, FW_BUILD_TIME);
      add(v, UI_COL_NORMAL, L("対応基板: %s"), HW_INTERFACE_REV);
      add(v, UI_COL_NORMAL, L("コールサイン: %s"), g_cfg.callsign);
      add(v, UI_COL_DIM,    "SDK: %s", sn->sdkVersion[0] ? sn->sdkVersion : "---");
      add(v, UI_COL_DIM,    L("チップ rev%u  空きメモリ %luB"), (unsigned)sn->chipRev, (unsigned long)sn->freeHeap);
      add(v, sn->sdOk ? UI_COL_DIM : UI_COL_WARN, L("SD: %s  書き込み待ち %luB"), sn->sdOk ? L("正常") : L("エラー/未挿入"), (unsigned long)sn->sdPendingBytes);
      { const CfgStatus* cs = &g_cfgStatus;                // 設定ファイルの読み込みの結果
        if (cs->source == CFG_SRC_FILE && cs->errorCount) add(v, UI_COL_WARN, L("設定: config.txt  エラー%u件"), (unsigned)cs->errorCount);
        else if (cs->source == CFG_SRC_FILE) add(v, UI_COL_DIM, "%s", L("設定: config.txt"));
        else if (cs->source == CFG_SRC_CACHE) add(v, UI_COL_WARN, "%s", L("設定: 本体の控え (SDなし)"));
        else add(v, UI_COL_WARN, "%s", L("設定: 初期値 (config.txt なし)"));
        if (cs->errorCount) add(v, UI_COL_WARN, L("  %u行目: %s %s"), (unsigned)cs->errors[0].line, L(app_config_error_text(cs->errors[0].code)), cs->errors[0].key); }
      break; }
  }
}

static void render_calib(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  header(v, ui, sn, L("調整トーン"));
  if (ui->calibOn) {
    uint32_t sec = (uint32_t)(nowMs - ui->calibSinceMs) / 1000u;
    add(v, UI_COL_BAD, L("%s  %u秒"), sn->ptt ? L("PTT ON  送信中") : L("PTT 準備/終了処理中"), (unsigned)sec);
    add(v, UI_COL_BAD, "%s", L("どのボタンでも停止"));
  } else {
    add(v, UI_COL_GOOD, "%s", L("停止中 (PTTオフ)"));
    add(v, UI_COL_NORMAL, "%s", "");
  }
  static const char* const names[] = { "", "1200Hz 連続 (MARK)", "2200Hz 連続 (SPACE)", "1200Hz⇔2200Hz 交互 (0.5秒ごと)" };
  for (int i = AFSK_TONE_MARK; i <= AFSK_TONE_ALT; i++) {
    bool cur = (i == ui->calibMode);
    add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, "%s %s", cur ? ">" : " ", L(names[i]));
  }
  add(v, UI_COL_NORMAL, "%s", "");
  add(v, UI_COL_DIM, L("振幅 MARK %d / SPACE %d (config.txt)"), (int)g_cfg.txAmpMark, (int)g_cfg.txAmpSpace);
  add(v, UI_COL_DIM, L("最長 %u秒で自動停止"), (unsigned)(CALIB_MAX_ON_MS / 1000u));
  add(v, UI_COL_WARN, "%s", L("ダミーロード・最小電力で使うこと"));
}

static void render_clear(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  header(v, ui, sn, L("記録の消去"));
  for (int i = 0; i < UI_CLEAR_COUNT; i++) {
    bool cur = (i == ui->clearTarget);
    add(v, cur ? UI_COL_HILITE : UI_COL_NORMAL, L("%s %s (%d件)"), cur ? ">" : " ", L(CLEAR_NAMES[i]), clear_count(ui, (uint8_t)i));
  }
  add(v, UI_COL_NORMAL, "%s", "");
  if (ui->clearConfirm) {
    add(v, UI_COL_BAD, L("「%s」を消去します"), L(CLEAR_NAMES[ui->clearTarget]));
    add(v, UI_COL_BAD, "%s", L("元に戻せません。よければ「消去」"));
  } else {
    add(v, UI_COL_DIM, "%s", L("消すものを選んで「選択」"));
    add(v, UI_COL_DIM, "%s", L("送り直し中のメッセージも止まります"));
  }
  if (ui->msgUntilMs && (int32_t)(ui->msgUntilMs - nowMs) > 0) add(v, UI_COL_GOOD, "%s", ui->msg);
}

void ui_render(const UiState* ui, const UiSnapshot* sn, UiView* v, uint32_t nowMs) {
  memset(v, 0, sizeof(*v));
  v->screen = ui->screen;
  switch (ui->screen) {
    case UI_SCREEN_OWN:    render_own(ui, sn, v, nowMs); break;
    case UI_SCREEN_RX:     render_rx(ui, sn, v, nowMs); break;
    case UI_SCREEN_RADAR:  render_radar(ui, sn, v, nowMs); break;
    case UI_SCREEN_WX:     render_wx(ui, sn, v, nowMs); break;
    case UI_SCREEN_MSG:    render_msg(ui, sn, v, nowMs); break;
    case UI_SCREEN_RXSTAT: render_rxstat(ui, sn, v, nowMs); break;
    case UI_SCREEN_TXSTAT: render_txstat(ui, sn, v, nowMs); break;
    case UI_SCREEN_CALIB:  render_calib(ui, sn, v, nowMs); break;
    case UI_SCREEN_CLEAR:  render_clear(ui, sn, v, nowMs); break;
    default:               render_menu(ui, sn, v); break;
  }
  make_icons(ui, sn, &v->icons);
  // ボタン表示: 短押し / 長押し。空は機能なし (「-」で表示)
  const char* ks[3] = { "", "", "" };
  const char* kl[3] = { "", "", "" };
  if (ui->calibOn) { ks[0] = ks[1] = ks[2] = K("停止"); }
  else if (ui->txBusy) { }
  else if (ui->screen == UI_SCREEN_CALIB) { ks[0] = K("前"); ks[1] = K("開始"); ks[2] = K("次"); kl[1] = K("戻る"); }
  else if (ui->screen == UI_SCREEN_CLEAR) {
    if (ui->clearConfirm) { ks[0] = K("やめる"); ks[1] = K("消去"); ks[2] = K("やめる"); }
    else { ks[0] = K("前"); ks[1] = K("選択"); ks[2] = K("次"); }
    kl[1] = K("戻る");
  }
  else if (ui->screen == UI_SCREEN_MENU) {
    ks[0] = K("前"); ks[2] = K("次");
    ks[1] = ui->menuItem == UI_MENU_VERSION ? "" : (ui->menuItem == UI_MENU_CALIB || ui->menuItem == UI_MENU_CLEAR) ? K("開く") : K("変更");
    kl[1] = kl[2] = K("閉じる");
  }
  else if (ui->screen == UI_SCREEN_MSG && ui->cmp.active) {
    bool conf = ui->cmp.step == UI_CMP_CONFIRM;
    ks[0] = conf ? "" : K("前"); ks[1] = conf ? K("送信") : K("決定"); ks[2] = conf ? "" : K("次");
    kl[1] = K("戻る");
  }
  else {
    ks[0] = K("前"); ks[2] = K("次"); kl[0] = K("送信"); kl[2] = K("設定");
    if (ui->screen == UI_SCREEN_MSG && ui->msgDetail) {
      int idx = (ui->msgs && ui->msgSel) ? msg_find_seq(ui->msgs, ui->msgSel) : -1;
      ks[1] = idx < 0 ? K("作成") : ui->msgs->e[idx].dir == MSG_DIR_OUT ? K("再送") : K("返信");
      kl[1] = K("一覧");
    } else if (ui->screen == UI_SCREEN_RX && ui->rxDetail) {
      int cur = -1;
      for (int i = 0; i < (int)ui->rxCount; i++) if (ui->rx[rx_idx(ui, i)].seq == ui->rxSel) cur = i;
      bool own = cur >= 0 && ui->myCall[0] && msg_call_equal(ui->rx[rx_idx(ui, cur)].src, ui->myCall);
      ks[1] = (cur >= 0 && !own) ? K("作成") : ""; kl[1] = K("一覧");
    } else if (ui->screen == UI_SCREEN_RX) {
      if (ui->rxCount > 0) ks[1] = K("詳細");
    } else if (ui->screen == UI_SCREEN_RADAR) {
      if (ui->radarMode) { ks[0] = K("距離"); ks[1] = K("終了"); ks[2] = K("時間"); }
      else ks[1] = K("切替");
    } else if (ui->screen == UI_SCREEN_WX && ui->wxDetail) {
      ks[0] = K("前局"); ks[2] = K("次局"); kl[1] = K("一覧");
    } else if (ui->screen == UI_SCREEN_WX) {
      uint8_t o[WX_STATION_COUNT]; if (ui_wx_order(ui, o) > 0) ks[1] = K("詳細");
    } else if (ui->screen == UI_SCREEN_MSG) {
      uint8_t o[MSG_STORE_COUNT]; ks[1] = (ui->msgs && msg_order(ui->msgs, o) > 0) ? K("詳細") : K("作成");
    }
  }
  for (int i = 0; i < 3; i++) {
    snprintf(v->keyShort[i], UI_SOFTKEY_MAX, "%s", ks[i]);
    snprintf(v->keyLong[i], UI_SOFTKEY_MAX, "%s", kl[i]);
  }
}
