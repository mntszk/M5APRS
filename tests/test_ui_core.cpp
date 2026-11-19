// test_ui_core.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "ui_core.h"
#include "tx_compose.h"
#include "packet_build.h"
#include "packet_parse.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { checks++; double _a = (a), _b = (b); if (!(fabs(_a - _b) <= (tol))) { failures++; printf("FAIL line %d: %s=%.7f vs %.7f\n", __LINE__, #a, _a, _b); } } while (0)

static std::string nm(const std::string& body) {
  uint8_t x = 0; for (size_t i = 0; i < body.size(); i++) x ^= (uint8_t)body[i];
  char h[8]; snprintf(h, sizeof(h), "*%02X", x);
  return "$" + body + h + "\r\n";
}
static void feed(GpsState& g, const std::string& s, uint32_t now) { gps_feed(&g, s.data(), s.size(), now); }
static void fix(GpsState& g, uint32_t now, const char* speed = "10.0", const char* crs = "90.0") {
  feed(g, nm(std::string("GPRMC,123519,A,3540.0000,N,13945.0000,E,") + speed + "," + crs + ",091026,,,A"), now);
  feed(g, nm("GPGGA,123519,3540.0000,N,13945.0000,E,1,08,0.9,123.4,M,46.9,M,,"), now);
}

// GPSの位置から送信内容を作る (取得元=GPS)
static TxComposeResult compose(const SettingsMgr* sm, const GpsState* g, uint32_t now, TxComposed* c) {
  static LastPosMgr lp; last_pos_init(&lp, NULL);
  OwnPos o; position_resolve(sm, g, &lp, now, &o);
  return tx_compose(sm, &o, c);
}

// ボタン操作の補助: 押す→離す
struct Rig {
  UiState ui; SettingsMgr sm; uint32_t t;
  Rig() { ui_init(&ui); settings_init(&sm, NULL); t = 10000; }
  UiAction press(int b, uint32_t holdMs) {
    bool d[3] = { b == 0, b == 1, b == 2 };
    UiAction a = ui_buttons(&ui, &sm, d[0], d[1], d[2], t);
    UiAction r = a;
    for (uint32_t k = 20; k <= holdMs; k += 20) { t += 20; a = ui_buttons(&ui, &sm, d[0], d[1], d[2], t); if (a) r = a; }
    t += 20; a = ui_buttons(&ui, &sm, false, false, false, t); if (a) r = a;
    t += 100; ui_buttons(&ui, &sm, false, false, false, t);
    return r;
  }
  UiAction shortp(int b) { return press(b, 100); }
  UiAction longp(int b)  { return press(b, LONG_PRESS_MS + 200); }
};

static UiSnapshot snap_of(const SettingsMgr* sm) {
  UiSnapshot s; memset(&s, 0, sizeof(s)); s.settings = sm; s.gps = GPS_FIX; s.sdOk = true; return s;
}
static bool has_line(const UiView& v, const char* sub) {
  for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, sub)) return true;
  return false;
}
static int line_cols(const char* s) { char t[UI_TEXT_MAX]; snprintf(t, sizeof(t), "%s", s); return ui_fit(t, 100000); }

int main() {
  test_cfg_setup();
  // ---- ui_fit ----
  { char s[64]; snprintf(s, sizeof(s), "hello"); CHECK(ui_fit(s, 10) == 5 && !strcmp(s, "hello"));
    snprintf(s, sizeof(s), "hello world"); CHECK(ui_fit(s, 5) == 5 && !strcmp(s, "hello"));
    snprintf(s, sizeof(s), "日本語ABC"); CHECK(ui_fit(s, 5) == 4 && !strcmp(s, "日本"));       // 全角は2桁、3桁目に入らない
    snprintf(s, sizeof(s), "日本語ABC"); CHECK(ui_fit(s, 7) == 7 && !strcmp(s, "日本語A"));
    snprintf(s, sizeof(s), "a\tb\x01" "c"); ui_fit(s, 10); CHECK(!strcmp(s, "a?b?c"));
    snprintf(s, sizeof(s), "ab\xE3\x81"); ui_fit(s, 10); CHECK(!strcmp(s, "ab??"));              // 途切れたUTF-8
    snprintf(s, sizeof(s), "x\xFFy"); ui_fit(s, 10); CHECK(!strcmp(s, "x?y"));
    snprintf(s, sizeof(s), "\xC2\xB0" "C"); CHECK(ui_fit(s, 10) == 2);                           // °
    snprintf(s, sizeof(s), "%s", ""); CHECK(ui_fit(s, 10) == 0 && s[0] == 0);
    snprintf(s, sizeof(s), "abc"); CHECK(ui_fit(s, 0) == 0 && s[0] == 0); }

  // ---- tx_compose ----
  { GpsState g; gps_init(&g); SettingsMgr sm; settings_init(&sm, NULL);
    TxComposed* c = new TxComposed;
    CHECK(compose(&sm, &g, 1000, c) == TXC_NO_POSITION);
    fix(g, 1000);
    CHECK(compose(&sm, &g, 1500, c) == TXC_OK);
    CHECK(c->frameLen > 20 && !c->usedStaleFix);
    CHECK(ax25_fcs(c->frame, c->frameLen - 2) == (uint16_t)(c->frame[c->frameLen - 2] | (c->frame[c->frameLen - 1] << 8)));
    AprsPacket p; CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p));
    CHECK(p.hasPos); NEAR(p.latDeg, 35.6667, 0.001); NEAR(p.lonDeg, 139.75, 0.001);
    CHECK(p.symTable == '/' && p.symCode == '>');            // 既定プリセット Car
    CHECK(!strcmp(p.src, MY_CALLSIGN "-9") && !strcmp(p.via, "WIDE1-1"));
    CHECK(!p.hasSpeed && !p.hasAltitude);                    // 既定はオフ
    CHECK(!strcmp(c->header, MY_CALLSIGN "-9>APM5AP,WIDE1-1:"));
    CHECK(c->info[0] == APRS_DTI && APRS_DTI == '=' && strstr(c->info, APRS_COMMENT));
    // 進行方向・速度、高度
    settings_set_course_speed(&sm, true); settings_set_altitude(&sm, true);
    CHECK(compose(&sm, &g, 1500, c) == TXC_OK);
    CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p));
    CHECK(p.hasSpeed && p.speedKnots == 10 && p.hasCourse && p.courseDeg == 90);
    CHECK(p.hasAltitude); NEAR(p.altitudeM, 123.4, 0.4);
    // 速度あり・進行方向なし → 0 (不明)
    GpsState g2; gps_init(&g2);
    feed(g2, nm("GPRMC,123519,A,3540.0000,N,13945.0000,E,0.3,,091026,,,A"), 1000);
    CHECK(compose(&sm, &g2, 1500, c) == TXC_OK);
    CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p) && p.hasSpeed == true && !p.hasCourse);
    // 曖昧化・シンボル・SSID・デジパス
    settings_set_ambiguity(&sm, 2); settings_set_symbol(&sm, (uint8_t)symbol_find('/', '-')); settings_set_ssid(&sm, 0); settings_set_digipath(&sm, 0);
    CHECK(compose(&sm, &g, 1500, c) == TXC_OK);
    CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p));
    CHECK(p.ambiguity == 2 && p.symCode == '-' && !strcmp(p.src, MY_CALLSIGN) && p.via[0] == 0);
    CHECK(!strcmp(c->header, MY_CALLSIGN ">APM5AP:"));
    // 古い位置: 5分以内は使える (測位が途切れても)、超えたら送れない
    settings_set_digipath(&sm, 1);
    CHECK(compose(&sm, &g, 1000 + 200000, c) == TXC_OK && c->usedStaleFix && c->posAgeSec >= 199);
    CHECK(compose(&sm, &g, 1000 + 301000, c) == TXC_NO_POSITION);
    // 情報フィールドをそのまま (メッセージ用)
    CHECK(tx_compose_info(&sm, ":N0CALL-7 :Hello{1", c) == TXC_OK && aprs_parse_frame(c->frame, c->frameLen - 2, &p));
    CHECK(p.msg.present && !strcmp(p.msg.addressee, "N0CALL-7") && !strcmp(p.src, MY_CALLSIGN) && !strcmp(c->info, ":N0CALL-7 :Hello{1"));
    CHECK(ax25_fcs(c->frame, c->frameLen - 2) == (uint16_t)(c->frame[c->frameLen - 2] | (c->frame[c->frameLen - 1] << 8)));
    CHECK(tx_compose_info(&sm, "", c) == TXC_BUILD_ERROR);
    delete c; }

  // ---- ボタン: 通常画面 (短押し A=前 C=次、長押し A=送信 C=設定) ----
  { Rig r;
    CHECK(r.ui.screen == UI_SCREEN_OWN);
    CHECK(r.shortp(2) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_RX);        // C短 = 次の画面
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_RADAR);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_MSG);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_WX);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_RXSTAT);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_TXSTAT);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_OWN);                 // 一周 (7画面)
    CHECK(r.shortp(0) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_TXSTAT);     // A短 = 前の画面 (送信しない)
    r.shortp(0); CHECK(r.ui.screen == UI_SCREEN_RXSTAT);
    r.shortp(0); CHECK(r.ui.screen == UI_SCREEN_WX);
    CHECK(r.shortp(1) == UI_ACT_NONE && !r.ui.wxDetail);                     // WX: 局がなければ詳細は開かない
    r.shortp(0); CHECK(r.ui.screen == UI_SCREEN_MSG);
    r.shortp(0); CHECK(r.ui.screen == UI_SCREEN_RADAR);
    r.shortp(0); CHECK(r.ui.screen == UI_SCREEN_RX);
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_RX);        // B短: 詳細のない画面では何もしない
    CHECK(r.longp(1) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_RX);         // B長: 通常画面では戻る先がない
    // A長 = 送信。画面は変わらない
    CHECK(r.longp(0) == UI_ACT_SEND && r.ui.screen == UI_SCREEN_RX);
    // 送信は長押しの時間に達した時点で1回だけ (離すのを待たない、押し続けても繰り返さない)
    { Rig q; uint32_t t = q.t; int sends = 0;
      for (uint32_t k = 0; k <= 4000; k += 20) { if (ui_buttons(&q.ui, &q.sm, true, false, false, t + k) == UI_ACT_SEND) { sends++; CHECK(k >= LONG_PRESS_MS && k < LONG_PRESS_MS + 40); } }
      CHECK(ui_buttons(&q.ui, &q.sm, false, false, false, t + 4100) == UI_ACT_NONE && sends == 1 && q.ui.screen == UI_SCREEN_OWN); }
    // C長 = 設定メニュー。戻る先を覚える
    UiAction a = r.longp(2); CHECK(a == UI_ACT_NONE && r.ui.screen == UI_SCREEN_MENU && r.ui.returnScreen == UI_SCREEN_RX);
    CHECK(r.ui.menuItem == 0);
    // 長押し時間に足りなければ短押し
    Rig r2; CHECK(r2.press(2, LONG_PRESS_MS - 100) == UI_ACT_NONE && r2.ui.screen == UI_SCREEN_RX);
    Rig r2b; CHECK(r2b.press(0, LONG_PRESS_MS - 100) == UI_ACT_NONE && r2b.ui.screen == UI_SCREEN_TXSTAT);
    // 同時押しは無効
    Rig r5; uint32_t t = r5.t;
    ui_buttons(&r5.ui, &r5.sm, true, false, false, t);
    ui_buttons(&r5.ui, &r5.sm, true, true, false, t + 50);
    UiAction x = ui_buttons(&r5.ui, &r5.sm, false, false, false, t + 100);
    CHECK(x == UI_ACT_NONE && r5.ui.screen == UI_SCREEN_OWN);
    UiAction y = UI_ACT_NONE;
    for (uint32_t k = 0; k <= 2000; k += 20) { UiAction z = ui_buttons(&r5.ui, &r5.sm, true, false, true, t + 1000 + k); if (z) y = z; }   // 同時の長押しでも送信・メニューにならない
    ui_buttons(&r5.ui, &r5.sm, false, false, false, t + 3100);
    CHECK(y == UI_ACT_NONE && r5.ui.screen == UI_SCREEN_OWN);
    // 離してからもう一度押せば普通に動く
    CHECK(r5.longp(0) == UI_ACT_SEND);
    // 押しっぱなしでの長押しの繰り返し発火なし
    Rig r6; ui_buttons(&r6.ui, &r6.sm, false, false, true, r6.t);
    for (int k = 1; k < 200; k++) ui_buttons(&r6.ui, &r6.sm, false, false, true, r6.t + k * 20);
    CHECK(r6.ui.screen == UI_SCREEN_MENU);
    ui_buttons(&r6.ui, &r6.sm, false, false, false, r6.t + 5000);
    CHECK(r6.ui.screen == UI_SCREEN_MENU);                             // 離しても閉じない/動かない
    // millis() の桁あふれをまたいでも長押し判定できる
    Rig r7; r7.t = 0xFFFFFF00u; r7.longp(2); CHECK(r7.ui.screen == UI_SCREEN_MENU);
    Rig r8; r8.t = 0xFFFFFF00u; CHECK(r8.longp(0) == UI_ACT_SEND); }

  // ---- 送信中はボタンを無視 ----
  { Rig r; ui_set_tx_busy(&r.ui, true);
    CHECK(r.shortp(0) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_OWN); CHECK(r.shortp(2) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_OWN);
    CHECK(r.longp(0) == UI_ACT_NONE); r.longp(2); CHECK(r.ui.screen == UI_SCREEN_OWN);
    // 送信中に押し始めたものは、送信が終わってから離しても無効
    uint32_t t = r.t; ui_buttons(&r.ui, &r.sm, true, false, false, t);
    ui_set_tx_busy(&r.ui, false);
    CHECK(ui_buttons(&r.ui, &r.sm, false, false, false, t + 100) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_OWN);
    { uint32_t t2 = r.t; ui_buttons(&r.ui, &r.sm, true, false, false, t2); ui_set_tx_busy(&r.ui, true); ui_set_tx_busy(&r.ui, false);
      UiAction z = UI_ACT_NONE; for (uint32_t k = 20; k <= 1500; k += 20) { UiAction w = ui_buttons(&r.ui, &r.sm, true, false, false, t2 + k); if (w) z = w; }
      ui_buttons(&r.ui, &r.sm, false, false, false, t2 + 1600); CHECK(z == UI_ACT_SEND); r.t = t2 + 1800; }   // 送信していないときに押し始めた長押しは有効
    CHECK(r.longp(0) == UI_ACT_SEND);
    // 画面: 「送信中」 + ボタン表示なし
    UiSnapshot sn = snap_of(&r.sm); UiView v; ui_set_tx_busy(&r.ui, true);
    ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "PTT") && v.keyShort[0][0] == 0 && v.keyLong[0][0] == 0 && v.icons.act == UI_ACTICON_TX && v.lines[0].color == UI_COL_BAD);
    ui_set_tx_busy(&r.ui, false); ui_render(&r.ui, &sn, &v, r.t); CHECK(!has_line(v, "送信中") && !strcmp(v.keyShort[0], "前") && !strcmp(v.keyLong[0], "送信") && v.icons.act == UI_ACTICON_NONE);
    // PTT: 送信中の自局画面にPTTの状態を出す (待ち・終了処理中は OFF)
    ui_set_tx_busy(&r.ui, true); sn.ptt = true; ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "PTT ON") && !has_line(v, "送信可") && v.count <= UI_MAX_LINES);
    sn.ptt = false; ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "PTT OFF") && v.icons.act == UI_ACTICON_TX);
    ui_set_tx_busy(&r.ui, false); sn.ptt = false; ui_render(&r.ui, &sn, &v, r.t); CHECK(!has_line(v, "PTT ON"));
    // 送信画面
    r.ui.screen = UI_SCREEN_TXSTAT; ui_set_tx_busy(&r.ui, true); sn.ptt = true; ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "PTT ON (送信中)") && v.count <= UI_MAX_LINES);
    ui_set_tx_busy(&r.ui, false); sn.pttTimeouts = 2; ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "PTT時間超過で中止 2回") && !has_line(v, "PTT ON") && v.count <= UI_MAX_LINES);
    sn.pttTimeouts = 0; ui_render(&r.ui, &sn, &v, r.t); CHECK(!has_line(v, "PTT")); }

  // ---- 設定メニュー ----
  { Rig r; r.longp(2);
    CHECK(r.ui.screen == UI_SCREEN_MENU && r.ui.menuItem == UI_MENU_AUTOTX);          // 最初の項目は自動送信
    CHECK(r.shortp(1) == UI_ACT_NONE && r.sm.cur.autoTx == AUTO_TX_ACK_ONLY);           // B短で次の選択肢へ (送信はしない)
    for (int i = 2; i < AUTO_TX_FLEX; i++) r.shortp(1);
    CHECK(r.sm.cur.autoTx == AUTO_TX_FLEX - 1);
    { UiSnapshot sn = snap_of(&r.sm); UiView v; ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "  フレックス (config.txt に設定なし)")); }
    r.shortp(1); CHECK(r.sm.cur.autoTx == AUTO_TX_OFF);   // config.txt にフレックスの値がなければ飛ばす
    // フレックスの値があれば選べる
    g_cfg.flex.set = 0x7F; g_cfg.flex.lowSpeed = 5; g_cfg.flex.highSpeed = 70; g_cfg.flex.slowRateMin = 30; g_cfg.flex.fastRateSec = 120;
    g_cfg.flex.turnAngle = 28; g_cfg.flex.turnSlope = 26; g_cfg.flex.turnTimeSec = 30;
    for (int i = 0; i < AUTO_TX_FLEX; i++) r.shortp(1);
    CHECK(r.sm.cur.autoTx == AUTO_TX_FLEX);
    { UiSnapshot sn = snap_of(&r.sm); UiView v; ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "> フレックス (速度と向きで変える)")); }
    r.shortp(1); CHECK(r.sm.cur.autoTx == AUTO_TX_OFF);
    // 自局画面・送信状態の画面の表示 (日本語・英語とも1行に収まる)
    settings_set_auto_tx(&r.sm, AUTO_TX_FLEX);
    for (int lang = 0; lang < 2; lang++) {
      ui_set_lang(lang ? UI_LANG_EN : UI_LANG_JA);
      for (int ok = 0; ok < 2; ok++) {
        g_cfg.flex.set = ok ? 0x7F : 0x3F;
        UiSnapshot sn = snap_of(&r.sm); sn.autoTx = AUTO_TX_FLEX; sn.autoRemainMs = 140000; UiView v;
        r.ui.screen = UI_SCREEN_OWN; ui_render(&r.ui, &sn, &v, r.t);
        if (!lang) CHECK(ok ? has_line(v, "次の自動送信まで 2:20") : has_line(v, "送信内容:  フレックスの設定なし"));
        else CHECK(ok ? has_line(v, "next auto TX in 2:20") : has_line(v, "no Flexible settings"));
        for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
        r.ui.screen = UI_SCREEN_TXSTAT; ui_render(&r.ui, &sn, &v, r.t);
        if (!lang) CHECK(has_line(v, ok ? "自動送信: フレックス (次 2:20)" : "自動送信: フレックス (設定なし)"));
        else CHECK(has_line(v, ok ? "Flexible (next 2:20)" : "Flexible (no settings)"));
        for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
        r.ui.screen = UI_SCREEN_MENU; r.ui.menuItem = UI_MENU_AUTOTX; ui_render(&r.ui, &sn, &v, r.t);
        for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
        r.ui.screen = UI_SCREEN_OWN; ui_render(&r.ui, &sn, &v, r.t); CHECK(v.icons.autoTx == (ok ? UI_AUTOICON_ON : UI_AUTOICON_ACK));
      }
    }
    ui_set_lang(UI_LANG_JA); settings_set_auto_tx(&r.sm, AUTO_TX_OFF); r.ui.screen = UI_SCREEN_MENU; r.ui.menuItem = UI_MENU_AUTOTX;
    g_cfg.flex.set = 0;
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_AMBIGUITY);
    CHECK(r.shortp(1) == UI_ACT_NONE); CHECK(r.sm.cur.ambiguity == 1);                // 値の切り替え
    for (int i = 0; i < 4; i++) { r.shortp(1); } CHECK(r.sm.cur.ambiguity == 0);          // 一周して0
    r.shortp(0); CHECK(r.ui.menuItem == UI_MENU_AUTOTX);                              // 前の項目
    r.shortp(0); CHECK(r.ui.menuItem == UI_MENU_VERSION);                             // 最初から前は最後へ (固定位置の選択は取得元=固定のときだけ表示)
    r.shortp(2); CHECK(r.ui.menuItem == 0);                                           // 次の項目
    r.shortp(2); r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_DIGIPATH);
    uint8_t d0 = r.sm.cur.digipathIndex; r.shortp(1); CHECK(r.sm.cur.digipathIndex == (d0 + 1) % DIGIPATH_CHOICE_COUNT);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_SYMBOL);
    r.shortp(1); CHECK(r.sm.cur.symbolIndex == 1);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_SSID);
    r.shortp(1); CHECK(r.sm.cur.ssid == DEFAULT_SSID + 1);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_COURSE_SPEED); r.shortp(1); CHECK(r.sm.cur.sendCourseSpeed);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_ALTITUDE);     r.shortp(1); CHECK(r.sm.cur.sendAltitude);
    r.shortp(1); CHECK(!r.sm.cur.sendAltitude);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_MONITOR);
    CHECK(r.sm.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    r.shortp(1); CHECK(r.sm.cur.monitorLevel == (DEFAULT_MONITOR_LEVEL + 1) % (MONITOR_LEVEL_MAX + 1));
    for (int i = 0; i < MONITOR_LEVEL_MAX; i++) r.shortp(1);                        // 合計 MAX+1 回で一周して元の値
    CHECK(r.sm.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_CHIME);                                 // 受信音の音量: モニター音量と別。5段階で一周
    CHECK(r.sm.cur.chimeLevel == DEFAULT_CHIME_LEVEL);
    { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t);
      CHECK(has_line(v0, "受信音の音量") && has_line(v0, "  0 オフ (鳴らさない)") && has_line(v0, "> 2") && has_line(v0, "  4 最大") && v0.icons.bell == 2); }
    r.shortp(1); CHECK(r.sm.cur.chimeLevel == DEFAULT_CHIME_LEVEL + 1 && r.sm.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    for (int i = 0; i < MONITOR_LEVEL_MAX; i++) r.shortp(1);
    CHECK(r.sm.cur.chimeLevel == DEFAULT_CHIME_LEVEL);
    settings_set_chime(&r.sm, 0); { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t); CHECK(v0.icons.bell == 0 && v0.icons.vol != 0); }
    settings_set_chime(&r.sm, MONITOR_LEVEL_MAX); { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t); CHECK(v0.icons.bell == 4); }
    settings_set_chime(&r.sm, DEFAULT_CHIME_LEVEL);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_BRIGHTNESS);                           // 画面の明るさ: 5段階で一周
    CHECK(r.sm.cur.brightness == DEFAULT_BRIGHTNESS_LEVEL && settings_backlight(&r.sm) == 120);
    { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t);
      CHECK(has_line(v0, "画面の明るさ") && has_line(v0, "  1 夜間") && !has_line(v0, "わずかに") && has_line(v0, "> 4 日中") && has_line(v0, "  5 直射日光の下") && !strcmp(v0.keyShort[1], "変更")); }
    r.shortp(1); CHECK(r.sm.cur.brightness == 4 && settings_backlight(&r.sm) == 255);
    r.shortp(1); CHECK(r.sm.cur.brightness == 0 && settings_backlight(&r.sm) == 1);
    for (int i = 0; i < BRIGHTNESS_LEVEL_COUNT - 2; i++) r.shortp(1);
    CHECK(r.sm.cur.brightness == DEFAULT_BRIGHTNESS_LEVEL);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_POSSRC);
    CHECK(r.sm.cur.positionSource == POSITION_SOURCE_GPS);
    r.shortp(1); CHECK(r.sm.cur.positionSource == POSITION_SOURCE_LAST);
    if (fixed_position_count_set() > 0) {
      r.shortp(1); CHECK(r.sm.cur.positionSource == POSITION_SOURCE_FIXED);       // 固定位置を選ぶと、次の項目に「固定位置の選択」が現れる
      r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_FIXED);
      int before = r.sm.cur.fixedIndex; r.shortp(1); CHECK(r.sm.cur.fixedIndex != before || fixed_position_count_set() == 1); CHECK(fixed_position_set(r.sm.cur.fixedIndex));
      r.shortp(0); CHECK(r.ui.menuItem == UI_MENU_POSSRC);
      r.shortp(1); CHECK(r.sm.cur.positionSource == POSITION_SOURCE_GPS);          // GPSへ戻ると「固定位置の選択」は隠れる
      r.shortp(0); CHECK(r.ui.menuItem == UI_MENU_BRIGHTNESS); r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_POSSRC);
    } else {
      r.shortp(1); CHECK(r.sm.cur.positionSource == POSITION_SOURCE_GPS);          // 固定位置が1件もなければ「固定」は飛ばす
    }
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_CALIB);                              // 取得元の次は「調整トーン」
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_CLEAR);                              // 次は「記録の消去」 (B短は「開く」)
    { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t); CHECK(!strcmp(v0.keyShort[1], "開く")); }
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_LANGUAGE);                           // 言語: B短で日本語⇔英語、すぐ切り替わる
    CHECK(r.sm.cur.language == 0 && ui_lang() == UI_LANG_JA);
    { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t);
      CHECK(has_line(v0, "言語 / Language") && has_line(v0, "> 日本語") && has_line(v0, "  English") && !strcmp(v0.keyShort[1], "変更")); }
    r.shortp(1); CHECK(r.sm.cur.language == 1 && ui_lang() == UI_LANG_EN);
    { UiSnapshot s0 = snap_of(&r.sm); UiView v0; ui_render(&r.ui, &s0, &v0, r.t);
      CHECK(has_line(v0, "Settings 14/15") && has_line(v0, "言語 / Language") && has_line(v0, "> English") && !strcmp(v0.keyShort[1], "Change") && !strcmp(v0.keyLong[1], "Close")); }
    r.shortp(1); CHECK(r.sm.cur.language == 0 && ui_lang() == UI_LANG_JA);
    r.shortp(2); CHECK(r.ui.menuItem == UI_MENU_VERSION);                            // 最後は「バージョン情報」
    r.shortp(1); CHECK(r.ui.screen == UI_SCREEN_MENU && r.ui.menuItem == UI_MENU_VERSION);   // B短押しは何もしない
    r.shortp(2); CHECK(r.ui.menuItem == 0);                                         // 最後の次は最初へ
    r.shortp(2); CHECK(r.sm.cur.ambiguity == 0);                                                    // 他の項目に影響しない
    // メニュー内ではA長は送信にならない (何もしない)。C長・B長で閉じる
    CHECK(r.longp(0) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_MENU);
    r.longp(2); CHECK(r.ui.screen == UI_SCREEN_OWN);
    r.longp(2); CHECK(r.ui.screen == UI_SCREEN_MENU && r.ui.menuItem == 0);
    r.longp(1); CHECK(r.ui.screen == UI_SCREEN_OWN);
    r.shortp(2); r.longp(2); bool before = r.sm.cur.sendCourseSpeed; r.longp(1);
    CHECK(r.ui.screen == UI_SCREEN_RX && r.sm.cur.sendCourseSpeed == before);   // 開いた画面に戻る。閉じるときに値は変わらない
    // メニューを開き直すと最初の項目から
    r.longp(2); CHECK(r.ui.menuItem == 0); }

  // ---- メッセージ ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    ui_show_message(&r.ui, "テスト表示", 5000, 2000);
    ui_render(&r.ui, &sn, &v, 5500); CHECK(has_line(v, "テスト表示"));
    ui_render(&r.ui, &sn, &v, 7001); CHECK(!has_line(v, "テスト表示"));
    ui_show_message(&r.ui, "wrap", 0xFFFFFF00u, 1000);                                 // 桁あふれ
    ui_render(&r.ui, &sn, &v, 0xFFFFFF80u); CHECK(has_line(v, "wrap"));
    ui_render(&r.ui, &sn, &v, 0x00000400u); CHECK(!has_line(v, "wrap")); }

  // ---- 自局画面の表示 ----
  { Rig r; GpsState g; gps_init(&g); fix(g, 1000);
    TxComposed* c = new TxComposed; CHECK(compose(&r.sm, &g, 1500, c) == TXC_OK);
    UiSnapshot sn = snap_of(&r.sm); UiView v;
    sn.gps = GPS_FIX; sn.hasPos = true; sn.latDeg = 35.6667; sn.lonDeg = -139.75; sn.satellites = 8;
    sn.hasUtc = true; sn.utc.year = 2026; sn.utc.month = 10; sn.utc.day = 9; sn.utc.hour = 12; sn.utc.minute = 34; sn.utc.second = 56;
    sn.previewValid = true; snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s", c->header); snprintf(sn.previewInfo, sizeof(sn.previewInfo), "%s", c->info);
    ui_render(&r.ui, &sn, &v, 2000);
    CHECK(has_line(v, "測位中") && has_line(v, "衛星8") && has_line(v, "位置 35.66670N 139.75000W"));
    CHECK(has_line(v, "2026-10-09 12:34:56") && has_line(v, "送信可") && has_line(v, MY_CALLSIGN "-9>APM5AP,WIDE1-1:"));
    CHECK(has_line(v, "=3540.00N/13945.00E>"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(v.count <= UI_MAX_LINES);
    // 各状態の警告
    sn.gps = GPS_NO_DATA; sn.previewValid = false; sn.hasPos = false; ui_render(&r.ui, &sn, &v, 2000);
    CHECK(has_line(v, "GPS受信なし") && has_line(v, "未測位のため送信できません") && has_line(v, "位置 ---") && v.icons.pos == UI_POSICON_GPS_NONE);
    sn.gps = GPS_NO_FIX; ui_render(&r.ui, &sn, &v, 2000); CHECK(has_line(v, "測位待ち") && v.icons.pos == UI_POSICON_GPS_WAIT);
    sn.gps = GPS_STALE; sn.posAgeSec = 125; sn.previewValid = true; sn.previewStale = true; sn.hasPos = true; ui_render(&r.ui, &sn, &v, 2000);
    CHECK(has_line(v, "2m05s前") && has_line(v, "最終測位位置で送信します") && v.icons.pos == UI_POSICON_GPS_STALE);
    sn.sdOk = false; ui_render(&r.ui, &sn, &v, 2000); CHECK(!has_line(v, "SD") && !v.icons.sdOk);
    sn.rxCarrier = true; ui_render(&r.ui, &sn, &v, 2000); CHECK(v.icons.act == UI_ACTICON_RX);
    sn.hasUtc = false; ui_render(&r.ui, &sn, &v, 2000); CHECK(has_line(v, "UTC ---"));
    // 最大長の情報フィールド (コメントが長い) でも行数・桁数が収まる
    memset(sn.previewInfo, 'x', 100); sn.previewInfo[100] = 0; ui_render(&r.ui, &sn, &v, 2000);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(v.count <= UI_MAX_LINES);
    delete c; }

  // ---- 受信一覧 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    ui_render(&r.ui, &sn, &v, 0); r.ui.screen = UI_SCREEN_RX; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "まだ受信していません"));
    AprsPacket p; memset(&p, 0, sizeof(p));
    LogTime lt; memset(&lt, 0, sizeof(lt)); lt.valid = true; lt.hour = 1; lt.minute = 2; lt.second = 3;
    snprintf(p.src, sizeof(p.src), "NOCALL-9"); p.kind = APRS_KIND_POSITION; p.hasPos = true; p.latDeg = 35.0; p.lonDeg = 140.0;
    p.symTable = '/'; p.symCode = '>'; p.hasAltitude = true; p.altitudeM = 61.4; snprintf(p.comment, sizeof(p.comment), "hello");
    ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 1000);
    ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "01:02:03 NOCALL-9") && has_line(v, "91.") == false);
    CHECK(has_line(v, "km 09") || has_line(v, "km 08"));          // 東へ約91km、方位は約90° (大圏の初期方位は少し南寄り)
    CHECK(has_line(v, "  61m hello") && !has_line(v, "/> 61m"));                   // シンボルは文字ではなくアイコン
    { int k = -1; for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, "01:02:03 NOCALL-9")) k = i;
      CHECK(k > 0 && v.lines[k].icon == 1 && v.lines[k].iconCol == 0 && !strncmp(v.lines[k].text, "  01:02:03", 10)); }
    // 位置なし、時刻なし、自局位置なし
    AprsPacket q; memset(&q, 0, sizeof(q)); snprintf(q.src, sizeof(q.src), "NOCAL2"); snprintf(q.comment, sizeof(q.comment), ":MSG");
    ui_rx_add(&r.ui, &q, false, 0, 0, NULL, 1000);
    ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "--:--:-- NOCAL2    その他") && has_line(v, "  :MSG"));
    CHECK(strstr(v.lines[1].text, "NOCAL2") != NULL);              // 新しいものが上
    p.hasAltitude = false; ui_rx_add(&r.ui, &p, false, 0, 0, &lt, 1000); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "距離不明"));
    // 溢れ: RX_LIST_COUNT を超えて追加しても最新が上、行数が収まる
    for (int i = 0; i < RX_LIST_COUNT * 2 + 3; i++) { snprintf(p.src, sizeof(p.src), "N%u", (unsigned)(i % 1000)); ui_rx_add(&r.ui, &p, true, 35, 139, &lt, 1000); }
    CHECK(r.ui.rxCount == RX_LIST_COUNT && r.ui.rxTotal == 3 + RX_LIST_COUNT * 2 + 3);
    ui_render(&r.ui, &sn, &v, 0);
    char want[16]; snprintf(want, sizeof(want), "N%d", RX_LIST_COUNT * 2 + 2);
    CHECK(strstr(v.lines[1].text, want) != NULL && v.count <= UI_MAX_LINES);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    // 近距離は小数1桁、遠距離は整数
    Rig r2; r2.ui.screen = UI_SCREEN_RX; p.latDeg = 35.0; p.lonDeg = 139.05; ui_rx_add(&r2.ui, &p, true, 35.0, 139.0, &lt, 1000);
    ui_render(&r2.ui, &sn, &v, 0); CHECK(has_line(v, "4.5km") || has_line(v, "4.6km"));
    // 長いコメント・長い呼出符号でも収まる
    memset(p.comment, 'c', 99); p.comment[99] = 0; snprintf(p.src, sizeof(p.src), "NOCALL-15"); ui_rx_add(&r2.ui, &p, true, 35, 139, &lt, 1000);
    ui_render(&r2.ui, &sn, &v, 0); for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS); }

  // ---- 送信・設定画面 ----
  { Rig r; r.ui.screen = UI_SCREEN_TXSTAT; UiSnapshot sn = snap_of(&r.sm); UiView v;
    sn.rxFramesOk = 12; sn.rxFcsErrors = 3; sn.txCount = 4; sn.rxOverruns = 1; sn.queueDropped = 2; sn.sdPendingBytes = 512; sn.uptimeMs = 3723000;
    ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "局: " MY_CALLSIGN "-9") && has_line(v, "シンボル: /> 車") && has_line(v, "デジパス: WIDE1-1") && has_line(v, "曖昧化: Lv0"));
    CHECK(has_line(v, "進行方向・速度: オフ") && has_line(v, "高度: オフ") );
    CHECK(has_line(v, "送信 4回") && !has_line(v, "FCS") && !has_line(v, "取りこぼし") && !has_line(v, "SD") && has_line(v, "稼働 1:02:03"));
    CHECK(has_line(v, "設定保存: 待ち") == settings_dirty(&r.sm));
    CHECK(!has_line(v, "モニター音量") && v.icons.vol == 2);
    settings_set_monitor(&r.sm, 0); ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.vol == 0);
    settings_set_monitor(&r.sm, 1); ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.vol == 1);
    settings_set_monitor(&r.sm, 4); ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.vol == 4);
    settings_set_monitor(&r.sm, DEFAULT_MONITOR_LEVEL);
    settings_set_digipath(&r.sm, 0); settings_set_symbol(&r.sm, (uint8_t)symbol_find('/', '-')); settings_set_ssid(&r.sm, 0); settings_set_altitude(&r.sm, true);
    ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "デジパス: なし") && has_line(v, "局: " MY_CALLSIGN) && !has_line(v, "局: " MY_CALLSIGN "-") && has_line(v, "高度: オン") && has_line(v, "シンボル: /- 自宅"));
    settings_set_ssid(&r.sm, 7); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "局: " MY_CALLSIGN "-7"));
    CHECK(!strcmp(v.keyShort[0], "前") && !strcmp(v.keyLong[0], "送信") && v.keyShort[1][0] == 0 && v.keyLong[1][0] == 0 && !strcmp(v.keyShort[2], "次") && !strcmp(v.keyLong[2], "設定"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(v.count <= UI_MAX_LINES); }

  // ---- 受信一覧: 速度 (km/h) ----
  { Rig r; r.ui.screen = UI_SCREEN_RX; UiSnapshot sn = snap_of(&r.sm); UiView v;
    AprsPacket p; memset(&p, 0, sizeof(p)); LogTime lt; memset(&lt, 0, sizeof(lt));
    snprintf(p.src, sizeof(p.src), "NOCALL-9"); p.kind = APRS_KIND_POSITION; p.hasPos = true; p.latDeg = 35.0; p.lonDeg = 139.1;
    p.symTable = '/'; p.symCode = '>';
    p.hasSpeed = true; p.speedKnots = 30;                                   // 30kt = 55.6km/h → 56
    ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, "56km/h") != NULL);
    CHECK(strstr(v.lines[1].text, "NOCALL-9") != NULL && strstr(v.lines[1].text, "°") != NULL);
    p.speedKnots = 0; ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, " 0km/h") != NULL);                        // 速度0は「0km/h」
    p.hasSpeed = false; ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, " -km/h") != NULL && strstr(v.lines[1].text, "0km/h") == NULL);   // 付いていない
    p.hasSpeed = true; p.speedKnots = 600; ui_rx_add(&r.ui, &p, false, 0, 0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, "距離不明") != NULL && strstr(v.lines[1].text, "1111km/h") != NULL);
    // 位置なしのパケットには速度の欄を出さない
    AprsPacket q; memset(&q, 0, sizeof(q)); snprintf(q.src, sizeof(q.src), "NOCAL2"); q.hasSpeed = true; q.speedKnots = 10;
    ui_rx_add(&r.ui, &q, true, 35, 139, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, "その他") != NULL && strstr(v.lines[1].text, "km/h") == NULL);
    // 最も長い1行 (SSID-15、3桁の距離、3桁の速度) でも、38桁に収まり、速度が切れない
    p.hasSpeed = true; p.speedKnots = 400; snprintf(p.src, sizeof(p.src), "NOCALL-15"); p.lonDeg = 140.5;
    ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(strstr(v.lines[1].text, "741km/h") != NULL && line_cols(v.lines[1].text) <= UI_COLS);
    // アイコンのないシンボルは、今までどおり文字で出す
    p.symTable = '\\'; p.symCode = 'n'; snprintf(p.src, sizeof(p.src), "NOCALL-3"); ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 0); ui_render(&r.ui, &sn, &v, 0);
    CHECK(v.lines[1].icon == 0 && !strncmp(v.lines[2].text, "  \\n", 4)); }

  // ---- 受信状態画面 ----
  { Rig r; r.ui.screen = UI_SCREEN_RXSTAT; UiSnapshot sn = snap_of(&r.sm); UiView v;
    sn.rxFramesOk = 12; sn.rxFcsErrors = 3; sn.rxFragments = 77; sn.rxFlags = 456; sn.rxDiscards = 5; sn.rxOverruns = 1; sn.queueDropped = 2;
    ui_render(&r.ui, &sn, &v, 5000);
    CHECK(v.lines[0].color == UI_COL_HEAD && has_line(v, "受信状態") && !has_line(v, "5/6"));
    CHECK(has_line(v, "無信号") && v.lines[1].color == UI_COL_DIM);
    CHECK(has_line(v, "受信OK 12  CRC不良 3  雑音 77") && has_line(v, "フラグ検出 456  途中破棄 5") && has_line(v, "取りこぼし 1  待ち行列破棄 2"));
    CHECK(has_line(v, "直近: なし") && has_line(v, "MARK/SPACE = ---"));
    CHECK(v.barCount == 3 && v.bars[0].row == 2 && v.bars[1].row == 3 && v.bars[2].row == 4);
    CHECK(strstr(v.lines[2].text, "入力レベル") && strstr(v.lines[3].text, "MARK") && strstr(v.lines[4].text, "SPACE"));
    CHECK(v.bars[0].pct == 0 && v.bars[0].color == UI_COL_DIM);               // 信号なし: 灰色
    CHECK(!has_line(v, "大きすぎ") && !has_line(v, "小さい"));                  // 信号がなければ、レベルの警告は出さない
    // 信号あり (まだAFSKとは確認できない)
    sn.rx.phase = AFSK_PHASE_SIGNAL; sn.rx.peak = 1000; sn.rx.markAmp = 400; sn.rx.spaceAmp = 200; sn.rx.markAvgAmp = 400; sn.rx.spaceAvgAmp = 200;
    ui_render(&r.ui, &sn, &v, 5000);
    CHECK(has_line(v, "信号あり") && v.lines[1].color == UI_COL_WARN);
    CHECK(v.bars[0].pct == 50 && v.bars[1].pct == 20 && v.bars[2].pct == 10 && v.bars[0].color == UI_COL_GOOD && v.bars[1].color == UI_COL_GOOD);
    CHECK(has_line(v, "MARK/SPACE = 2.0") && v.lines[6].color == UI_COL_GOOD);   // 比2.0 (良好の範囲の端)
    // データ受信中
    sn.rx.phase = AFSK_PHASE_DATA; sn.rx.frameBytes = 42; sn.rx.syncFlags = 30;
    ui_render(&r.ui, &sn, &v, 5000); CHECK(has_line(v, "データ受信中 42バイト") && v.lines[1].color == UI_COL_GOOD);
    // 結果の表示: 2秒間 (信号がなくなったあと)
    sn.rx.phase = AFSK_PHASE_IDLE; sn.rx.peak = 0; sn.rx.markAmp = sn.rx.spaceAmp = 0; sn.rx.lastResult = AFSK_RESULT_OK; sn.rx.lastResultMs = 4000;
    ui_render(&r.ui, &sn, &v, 5000); CHECK(!strcmp(v.lines[1].text, "受信OK") && v.lines[1].color == UI_COL_GOOD);
    ui_render(&r.ui, &sn, &v, 4000 + RX_RESULT_SHOW_MS - 1); CHECK(!strcmp(v.lines[1].text, "受信OK"));
    ui_render(&r.ui, &sn, &v, 4000 + RX_RESULT_SHOW_MS); CHECK(!strcmp(v.lines[1].text, "無信号"));   // 2秒で消える
    sn.rx.lastResult = AFSK_RESULT_FCS_ERROR; ui_render(&r.ui, &sn, &v, 5000); CHECK(!strcmp(v.lines[1].text, "FCS不良") && v.lines[1].color == UI_COL_BAD);
    sn.rx.phase = AFSK_PHASE_DATA; ui_render(&r.ui, &sn, &v, 5000); CHECK(strstr(v.lines[1].text, "データ受信中") != NULL);   // 次のデータが来たらそちらを優先
    sn.rx.phase = AFSK_PHASE_SIGNAL; ui_render(&r.ui, &sn, &v, 5000); CHECK(!strcmp(v.lines[1].text, "FCS不良"));                     // 信号だけなら結果を残す
    // millis の桁あふれをまたいでも2秒で消える
    sn.rx.phase = AFSK_PHASE_IDLE; sn.rx.lastResult = AFSK_RESULT_OK; sn.rx.lastResultMs = 0xFFFFFF00u;
    ui_render(&r.ui, &sn, &v, 0xFFFFFF80u); CHECK(!strcmp(v.lines[1].text, "受信OK"));
    ui_render(&r.ui, &sn, &v, 0x00000900u); CHECK(!strcmp(v.lines[1].text, "無信号"));
    // 送信中
    sn.rx.lastResult = AFSK_RESULT_NONE; ui_set_tx_busy(&r.ui, true); ui_render(&r.ui, &sn, &v, 5000);
    CHECK(has_line(v, "送信中 (受信停止)") && v.icons.act == UI_ACTICON_TX); ui_set_tx_busy(&r.ui, false);
    // 直流が範囲外 (入力なしでも出る): 「大きすぎます」ではなく、バイアス確認の警告
    memset(&sn.rx, 0, sizeof(sn.rx)); sn.rx.dcOffset = -2040; ui_render(&r.ui, &sn, &v, 5000);
    CHECK(!has_line(v, "入力が大きすぎます") && has_line(v, "ADC入力の直流が範囲外"));
    sn.rx.dcOffset = 900; ui_render(&r.ui, &sn, &v, 5000); CHECK(!has_line(v, "ADC入力の直流が範囲外"));
    sn.rx.dcOffset = 1000; ui_render(&r.ui, &sn, &v, 5000); CHECK(has_line(v, "ADC入力の直流が範囲外"));
    sn.rx.clipped = true; ui_render(&r.ui, &sn, &v, 5000); CHECK(has_line(v, "入力が大きすぎます") && !has_line(v, "ADC入力の直流が範囲外"));
    // 入力レベルの警告
    memset(&sn.rx, 0, sizeof(sn.rx)); sn.rx.phase = AFSK_PHASE_SIGNAL; sn.rx.peak = 2040; sn.rx.clipped = true; sn.rx.markAmp = 1500; sn.rx.spaceAmp = 900; sn.rx.markAvgAmp = 1500; sn.rx.spaceAvgAmp = 900;
    ui_render(&r.ui, &sn, &v, 5000); CHECK(v.bars[0].pct == 100 && v.bars[0].color == UI_COL_BAD && has_line(v, "入力が大きすぎます"));
    sn.rx.clipped = false; sn.rx.peak = 60; sn.rx.markAmp = 30; sn.rx.spaceAmp = 20;
    ui_render(&r.ui, &sn, &v, 5000); CHECK(v.bars[0].color == UI_COL_WARN && has_line(v, "入力が小さい") && has_line(v, "MARK/SPACE = ---"));   // 振幅が小さい間は比を出さない
    sn.rx.peak = UI_LEVEL_LOW; ui_render(&r.ui, &sn, &v, 5000); CHECK(v.bars[0].color == UI_COL_GOOD && !has_line(v, "入力が小さい"));
    sn.rx.peak = 4095; ui_render(&r.ui, &sn, &v, 5000); CHECK(v.bars[0].pct == 100);                                    // 100%でそろえる
    // MARK/SPACEの強さの比の判定 (実測 0.37〜3.5倍で受信できる)
    struct { unsigned m, s; uint8_t col; const char* txt; } rt[] = {
      { 400, 400, UI_COL_GOOD, "= 1.0" }, { 200, 400, UI_COL_GOOD, "= 0.5" }, { 300, 400, UI_COL_GOOD, "= 0.8" },
      { 150, 400, UI_COL_WARN, "= 0.4" }, { 100, 400, UI_COL_BAD, "= 0.2" }, { 700, 300, UI_COL_WARN, "= 2.3" },
      { 800, 100, UI_COL_BAD, "= 8.0" } };
    for (size_t i = 0; i < sizeof(rt) / sizeof(rt[0]); i++) {
      memset(&sn.rx, 0, sizeof(sn.rx)); sn.rx.phase = AFSK_PHASE_SIGNAL; sn.rx.peak = 800; sn.rx.markAmp = sn.rx.markAvgAmp = (uint16_t)rt[i].m; sn.rx.spaceAmp = sn.rx.spaceAvgAmp = (uint16_t)rt[i].s;
      ui_render(&r.ui, &sn, &v, 5000);
      bool found = false; for (int k = 0; k < v.count; k++) if (strstr(v.lines[k].text, "強さの比") && strstr(v.lines[k].text, rt[i].txt) && v.lines[k].color == rt[i].col) found = true;
      if (!found) printf("ratio case %zu: m=%u s=%u\n", i, rt[i].m, rt[i].s);
      CHECK(found); }
    // 直近の受信
    AprsPacket p; memset(&p, 0, sizeof(p)); snprintf(p.src, sizeof(p.src), "NOCALL-9"); p.hasPos = false; LogTime lt; memset(&lt, 0, sizeof(lt));
    ui_rx_add(&r.ui, &p, false, 0, 0, &lt, 1000);
    memset(&sn.rx, 0, sizeof(sn.rx));
    ui_render(&r.ui, &sn, &v, 13000); CHECK(has_line(v, "直近: NOCALL-9 12秒前"));
    ui_render(&r.ui, &sn, &v, 1000 + 65000); CHECK(has_line(v, "直近: NOCALL-9 1分05秒前"));
    ui_render(&r.ui, &sn, &v, 1000 + 7300000); CHECK(has_line(v, "2時間前"));
    // 最後に受けたパケットの MARK/SPACE の比
    sn.rx.lastOkMarkAmp = 900; sn.rx.lastOkSpaceAmp = 400;
    ui_render(&r.ui, &sn, &v, 13000); CHECK(has_line(v, "直近: NOCALL-9 12秒前 比2.2"));
    sn.rx.lastOkMarkAmp = 900; sn.rx.lastOkSpaceAmp = 200; ui_render(&r.ui, &sn, &v, 13000);
    { bool bad = false; for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, "比4.5") && v.lines[i].color == UI_COL_BAD) bad = true; CHECK(bad); }
    // 行数・桁数
    sn.rx.lastOkMarkAmp = 4095; sn.rx.lastOkSpaceAmp = 1;
    sn.rxFragments = 4000000000u;
    sn.rxFramesOk = sn.rxFcsErrors = sn.rxFlags = sn.rxDiscards = sn.rxOverruns = sn.queueDropped = 4000000000u;
    sn.rx.phase = AFSK_PHASE_DATA; sn.rx.frameBytes = 65535; sn.rx.peak = 65535; sn.rx.markAmp = sn.rx.spaceAmp = 65535; sn.rx.markAvgAmp = 65535; sn.rx.spaceAvgAmp = 1;
    ui_render(&r.ui, &sn, &v, 5000);
    CHECK(v.count <= UI_MAX_LINES); for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    for (int i = 0; i < v.barCount; i++) CHECK(v.bars[i].row >= 0 && v.bars[i].row < v.count && v.bars[i].pct <= 100); }

  // ---- 送信・設定画面: 最後の送信 ----
  { Rig r; r.ui.screen = UI_SCREEN_TXSTAT; UiSnapshot sn = snap_of(&r.sm); UiView v; sn.txCount = 3;
    ui_render(&r.ui, &sn, &v, 31000); CHECK(has_line(v, "送信 3回") && !has_line(v, "最後"));
    sn.hasLastTx = true; sn.lastTxMs = 1000; ui_render(&r.ui, &sn, &v, 31000); CHECK(has_line(v, "送信 3回  最後 30秒前"));
    CHECK(has_line(v, "送信・設定")); }

  // ---- 位置の取得元: 画面 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v; sn.gps = GPS_FIX; sn.satellites = 8;
    sn.hasPos = true; sn.latDeg = 35.5; sn.lonDeg = 139.5; sn.previewValid = true;
    snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s>APRS:", MY_CALLSIGN); snprintf(sn.previewInfo, sizeof(sn.previewInfo), "!3530.00N/13930.00E>");
    // GPS
    sn.posSource = POSITION_SOURCE_GPS; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "GPS測位中") && has_line(v, "送信可"));
    // 最後の位置: 現在地ではないと分かる表示。GPSの状態も添える
    sn.posSource = POSITION_SOURCE_LAST; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "位置:保存位置  GPS:測位中") && v.lines[1].color == UI_COL_HILITE && has_line(v, "保存位置で送信します") && !has_line(v, "送信可"));
    sn.gps = GPS_NO_DATA; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "位置:保存位置  GPS:受信なし"));
    sn.gps = GPS_NO_FIX; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "GPS:測位待ち"));
    sn.gps = GPS_STALE; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "GPS:途切れ"));
    // 固定位置: 名前を出す
    sn.gps = GPS_FIX; sn.posSource = POSITION_SOURCE_FIXED; snprintf(sn.posLabel, sizeof(sn.posLabel), "Home"); ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "位置:固定 Home  GPS:測位中") && has_line(v, "固定位置で送信します"));
    snprintf(sn.posLabel, sizeof(sn.posLabel), "LongName123456"); ui_render(&r.ui, &sn, &v, 0);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(has_line(v, "位置:固定 LongName12"));                                          // 10文字で切る
    // 位置が使えない理由
    sn.previewValid = false; sn.hasPos = false;
    sn.posSource = POSITION_SOURCE_GPS; sn.posReason = POS_GPS_NO_FIX; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "未測位のため送信できません") && !has_line(v, "位置の取得元を変更可"));
    sn.posSource = POSITION_SOURCE_LAST; sn.posReason = POS_LAST_NONE; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "保存された位置がありません") && !has_line(v, "未測位") && !has_line(v, "取得元を変更可"));
    sn.posSource = POSITION_SOURCE_FIXED; sn.posReason = POS_FIXED_UNSET; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "固定位置が未設定です"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    // 送信を断るメッセージ
    CHECK(!strcmp(ui_send_refused_text(POS_GPS_NO_DATA), "GPS受信なし: 送信しません") && !strcmp(ui_send_refused_text(POS_GPS_NO_FIX), "未測位のため送信しません"));
    CHECK(!strcmp(ui_send_refused_text(POS_LAST_NONE), "保存位置がないため送信しません") && !strcmp(ui_send_refused_text(POS_FIXED_UNSET), "固定位置が未設定です"));
    CHECK(ui_send_refused_text(POS_OK)[0] != 0);
    { const PosReason rs[] = { POS_GPS_NO_DATA, POS_GPS_NO_FIX, POS_LAST_NONE, POS_FIXED_UNSET, POS_OK };
      for (size_t i = 0; i < 5; i++) { char tmp[UI_TEXT_MAX]; snprintf(tmp, sizeof(tmp), "%s", ui_send_refused_text(rs[i])); CHECK(ui_fit(tmp, UI_COLS) <= UI_COLS && !strcmp(tmp, ui_send_refused_text(rs[i]))); } }   // 1行に収まる
    // 行数: 位置あり、情報フィールドが3行にわたる最大の場合でも収まる
    sn.previewValid = true; sn.hasPos = true; sn.posSource = POSITION_SOURCE_FIXED; sn.sdOk = false; memset(sn.previewInfo, 'x', 100); sn.previewInfo[100] = 0;
    ui_show_message(&r.ui, "メッセージ", 0, 5000); ui_render(&r.ui, &sn, &v, 100);
    CHECK(v.count <= UI_MAX_LINES); }

  // ---- 位置の取得元: 送信・設定画面と設定メニュー ----
  { Rig r; r.ui.screen = UI_SCREEN_TXSTAT; UiSnapshot sn = snap_of(&r.sm); UiView v;
    ui_render(&r.ui, &sn, &v, 0); CHECK(!has_line(v, "位置の取得元") && v.icons.pos == UI_POSICON_GPS_FIX);
    settings_set_position_source(&r.sm, POSITION_SOURCE_LAST); sn.posSource = POSITION_SOURCE_LAST; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.pos == UI_POSICON_LAST);
    settings_set_position_source(&r.sm, POSITION_SOURCE_FIXED); sn.posSource = POSITION_SOURCE_FIXED; snprintf(sn.posLabel, sizeof(sn.posLabel), "Home"); ui_render(&r.ui, &sn, &v, 0);
    CHECK(v.icons.pos == UI_POSICON_FIXED && v.count <= UI_MAX_LINES); sn.posSource = POSITION_SOURCE_GPS;
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);

    // メニュー: 取得元の選択
    r.ui.screen = UI_SCREEN_MENU; r.ui.menuItem = UI_MENU_POSSRC; settings_set_position_source(&r.sm, POSITION_SOURCE_GPS);
    ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "位置の取得元") && has_line(v, "> GPS (現在の測位)") && has_line(v, "  最後の位置 (保存済み)") && has_line(v, "  固定位置") && has_line(v, "自動では切り替わりません"));
    CHECK(has_line(v, "設定 11/15") == true);                                              // 固定位置の選択は隠れている
    settings_set_position_source(&r.sm, POSITION_SOURCE_LAST); ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "> 最後の位置") && has_line(v, "保存された位置はまだありません"));
    sn.lastPosValid = true; sn.lastPosLat = 35.68123; sn.lastPosLon = -139.5; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "保存: 35.68123N 139.50000W") && !has_line(v, "まだありません"));
    CHECK((fixed_position_count_set() == 0) == has_line(v, "固定位置は config.txt に設定します"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    // メニュー: 固定位置の選択 (取得元=固定のとき項目が増える)
    settings_set_position_source(&r.sm, POSITION_SOURCE_FIXED); ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "> 固定位置") && has_line(v, "設定 11/16"));
    r.ui.menuItem = UI_MENU_FIXED; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "固定位置の選択") && has_line(v, "設定 12/16") && v.count <= UI_MAX_LINES);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    if (fixed_position_count_set() == 0) CHECK(has_line(v, "設定済みの固定位置がありません"));
    else {
      int listed = 0; for (int i = 0; i < v.count; i++) if (v.lines[i].text[0] == '>' || (v.lines[i].text[0] == ' ' && v.lines[i].text[1] == ' ' && strstr(v.lines[i].text, "N "))) listed++;
      CHECK(listed >= fixed_position_count_set() - 1);
      for (int i = 0; i < FIXED_POSITION_COUNT; i++) CHECK(has_line(v, FIXED_POSITIONS[i].name) == fixed_position_set(i));   // 設定済みの欄だけが並ぶ
      settings_set_fixed_index(&r.sm, (uint8_t)0); ui_render(&r.ui, &sn, &v, 0);
      bool markHome = false; for (int i = 0; i < v.count; i++) if (v.lines[i].text[0] == '>' && strstr(v.lines[i].text, FIXED_POSITIONS[0].name)) markHome = true;
      CHECK(markHome == fixed_position_set(0)); } }

  // ---- 記録の消去 (0.13.3): メニューから開き、対象を選んで、確認してから消す ----
  { Rig r; static MsgCore mc; msg_init(&mc, 1); ui_set_msgs(&r.ui, &mc);
    AprsPacket p; memset(&p, 0, sizeof(p)); LogTime lt; memset(&lt, 0, sizeof(lt));
    for (int i = 0; i < 3; i++) { snprintf(p.src, sizeof(p.src), "N%dCALL", i); p.hasPos = true; p.latDeg = 35.0 + i * 0.01; p.lonDeg = 139.0; ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 1000 + i); }
    r.ui.wx[0].used = true; snprintf(r.ui.wx[0].src, sizeof(r.ui.wx[0].src), "N0CALL-13");
    CHECK(msg_send(&mc, "N1CALL", "TEST", 1000, NULL) >= 0);
    uint16_t no = mc.nextNo;
    r.longp(2); r.ui.menuItem = UI_MENU_CLEAR;
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_CLEAR && r.ui.clearTarget == UI_CLEAR_RX && !r.ui.clearConfirm);
    UiSnapshot sn = snap_of(&r.sm); UiView v;
    ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "記録の消去") && has_line(v, "> 受信一覧 (3件)") && has_line(v, "  WX局 (1件)") && has_line(v, "  メッセージ (1件)") && has_line(v, "  すべて ("));
    CHECK(!strcmp(v.keyShort[1], "選択") && !strcmp(v.keyLong[1], "戻る") && !strcmp(v.keyShort[0], "前"));
    // B で確認 → A でやめる (消えない)
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.clearConfirm);
    ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "「受信一覧」を消去します") && !strcmp(v.keyShort[1], "消去") && !strcmp(v.keyShort[0], "やめる"));
    CHECK(r.shortp(0) == UI_ACT_NONE && !r.ui.clearConfirm && r.ui.rxCount == 3 && r.ui.clearTarget == UI_CLEAR_RX);
    // B, B で受信一覧だけ消す
    r.shortp(1); CHECK(r.shortp(1) == UI_ACT_CLEARED && r.ui.clearDone == UI_CLEAR_RX && !r.ui.clearConfirm);
    CHECK(r.ui.rxCount == 0 && r.ui.rxTotal == 0 && !r.ui.hasLastRx && r.ui.wx[0].used && mc.e[0].used);
    // C でメッセージへ (WX の次)、消す。番号は続きから
    r.shortp(2); r.shortp(2); CHECK(r.ui.clearTarget == UI_CLEAR_MSG);
    r.shortp(1); CHECK(r.shortp(1) == UI_ACT_CLEARED && !mc.e[0].used && !mc.waiting && mc.nextNo == no && r.ui.wx[0].used);
    // A で前へ (先頭の前は最後の「すべて」)、すべて消す
    r.ui.clearTarget = UI_CLEAR_RX; r.shortp(0); CHECK(r.ui.clearTarget == UI_CLEAR_ALL);
    r.shortp(1); CHECK(r.shortp(1) == UI_ACT_CLEARED && !r.ui.wx[0].used);
    { bool any = false; for (int i = 0; i < RADAR_STATION_COUNT; i++) if (r.ui.st[i].used) any = true; CHECK(!any); }
    // B長でメニューへ戻る (確認中でも)
    r.shortp(1); CHECK(r.ui.clearConfirm); r.longp(1); CHECK(r.ui.screen == UI_SCREEN_MENU && !r.ui.clearConfirm);
    // 日本語・英語とも1行に収まる
    r.ui.screen = UI_SCREEN_CLEAR;
    for (int lang = 0; lang < 2; lang++) for (int cf = 0; cf < 2; cf++) for (int t = 0; t < UI_CLEAR_COUNT; t++) {
      ui_set_lang(lang ? UI_LANG_EN : UI_LANG_JA); r.ui.clearConfirm = cf; r.ui.clearTarget = (uint8_t)t;
      ui_render(&r.ui, &sn, &v, r.t); CHECK(v.count <= UI_MAX_LINES);
      for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
      CHECK(line_cols(v.lines[0].text) <= UI_TITLE_COLS);
    }
    ui_set_lang(UI_LANG_JA); }

  // ---- メニュー画面 (全項目・全値で行数と桁数が収まる) ----
  { Rig r; r.longp(2); UiSnapshot sn = snap_of(&r.sm); UiView v;
    for (int item = 0; item < UI_MENU_COUNT; item++) {
      r.ui.menuItem = (uint8_t)item;
      for (int k = 0; k < 9; k++) {
        ui_render(&r.ui, &sn, &v, r.t);
        CHECK(v.count <= UI_MAX_LINES);
        for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
        CHECK(!strcmp(v.keyShort[1], item == UI_MENU_VERSION ? "" : (item == UI_MENU_CALIB || item == UI_MENU_CLEAR) ? "開く" : "変更") && !strcmp(v.keyLong[1], "閉じる") && !strcmp(v.keyLong[2], "閉じる") && v.keyLong[0][0] == 0);
        int marks = 0; for (int i = 2; i < v.count; i++) if (v.lines[i].text[0] == '>') marks++;
        int want = (item == UI_MENU_FIXED && !fixed_position_set(r.sm.cur.fixedIndex)) ? 0 : 1;
        if (item == UI_MENU_CALIB || item == UI_MENU_CLEAR) want = 0;   // 調整トーン・記録の消去は値の選択ではなく、画面を開く項目
        if (item == UI_MENU_CALIB || item == UI_MENU_CLEAR) break;                   // (B短押しで画面が開くので、繰り返しはしない)
        if (item == UI_MENU_VERSION) break;                 // バージョン情報は表示だけ
        CHECK(marks == want);                                // 選択中の値に印が1つ (固定位置は、選んだ欄が設定済みのときだけ)
        UiState tmp = r.ui; (void)tmp;
        r.shortp(1);
        if (item == UI_MENU_LANGUAGE) { settings_set_language(&r.sm, 0); ui_set_lang(UI_LANG_JA); }   // 日本語に戻して続ける
      }
    }
    // シンボル: 21個のうち、選択中の前後が見える。各行の3桁目にアイコン
    r.ui.menuItem = UI_MENU_SYMBOL; settings_set_symbol(&r.sm, 0); ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "設定 4/15") && has_line(v, ">    />  車") && has_line(v, "/k  トラック") && !has_line(v, "デジピーター") && v.count == UI_MAX_LINES);
    for (int i = 3; i < v.count; i++) CHECK(v.lines[i].icon >= 1 && v.lines[i].iconCol == 2 && !v.lines[i].iconBig && !strncmp(v.lines[i].text + 1, "   ", 3));
    for (int k = 0; k < SYMBOL_CHOICE_COUNT; k++) {                                     // どれを選んでも、選択中の行が見える
      settings_set_symbol(&r.sm, (uint8_t)k); ui_render(&r.ui, &sn, &v, r.t);
      int marks = 0; for (int i = 3; i < v.count; i++) if (v.lines[i].text[0] == '>') { marks++; CHECK(v.lines[i].icon == SYMBOL_CHOICES[k].icon); }
      CHECK(marks == 1 && v.count == UI_MAX_LINES); }
    CHECK(has_line(v, ">    /#  デジピーター") && !has_line(v, "/>  車"));
    // SSID: 0〜15 と用途
    r.ui.menuItem = UI_MENU_SSID; settings_set_ssid(&r.sm, 9); ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "設定 5/15") && has_line(v, ">  9  モービル") && has_line(v, "   7  ハンディ機"));
    settings_set_ssid(&r.sm, 0); ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, ">  0  SSIDなし (固定局)") && !has_line(v, " 15  "));
    settings_set_ssid(&r.sm, 15); ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "> 15  汎用 (実験用)") && has_line(v, "  13  気象局") && !has_line(v, "   0  "));
    settings_set_ssid(&r.sm, DEFAULT_SSID); settings_set_symbol(&r.sm, 0);
    r.ui.menuItem = UI_MENU_MONITOR; settings_set_monitor(&r.sm, 0); ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "設定 8/15") && has_line(v, "モニター音量") && has_line(v, "> 0 ミュート") && has_line(v, "  4 最大"));
    settings_set_monitor(&r.sm, 4); ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "> 4 最大") && has_line(v, "  0 ミュート")); }


  // ---- 調整トーン画面 ----
  { Rig r; r.longp(2); UiSnapshot sn = snap_of(&r.sm); UiView v;
    r.ui.menuItem = UI_MENU_CALIB;
    ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "調整トーン (PTT送信)") && has_line(v, "ダミーロード") && !strcmp(v.keyShort[1], "開く"));
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_CALIB);                  // メニューで B短押し: 画面を開くだけ (送信しない)
    CHECK(r.ui.calibMode == AFSK_TONE_MARK && !r.ui.calibOn);
    ui_render(&r.ui, &sn, &v, r.t);
    CHECK(v.count <= UI_MAX_LINES && has_line(v, "停止中 (PTTオフ)") && has_line(v, "> 1200Hz 連続 (MARK)") && has_line(v, "  2200Hz 連続 (SPACE)") && has_line(v, "交互"));
    CHECK(!strcmp(v.keyShort[0], "前") && !strcmp(v.keyShort[1], "開始") && !strcmp(v.keyShort[2], "次") && !strcmp(v.keyLong[1], "戻る") && !has_line(v, "B長"));
    CHECK(has_line(v, "振幅 MARK") && has_line(v, "自動停止") && has_line(v, "ダミーロード・最小電力"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    // モード切替 (C=次、A=前)
    r.shortp(2); CHECK(r.ui.calibMode == AFSK_TONE_SPACE); ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "> 2200Hz 連続 (SPACE)"));
    r.shortp(2); CHECK(r.ui.calibMode == AFSK_TONE_ALT);   ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "> 1200Hz⇔2200Hz 交互"));
    r.shortp(2); CHECK(r.ui.calibMode == AFSK_TONE_MARK);
    r.shortp(0); CHECK(r.ui.calibMode == AFSK_TONE_ALT);
    r.shortp(0); CHECK(r.ui.calibMode == AFSK_TONE_SPACE);
    r.shortp(0); r.shortp(0); CHECK(r.ui.calibMode == AFSK_TONE_ALT); r.shortp(2);
    // A長(送信)・C長は何もしない (画面もそのまま)
    CHECK(r.longp(0) == UI_ACT_NONE); r.longp(2); CHECK(r.ui.screen == UI_SCREEN_CALIB && r.ui.calibMode == AFSK_TONE_MARK);
    // 開始 (B短押し) は要求を返すだけ。実際の出力開始は loop 側
    CHECK(r.shortp(1) == UI_ACT_CALIB_TOGGLE && !r.ui.calibOn);
    // 出力中: 表示と、どのボタンでも押した瞬間に停止要求
    ui_set_calib_on(&r.ui, true, r.t); sn.ptt = true;
    ui_render(&r.ui, &sn, &v, r.t + 3000);
    CHECK(has_line(v, "PTT ON  送信中  3秒") && has_line(v, "どのボタンでも停止") && v.icons.act == UI_ACTICON_TX && !strcmp(v.keyShort[0], "停止") && !strcmp(v.keyShort[2], "停止"));
    sn.ptt = false; ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "PTT 準備/終了処理中"));
    for (int b = 0; b < 3; b++) {
      bool d[3] = { b == 0, b == 1, b == 2 };
      UiAction a = ui_buttons(&r.ui, &r.sm, d[0], d[1], d[2], r.t);                     // 押した瞬間に停止要求
      CHECK(a == UI_ACT_CALIB_TOGGLE);
      r.t += 20; a = ui_buttons(&r.ui, &r.sm, d[0], d[1], d[2], r.t); CHECK(a == UI_ACT_NONE);   // 押し続けても繰り返さない
      r.t += 2000; a = ui_buttons(&r.ui, &r.sm, d[0], d[1], d[2], r.t); CHECK(a == UI_ACT_NONE && r.ui.screen == UI_SCREEN_CALIB);   // 長押しでも画面は動かない
      r.t += 20; a = ui_buttons(&r.ui, &r.sm, false, false, false, r.t); CHECK(a == UI_ACT_NONE);   // 離しても何も起きない
      r.t += 200; ui_buttons(&r.ui, &r.sm, false, false, false, r.t);
    }
    CHECK(r.ui.calibMode == AFSK_TONE_MARK);                                           // モードも変わらない
    ui_set_calib_on(&r.ui, false, r.t);
    ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "停止中 (PTTオフ)") && v.icons.act == UI_ACTICON_NONE);
    // 戻る (B長押し) でメニューへ。そこから閉じられる
    CHECK(r.longp(1) == UI_ACT_NONE && r.ui.screen == UI_SCREEN_MENU && r.ui.menuItem == UI_MENU_CALIB);
    r.longp(1); CHECK(r.ui.screen == UI_SCREEN_OWN);
    // 出力開始から時間を測る: 開始時刻は最初の on のときだけ記録
    ui_set_calib_on(&r.ui, true, 5000); ui_set_calib_on(&r.ui, true, 9000); CHECK(r.ui.calibSinceMs == 5000);
    ui_set_calib_on(&r.ui, false, 9500); ui_set_calib_on(&r.ui, true, 12000); CHECK(r.ui.calibSinceMs == 12000); }
  // ---- WX(気象)画面 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    auto wxpkt = [](const char* call, bool pos, double t) {
      AprsPacket p; memset(&p, 0, sizeof(p)); snprintf(p.src, sizeof(p.src), "%.9s", call); p.micEMessage = "";
      p.kind = pos ? APRS_KIND_POSITION : APRS_KIND_WEATHER; p.weather.present = true;
      if (pos) { p.hasPos = true; p.latDeg = 35.70; p.lonDeg = 139.80; p.symTable = '/'; p.symCode = '_'; }
      p.weather.hasTemp = true; p.weather.tempC = t; p.weather.hasHumidity = true; p.weather.humidityPct = 62;
      p.weather.hasPressure = true; p.weather.pressureHpa = 1012.3; p.weather.hasWindDir = true; p.weather.windDirDeg = 270;
      p.weather.hasWindSpeed = true; p.weather.windSpeedMs = 3.1; p.weather.hasGust = true; p.weather.gustMs = 5.4;
      p.weather.hasRain1h = true; p.weather.rain1hMm = 0.0; snprintf(p.comment, sizeof(p.comment), "Davis VP2");
      return p; };
    LogTime lt; memset(&lt, 0, sizeof(lt)); lt.valid = true; lt.hour = 12; lt.minute = 34; lt.second = 56;
    r.ui.screen = UI_SCREEN_WX; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "WX(0局)") && has_line(v, "気象データの受信なし") && v.keyShort[1][0] == 0);
    // 気象データのないパケットは入らない
    AprsPacket np; memset(&np, 0, sizeof(np)); snprintf(np.src, sizeof(np.src), "TESTXX"); np.micEMessage = "";
    ui_rx_add(&r.ui, &np, true, 35.68, 139.76, &lt, 1000); uint8_t o[WX_STATION_COUNT]; CHECK(ui_wx_order(&r.ui, o) == 0);
    // 受信すると一覧に入る (ui_rx_add 経由)。距離・方位は受信したときの自局位置から
    AprsPacket a = wxpkt("NOCAL2-13", true, 18.5); ui_rx_add(&r.ui, &a, true, 35.68, 139.76, &lt, 1000);
    CHECK(ui_wx_order(&r.ui, o) == 1 && r.ui.rxCount == 2);
    ui_render(&r.ui, &sn, &v, 61000);
    CHECK(has_line(v, "WX(1局)") && has_line(v, "NOCAL2-13 12:34") && has_line(v, "km 0") && has_line(v, "1分00秒前"));
    CHECK(has_line(v, " 18.5°C  湿度62%  1012.3hPa") && has_line(v, " 風270° 3.1m/s 突風5.4 雨1h 0.0mm") && !strcmp(v.keyShort[1], "詳細"));
    CHECK(v.lines[1].color == UI_COL_HILITE);
    // 同じ局の位置なしレポートは、前の位置を残して値だけ更新
    AprsPacket b = wxpkt("NOCAL2-13", false, 20.0); ui_wx_add(&r.ui, &b, true, 0, 0, &lt, 2000);
    CHECK(ui_wx_order(&r.ui, o) == 1 && r.ui.wx[o[0]].hasPos && r.ui.wx[o[0]].hasDist && r.ui.wx[o[0]].w.tempC == 20.0 && r.ui.wx[o[0]].count == 2);
    // 位置のない局
    AprsPacket c = wxpkt("NOCALL-13", false, 10.0); ui_wx_add(&r.ui, &c, true, 35.68, 139.76, &lt, 3000);
    ui_render(&r.ui, &sn, &v, 3000); CHECK(has_line(v, "位置不明") && has_line(v, "WX(2局)"));
    CHECK(ui_wx_order(&r.ui, o) == 2 && !strcmp(r.ui.wx[o[0]].src, "NOCALL-13"));        // 新しい順
    // 60分を超えたら灰色 (消さない)
    ui_render(&r.ui, &sn, &v, 2000 + WX_STALE_MS); CHECK(v.lines[4].color == UI_COL_DIM && v.lines[1].color == UI_COL_HILITE && has_line(v, "NOCAL2-13"));
    // 詳細: B短で一番新しい局。A/Cで前後の局 (一周)。B長で一覧へ
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.wxDetail && !strcmp(r.ui.wxSel, "NOCALL-13") && r.ui.screen == UI_SCREEN_WX);
    ui_render(&r.ui, &sn, &v, 3000);
    CHECK(has_line(v, "WX詳細 1/2") && has_line(v, "NOCALL-13  12:34:56 UTC") && has_line(v, "位置: 不明") && has_line(v, "気温: 10.0°C"));
    CHECK(has_line(v, "湿度: 62%") && has_line(v, "気圧: 1012.3hPa") && has_line(v, "風向: 270° (西)") && has_line(v, "突風: 5.4m/s"));
    CHECK(has_line(v, "雨量: 1時間 0.0mm  24時間 --") && has_line(v, "0時から --") && has_line(v, "Davis VP2") && v.count <= UI_MAX_LINES);
    CHECK(!strcmp(v.keyShort[0], "前局") && !strcmp(v.keyLong[0], "送信") && v.keyShort[1][0] == 0 && !strcmp(v.keyLong[1], "一覧"));
    r.shortp(2); CHECK(!strcmp(r.ui.wxSel, "NOCAL2-13")); ui_render(&r.ui, &sn, &v, 3000); CHECK(has_line(v, "WX詳細 2/2") && has_line(v, "位置: 35.7000N 139.8000E"));
    r.shortp(2); CHECK(!strcmp(r.ui.wxSel, "NOCALL-13"));
    r.shortp(0); CHECK(!strcmp(r.ui.wxSel, "NOCAL2-13"));
    r.shortp(1); CHECK(r.ui.wxDetail && r.ui.screen == UI_SCREEN_WX);                   // 詳細でB短は何もしない
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(r.longp(0) == UI_ACT_SEND && r.ui.wxDetail);                                    // 詳細からもA長で送信できる
    r.longp(2); CHECK(r.ui.screen == UI_SCREEN_MENU); r.longp(2); CHECK(r.ui.screen == UI_SCREEN_WX && r.ui.wxDetail);   // メニューから戻ると詳細のまま
    r.longp(1); CHECK(!r.ui.wxDetail && r.ui.screen == UI_SCREEN_WX);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_RXSTAT);                                  // 一覧ではA/Cで画面を切り替える
    // いっぱいになったら受信がいちばん古い局と入れ替え。詳細を表示中の局は残す
    Rig q; char call[16];
    for (int i = 0; i < WX_STATION_COUNT; i++) { snprintf(call, sizeof(call), "WX%d", i % 10); AprsPacket p = wxpkt(call, true, i); ui_wx_add(&q.ui, &p, false, 0, 0, NULL, 1000u + i * 100u); }
    CHECK(ui_wx_order(&q.ui, o) == WX_STATION_COUNT && !strcmp(q.ui.wx[o[0]].src, "WX7") && !strcmp(q.ui.wx[o[0]].time, "--:--:--"));
    q.ui.screen = UI_SCREEN_WX; q.ui.wxDetail = true; snprintf(q.ui.wxSel, sizeof(q.ui.wxSel), "WX0");
    { AprsPacket p = wxpkt("NEW1", true, 1); ui_wx_add(&q.ui, &p, false, 0, 0, NULL, 5000); }
    bool has0 = false, has1 = false; for (int i = 0; i < WX_STATION_COUNT; i++) { if (!strcmp(q.ui.wx[i].src, "WX0")) has0 = true; if (!strcmp(q.ui.wx[i].src, "WX1")) has1 = true; }
    CHECK(has0 && !has1 && ui_wx_order(&q.ui, o) == WX_STATION_COUNT);
    q.ui.wxDetail = false; UiSnapshot sq = snap_of(&q.sm); ui_render(&q.ui, &sq, &v, 6000);
    CHECK(has_line(v, "WX(8局)") && v.count <= UI_MAX_LINES && has_line(v, "NEW1"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    // 値がない項目は "--"
    { AprsPacket p; memset(&p, 0, sizeof(p)); snprintf(p.src, sizeof(p.src), "EMPTY"); p.weather.present = true; p.micEMessage = "";
      Rig e; ui_wx_add(&e.ui, &p, false, 0, 0, NULL, 0); e.ui.screen = UI_SCREEN_WX; UiSnapshot se = snap_of(&e.sm); ui_render(&e.ui, &se, &v, 0);
      CHECK(has_line(v, " --  湿度--  --") && has_line(v, " 風--- -- 突風-- 雨1h --"));
      e.shortp(1); ui_render(&e.ui, &se, &v, 0); CHECK(has_line(v, "風向: --") && !has_line(v, "日射")); } }

  // ---- メッセージ画面 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v; static MsgCore mc; msg_init(&mc, 0); ui_set_msgs(&r.ui, &mc);
    auto rxp = [](const char* src, const char* info) {
      AprsPacket p; memset(&p, 0, sizeof(p)); snprintf(p.src, sizeof(p.src), "%s", src); p.micEMessage = "";
      uint8_t f[AX25_FRAME_MAX]; Ax25Params ap = { "N0CALL", 7, "APRS", 0, "" };
      size_t n = ax25_build_ui_frame(ap, (const uint8_t*)info, strlen(info), f, sizeof(f)); aprs_parse_frame(f, n - 2, &p);
      snprintf(p.src, sizeof(p.src), "%s", src); return p; };
    auto all_fit = [&](const UiView& vv) { bool ok = vv.count <= UI_MAX_LINES; for (int i = 0; i < vv.count; i++) if (line_cols(vv.lines[i].text) > UI_COLS) ok = false; return ok; };
    r.ui.screen = UI_SCREEN_MSG; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "メッセージ(0)") && has_line(v, "メッセージはありません") && !strcmp(v.keyShort[1], "作成"));
    // 新規作成: B短で「新規」→ B短で作成画面
    r.shortp(1); CHECK(r.ui.msgDetail && r.ui.msgSel == 0); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "新しいメッセージを作成") && !strcmp(v.keyShort[1], "作成") && !strcmp(v.keyLong[1], "一覧") && has_line(v, "キーボード(CardKB2)でも入力できます"));
    r.shortp(1); CHECK(r.ui.cmp.active && r.ui.cmp.step == UI_CMP_TO);
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "1/3 宛先を選ぶ") && has_line(v, "候補がありません") && all_fit(v));
    CHECK(r.shortp(1) == UI_ACT_NONE && r.ui.cmp.step == UI_CMP_TO);                       // 候補がなければ進まない
    CHECK(r.longp(0) == UI_ACT_NONE && r.ui.cmp.active);                                     // 作成中はA長(送信)・C長(メニュー)を使わない
    r.longp(2); CHECK(r.ui.screen == UI_SCREEN_MSG && r.ui.cmp.active);
    r.longp(1); CHECK(!r.ui.cmp.active && r.ui.msgDetail);                                     // B長で作成をやめる
    // 受信した局が宛先の候補になる (新しい順、重複なし)
    LogTime lt; memset(&lt, 0, sizeof(lt));
    AprsPacket p1 = rxp("N0CALL-7", "!3540.00N/13945.00E>"); ui_rx_add(&r.ui, &p1, false, 0, 0, &lt, 100);
    AprsPacket p2 = rxp("NOCAL2-5", "!3540.00N/13945.00E>"); ui_rx_add(&r.ui, &p2, false, 0, 0, &lt, 200);
    ui_rx_add(&r.ui, &p1, false, 0, 0, &lt, 300);
    r.shortp(1); CHECK(r.ui.cmp.active);
    char items[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1];
    CHECK(ui_compose_items(&r.ui, items) == 2 && !strcmp(items[0], "N0CALL-7") && !strcmp(items[1], "NOCAL2-5"));
    // 今の自局は候補に出さない (デジピーターで折り返した自分の送信)。同じコールサインの別SSIDは出す
    { UiState u2 = r.ui; ui_set_my_call(&u2, "NOCAL2-5"); CHECK(ui_compose_items(&u2, items) == 1 && !strcmp(items[0], "N0CALL-7"));
      ui_set_my_call(&u2, "nocal2-9"); CHECK(ui_compose_items(&u2, items) == 2);
      ui_set_my_call(&u2, "N0CALL-7"); snprintf(u2.cmp.replyTo, sizeof(u2.cmp.replyTo), "N0CALL-7"); CHECK(ui_compose_items(&u2, items) == 1 && !strcmp(items[0], "NOCAL2-5"));
      snprintf(u2.cmp.input, sizeof(u2.cmp.input), "N0CALL-7"); CHECK(ui_compose_items(&u2, items) == 2 && !strcmp(items[0], "N0CALL-7")); }   // 打ったものはそのまま
    r.shortp(2); CHECK(r.ui.cmp.sel == 1); r.shortp(2); CHECK(r.ui.cmp.sel == 0); r.shortp(0); CHECK(r.ui.cmp.sel == 1);
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "> NOCAL2-5") && has_line(v, "  N0CALL-7"));
    r.shortp(1); CHECK(r.ui.cmp.step == UI_CMP_TEXT && !strcmp(r.ui.cmp.to, "NOCAL2-5") && r.ui.cmp.sel == 0);
    CHECK(ui_compose_items(&r.ui, items) == MSG_PRESET_COUNT && !strcmp(items[0], "QSL"));
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "宛先: NOCAL2-5") && has_line(v, "2/3 本文を選ぶ") && has_line(v, "> QSL") && all_fit(v));
    r.shortp(2); r.shortp(1); CHECK(r.ui.cmp.step == UI_CMP_CONFIRM && !strcmp(r.ui.cmp.text, "73"));
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "宛先: NOCAL2-5") && has_line(v, "本文: (2/67文字)") && has_line(v, "73") && !strcmp(v.keyShort[1], "送信") && !strcmp(v.keyLong[1], "戻る") && v.keyShort[0][0] == 0 && all_fit(v));
    r.shortp(0); r.shortp(2); CHECK(r.ui.cmp.step == UI_CMP_CONFIRM);                       // 確認ではA/Cは何もしない
    r.longp(1); CHECK(r.ui.cmp.step == UI_CMP_TEXT); r.shortp(1); CHECK(r.ui.cmp.step == UI_CMP_CONFIRM && !strcmp(r.ui.cmp.text, "QSL"));
    CHECK(r.shortp(1) == UI_ACT_MSG_SEND);                                                     // B短で送信の要求
    int idx = msg_send(&mc, r.ui.cmp.to, r.ui.cmp.text, 1000, NULL); ui_compose_done(&r.ui, idx);
    CHECK(!r.ui.cmp.active && r.ui.msgDetail && r.ui.msgSel == mc.e[idx].seq);
    ui_render(&r.ui, &sn, &v, 1000); CHECK(has_line(v, "メッセージ 1/1") && has_line(v, "To:   NOCAL2-5") && has_line(v, "状態: 送信待ち") && !strcmp(v.keyShort[1], "再送") && all_fit(v));
    mc.e[idx].sends = 6; mc.e[idx].state = MSG_ST_FAILED; ui_render(&r.ui, &sn, &v, 1000); CHECK(has_line(v, "応答なし") && has_line(v, "6/6回"));
    r.shortp(1); CHECK(r.ui.cmp.active && r.ui.cmp.step == UI_CMP_CONFIRM && !strcmp(r.ui.cmp.to, "NOCAL2-5") && !strcmp(r.ui.cmp.text, "QSL"));   // もう一度送る
    r.longp(1); r.longp(1); r.longp(1); CHECK(!r.ui.cmp.active);
    // 受信: 未読の表示、詳細で既読、返信
    AprsPacket m1 = rxp("N0CALL-7", ":NOCALL-9 :Hello from N0CALL{5");
    CHECK(msg_on_packet(&mc, &m1, "NOCALL-9", 2000, NULL) == MSG_RX_NEW);
    r.longp(1); CHECK(!r.ui.msgDetail);
    ui_render(&r.ui, &sn, &v, 2000); CHECK(v.icons.unread && has_line(v, "受 --:-- N0CALL-7  *未読") && has_line(v, "  Hello from N0CALL") && has_line(v, "メッセージ(2)") && !strcmp(v.keyShort[1], "詳細"));
    r.ui.screen = UI_SCREEN_OWN; ui_render(&r.ui, &sn, &v, 2000); CHECK(v.icons.unread); r.ui.screen = UI_SCREEN_MSG;   // どの画面でも見える
    r.shortp(1); CHECK(r.ui.msgDetail && msg_unread(&mc) == 0);                               // いちばん新しいものを開くと既読
    ui_render(&r.ui, &sn, &v, 2000); CHECK(has_line(v, "メッセージ 1/2") && has_line(v, "From: N0CALL-7") && has_line(v, "番号: 5  (Ackを返します)") && !strcmp(v.keyShort[1], "返信") && !v.icons.unread);
    r.shortp(2); CHECK(r.ui.msgSel == mc.e[idx].seq); r.shortp(2); CHECK(r.ui.msgSel == 0); r.shortp(2); CHECK(r.ui.msgSel == mc.e[msg_find_seq(&mc, r.ui.msgSel)].seq && r.ui.msgSel != 0);
    r.shortp(1); CHECK(r.ui.cmp.active && r.ui.cmp.step == UI_CMP_TEXT && !strcmp(r.ui.cmp.to, "N0CALL-7"));   // 返信: 宛先は相手
    r.longp(1); CHECK(r.ui.cmp.step == UI_CMP_TO); CHECK(ui_compose_items(&r.ui, items) >= 1 && !strcmp(items[0], "N0CALL-7"));   // 戻ると返信の相手が先頭
    // キーボード: 宛先の入力
    for (const char* k = "testab-7"; *k; k++) ui_key(&r.ui, *k, 0);
    CHECK(!strcmp(r.ui.cmp.input, "TESTAB-7") && r.ui.cmp.sel == 0);
    ui_key(&r.ui, '!', 0); ui_key(&r.ui, 'x', 0); ui_key(&r.ui, 'y', 0); CHECK(!strcmp(r.ui.cmp.input, "TESTAB-7X"));   // 使えない文字・10文字目は入らない
    ui_key(&r.ui, 0x08, 0); CHECK(!strcmp(r.ui.cmp.input, "TESTAB-7"));
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "> 入力: TESTAB-7_") && all_fit(v));
    ui_key(&r.ui, '\r', 0); CHECK(r.ui.cmp.step == UI_CMP_TEXT && !strcmp(r.ui.cmp.to, "TESTAB-7") && r.ui.cmp.input[0] == 0);
    // キーボード: 本文の入力と、定型文に書き足し
    for (const char* k = "Hi {there}"; *k; k++) ui_key(&r.ui, *k, 0);
    CHECK(!strcmp(r.ui.cmp.input, "Hi ?there}"));
    ui_key(&r.ui, '\n', 0); CHECK(r.ui.cmp.step == UI_CMP_CONFIRM && !strcmp(r.ui.cmp.text, "Hi ?there}"));
    ui_key(&r.ui, 0x1B, 0); CHECK(r.ui.cmp.step == UI_CMP_TEXT);                                 // Esc で戻る
    r.shortp(1); CHECK(!strcmp(r.ui.cmp.text, "QSL"));
    for (const char* k = " 73"; *k; k++) ui_key(&r.ui, *k, 0);
    CHECK(!strcmp(r.ui.cmp.text, "QSL 73")); ui_key(&r.ui, 0x7F, 0); CHECK(!strcmp(r.ui.cmp.text, "QSL 7"));
    ui_key(&r.ui, (char)0xB5, 0); CHECK(!strcmp(r.ui.cmp.text, "QSL 7"));                         // 矢印キーなどは無視
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "QSL 7_") && !has_line(v, "Enter:送信") && all_fit(v));
    CHECK(ui_key(&r.ui, '\r', 0) == UI_ACT_MSG_SEND);
    for (int i = 0; i < 70; i++) { ui_key(&r.ui, 'z', 0); }
    CHECK(strlen(r.ui.cmp.text) == APRS_MSG_TEXT_MAX);
    ui_render(&r.ui, &sn, &v, 0); CHECK(all_fit(v));
    // 作成中でなければキーは無視
    r.longp(1); r.longp(1); r.longp(1); CHECK(!r.ui.cmp.active); CHECK(ui_key(&r.ui, 'a', 0) == UI_ACT_NONE);
    // 一斉通知・別SSID宛て・たくさんの件数でも画面に収まる
    AprsPacket b1 = rxp("N0CALL", ":BLN1     :Net tonight at 21:00 on 433.00"); msg_on_packet(&mc, &b1, "NOCALL-9", 3000, NULL);
    AprsPacket o1 = rxp("N0CALL-7", ":NOCALL-5 :to other ssid"); msg_on_packet(&mc, &o1, "NOCALL-9", 3000, NULL);
    for (int i = 0; i < 25; i++) { char inf[100]; snprintf(inf, sizeof(inf), ":NOCALL-9 :message number %d with a long long text body here{%d", i, i + 10); AprsPacket q = rxp("N0CALL-7", inf); msg_on_packet(&mc, &q, "NOCALL-9", 4000 + i, NULL); }
    r.longp(1); ui_render(&r.ui, &sn, &v, 5000); CHECK(all_fit(v) && v.icons.unread);
    r.shortp(1); for (int i = 0; i < MSG_STORE_COUNT + 2; i++) { ui_render(&r.ui, &sn, &v, 5000); CHECK(all_fit(v)); r.shortp(2); }
    CHECK(msg_unread(&mc) == 0); }

  // ---- 自動送信の表示 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.autoTx == UI_AUTOICON_OFF && has_line(v, "送信内容:") && !has_line(v, "次の自動送信"));
    sn.autoTx = AUTO_TX_ACK_ONLY; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.autoTx == UI_AUTOICON_ACK && !has_line(v, "次の自動送信"));
    settings_set_auto_tx(&r.sm, 4); sn.autoTx = 4; sn.autoRemainMs = 155000; ui_render(&r.ui, &sn, &v, 0);   // 4 = 3分
    CHECK(v.icons.autoTx == UI_AUTOICON_ON && has_line(v, "次の自動送信まで 2:35") && v.count <= UI_MAX_LINES);
    sn.autoRemainMs = 1; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "次の自動送信まで 0:01"));
    sn.autoWaiting = true; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "自動送信は他局の送信待ち"));
    sn.autoWaiting = false; sn.rxCarrier = true; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.autoTx == UI_AUTOICON_ON && v.icons.act == UI_ACTICON_RX);
    // 長い見出しでも画面名の領域に収まる
    sn.autoTx = 9; settings_set_auto_tx(&r.sm, 9); ui_set_tx_busy(&r.ui, true); sn.ptt = true; r.ui.screen = UI_SCREEN_RX; r.ui.rxTotal = 9999;
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "受信一覧(計9999)") && line_cols(v.lines[0].text) <= UI_TITLE_COLS && v.icons.act == UI_ACTICON_TX);
    ui_set_tx_busy(&r.ui, false); sn.ptt = false; sn.rxCarrier = false;
    // 送信・設定画面
    r.ui.screen = UI_SCREEN_TXSTAT; sn.autoRemainMs = 3599000; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "自動送信: 60分 (次 59:59)") && has_line(v, "進行方向・速度: オフ  高度: オフ") && v.count <= UI_MAX_LINES);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    settings_set_auto_tx(&r.sm, AUTO_TX_ACK_ONLY); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "自動送信: Ackのみ"));
    settings_set_auto_tx(&r.sm, AUTO_TX_OFF); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "自動送信: オフ"));
    ui_set_tx_busy(&r.ui, true); sn.pttTimeouts = 1; ui_render(&r.ui, &sn, &v, 0); CHECK(v.count <= UI_MAX_LINES && has_line(v, "稼働")); ui_set_tx_busy(&r.ui, false);
    // メニュー: 選択肢は11個で入りきらないので、選択中の前後が見える
    r.ui.screen = UI_SCREEN_MENU; r.ui.menuItem = UI_MENU_AUTOTX; settings_set_auto_tx(&r.sm, 6); ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "ビーコン自動送信") && has_line(v, "  Ackのみ") && has_line(v, "  1分ごと") && has_line(v, "> 10分ごと") && has_line(v, "  60分ごと") && has_line(v, "  フレックス"));
    settings_set_auto_tx(&r.sm, AUTO_TX_OFF); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "> オフ") && has_line(v, "  60分ごと") && v.count == UI_MAX_LINES);
    settings_set_auto_tx(&r.sm, AUTO_TX_FLEX); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "> フレックス") && !has_line(v, "  オフ") && v.count == UI_MAX_LINES);
    settings_set_auto_tx(&r.sm, 6); ui_render(&r.ui, &sn, &v, 0);
    CHECK(v.count <= UI_MAX_LINES && !has_line(v, "B:") && !strcmp(v.keyShort[1], "変更")); for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS); }

  // ---- バージョン情報 ----
  { Rig r; r.longp(2); UiSnapshot sn = snap_of(&r.sm); UiView v;
    snprintf(sn.sdkVersion, sizeof(sn.sdkVersion), "v4.4.7"); sn.chipRev = 3; sn.freeHeap = 201234;
    r.ui.menuItem = UI_MENU_VERSION; ui_render(&r.ui, &sn, &v, r.t);
    CHECK(has_line(v, "バージョン情報") && has_line(v, "設定 15/15") && has_line(v, "M5APRS  ver " FW_VERSION) && has_line(v, "Copyright (c) 2026 Minato Suzuki"));
    CHECK(has_line(v, "ビルド: ") && has_line(v, "対応基板: " HW_INTERFACE_REV) && has_line(v, "コールサイン: " MY_CALLSIGN));
    CHECK(has_line(v, "SDK: v4.4.7") && has_line(v, "チップ rev3  空きメモリ 201234B") && !has_line(v, "B長") && v.keyShort[1][0] == 0 && has_line(v, "SD: 正常"));
    CHECK(v.count <= UI_MAX_LINES); for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    sn.sdkVersion[0] = 0; ui_render(&r.ui, &sn, &v, r.t); CHECK(has_line(v, "SDK: ---"));
    r.longp(1); CHECK(r.ui.screen == UI_SCREEN_OWN); }
  { char d[16];
    ui_build_date(d, sizeof(d), "Oct 18 2026"); CHECK(!strcmp(d, "2026-10-18"));
    ui_build_date(d, sizeof(d), "Jan  5 2027"); CHECK(!strcmp(d, "2027-01-05"));
    ui_build_date(d, sizeof(d), "Dec 31 2030"); CHECK(!strcmp(d, "2030-12-31"));
    ui_build_date(d, sizeof(d), "???"); CHECK(!strcmp(d, "???"));
    ui_build_date(d, sizeof(d), __DATE__); CHECK(strlen(d) == 10 && d[4] == '-' && d[7] == '-'); }

  // 通常の送信中は、調整トーンの画面へ行けない (メニューが開かない)
  { Rig r; ui_set_tx_busy(&r.ui, true); r.longp(2); CHECK(r.ui.screen == UI_SCREEN_OWN); }

  // ---- ヘッダのアイコン ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    const int lv[] = { -1, 0, 10, 25, 50, 75, 100 }; const int8_t want[] = { -1, 0, 0, 1, 2, 3, 4 };
    for (int i = 0; i < 7; i++) { sn.battLevel = (int8_t)lv[i]; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.batt == want[i]); }
    sn.battCharging = true; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.charging);
    UiView w = v; CHECK(ui_icons_equal(&v.icons, &w.icons)); w.icons.unread = 1; CHECK(!ui_icons_equal(&v.icons, &w.icons));
    // 位置: GPS以外の取得元では、GPSの状態ではなく取得元を出す
    sn.gps = GPS_NO_DATA; sn.posSource = POSITION_SOURCE_FIXED; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.pos == UI_POSICON_FIXED);
    sn.posSource = POSITION_SOURCE_LAST; ui_render(&r.ui, &sn, &v, 0); CHECK(v.icons.pos == UI_POSICON_LAST);
    // 自局画面: 位置は1行。見出しに「n/6」は出さない
    sn.posSource = POSITION_SOURCE_GPS; sn.gps = GPS_FIX; sn.hasPos = true; sn.latDeg = 35.68123; sn.lonDeg = -139.76712;
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "位置 35.68123N 139.76712W") && !strcmp(v.lines[0].text, "自局")); }

  // ---- 本文に操作の説明を出さない、どの画面も13行・38桁に収まる ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v; static MsgCore mc2; msg_init(&mc2, 0); ui_set_msgs(&r.ui, &mc2);
    sn.previewValid = true; snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s", MY_CALLSIGN "-9>APRS,WIDE1-1:");
    snprintf(sn.previewInfo, sizeof(sn.previewInfo), "%s", "!3540.87N/13946.03E>090/012/A=000123 M5APRS long comment text here for wrap");
    ui_show_message(&r.ui, "test", 0, 5000);
    auto clean = [&]() {
      bool ok = v.count <= UI_MAX_LINES && line_cols(v.lines[0].text) <= UI_TITLE_COLS;
      for (int i = 0; i < v.count; i++) {
        if (line_cols(v.lines[i].text) > UI_COLS) ok = false;
        if (strstr(v.lines[i].text, "A/C") || strstr(v.lines[i].text, "B長") || strstr(v.lines[i].text, "B:") || strstr(v.lines[i].text, "長押し") || strstr(v.lines[i].text, "切替")) ok = false;
      }
      return ok; };
    for (int s = 0; s < UI_SCREEN_COUNT; s++) { r.ui.screen = (uint8_t)s; ui_render(&r.ui, &sn, &v, 0); CHECK(clean()); }
    r.ui.screen = UI_SCREEN_MENU; for (int i = 0; i < UI_MENU_COUNT; i++) { r.ui.menuItem = (uint8_t)i; ui_render(&r.ui, &sn, &v, 0); CHECK(clean()); }
    r.ui.screen = UI_SCREEN_CALIB; ui_render(&r.ui, &sn, &v, 0); CHECK(clean());
    r.ui.screen = UI_SCREEN_MSG; r.shortp(1); ui_render(&r.ui, &sn, &v, 0); CHECK(clean());
    r.shortp(1); ui_render(&r.ui, &sn, &v, 0); CHECK(clean() && r.ui.cmp.active);
    r.ui.cmp.step = UI_CMP_TEXT; snprintf(r.ui.cmp.to, sizeof(r.ui.cmp.to), "N0CALL"); ui_render(&r.ui, &sn, &v, 0); CHECK(clean());
    r.ui.cmp.step = UI_CMP_CONFIRM; snprintf(r.ui.cmp.text, sizeof(r.ui.cmp.text), "73"); ui_render(&r.ui, &sn, &v, 0); CHECK(clean());
    // 送信中: ボタンは全部「機能なし」
    ui_set_tx_busy(&r.ui, true); ui_render(&r.ui, &sn, &v, 0);
    for (int k = 0; k < 3; k++) CHECK(v.keyShort[k][0] == 0 && v.keyLong[k][0] == 0); }

  // ---- 受信一覧: 種類ごとの表示と、詳細画面 (10月25日の受信ログから) ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v; static MsgCore mc3; msg_init(&mc3, 0); ui_set_msgs(&r.ui, &mc3);
    ui_set_my_call(&r.ui, "NOCALL-9");
    auto pk = [](const char* src, int ssid, const char* dest, const char* path, const char* info, bool used) {
      AprsPacket p; uint8_t f[AX25_FRAME_MAX]; Ax25Params ap = { src, (uint8_t)ssid, dest, 0, path };
      size_t n = ax25_build_ui_frame(ap, (const uint8_t*)info, strlen(info), f, sizeof(f));
      if (used && path[0]) f[20] |= 0x80;
      aprs_parse_frame(f, n - 2, &p); return p; };
    LogTime lt = { true, 2026, 10, 25, 18, 0, 0 };
    auto at = [&](int h, int m, int sec) { lt.hour = (uint8_t)h; lt.minute = (uint8_t)m; lt.second = (uint8_t)sec; return &lt; };
    AprsPacket ps[] = {
      pk("TEST2", 9, "STQRP0", "DIGI2-3", "`C0 l#O>/`\"4$}BLUE VAN FTM300D 25W MOBILE TEST BEACON ONAIR!!_1\r", true),
      pk("DIGI2", 3, "UIDIGI", "", "UIDIGI 1.9", false),
      pk("IGATE", 10, "APU25N", "DIGI2-3", "}TEST7-10>APRX29,TCPIP,IGATE-10*::TEST7-10:UNIT.Rx Erlang,Tx Erlang,count/10m,count/10m,count/10m\r", true),
      pk("TEST3", 3, "APTT4", "DIGI1-1,WIDE2-1", "T#262,120,089,255,085,087,00000011", true),
      pk("TEST4", 1, "STQSP5", "WIDE1-1", "'C0 l \x1c#1]144.66MHz 1200bps Test Digi=\r", false),
      pk("TEST5", 8, "APK005", "DIGI2-3", ":IGATE-10 :Hello from the test! See you{61\r", true),
      pk("IGATE", 10, "APU25N", "DIGI2-3", ">120848zTNC TEST 1.2k9.6kbps status\r", true),
      pk("IGATE", 10, "APU25N", "DIGI2-3", "<IGATE,MSG_CNT=201,LOC_CNT=5\r", true),
      pk("TEST6", 10, "APJYC1", "DIGI2-3", ">\r", true) };
    int hm[][3] = { {15,11,58}, {17,43,45}, {17,57,35}, {18,3,3}, {18,24,37}, {18,25,0}, {18,29,35}, {18,29,50}, {18,30,0} };
    for (int i = 0; i < 9; i++) ui_rx_add(&r.ui, &ps[i], true, 34.0, 139.0, at(hm[i][0], hm[i][1], hm[i][2]), 1000u + (uint32_t)i * 1000u);
    CHECK(r.ui.rxCount == 8 && r.ui.rxTotal == 8);                                      // テレメトリは入れない
    r.ui.screen = UI_SCREEN_RX; ui_render(&r.ui, &sn, &v, 10000);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    CHECK(has_line(v, "18:30:00 TEST6-10  ステータス") && has_line(v, "  (空)"));
    CHECK(has_line(v, "18:29:50 IGATE-10  機能") && has_line(v, "  IGATE,MSG_CNT=201,LOC_CNT=5"));
    CHECK(has_line(v, "18:29:35 IGATE-10  ステータス") && has_line(v, "  TNC TEST 1.2k9.6kbps status"));
    CHECK(has_line(v, "18:25:00 TEST5-8   メッセージ") && has_line(v, "  →IGATE-10 Hello from the test!"));
    CHECK(has_line(v, "18:24:37 TEST4-1  ") && has_line(v, "  144.66MHz 1200bps Test Digi") && !has_line(v, "Digi="));
    CHECK(has_line(v, "17:57:35 IGATE-10  iGate経由") && !has_line(v, "その他"));      // 6件目まで
    CHECK(!has_line(v, "T#262") && !strcmp(v.keyShort[1], "詳細"));
    // 詳細: B短で一番新しいもの。A/Cで前後 (一周)
    r.shortp(1); CHECK(r.ui.rxDetail && r.ui.rxSel == 8);
    ui_render(&r.ui, &sn, &v, 12000);
    CHECK(has_line(v, "受信詳細 1/8") && has_line(v, "TEST6-10 18:30:00Z 3秒前") && has_line(v, "種類: ステータス") && has_line(v, "経路: DIGI2-3*") && has_line(v, "(空)"));
    CHECK(!strcmp(v.keyShort[0], "前") && !strcmp(v.keyShort[1], "作成") && !strcmp(v.keyLong[1], "一覧") && !strcmp(v.keyLong[0], "送信") && !strcmp(v.keyLong[2], "設定"));
    r.shortp(0); CHECK(r.ui.rxSel == 1);                                                   // 前 = 一番古いものへ一周
    ui_render(&r.ui, &sn, &v, 12000);
    for (int i = 0; i < v.count; i++) { CHECK(line_cols(v.lines[i].text) <= UI_COLS); }
    CHECK(v.count <= UI_MAX_LINES && has_line(v, "受信詳細 8/8") && has_line(v, "TEST2-9 15:11:58Z") && has_line(v, "種類: Mic-E  Off Duty"));
    CHECK(has_line(v, "機種: FTM-300D") && has_line(v, "位置: 34.20000N 139.33400E") && has_line(v, "方位: 05"));
    CHECK(has_line(v, "速度: 0km/h  進路: 351°") && has_line(v, "高度: 13m") && has_line(v, "シンボル: />") && has_line(v, "経路: DIGI2-3*"));
    CHECK(has_line(v, "コメント:") && has_line(v, "  BLUE VAN FTM300D 25W MOBILE TEST BEA") && has_line(v, "  CON ONAIR!!"));
    r.shortp(2); CHECK(r.ui.rxSel == 8);
    r.shortp(2); r.shortp(2); ui_render(&r.ui, &sn, &v, 12000);                            // 機能 → ステータス(時刻付き)
    CHECK(has_line(v, "受信詳細 3/8") && has_line(v, "種類: ステータス") && has_line(v, "TNC TEST 1.2k9.6kbps status"));
    r.shortp(2); ui_render(&r.ui, &sn, &v, 12000);
    CHECK(has_line(v, "種類: メッセージ") && has_line(v, "宛先: IGATE-10") && has_line(v, "Hello from the test! See you"));
    r.shortp(2); r.shortp(2); ui_render(&r.ui, &sn, &v, 12000);
    CHECK(has_line(v, "種類: iGate経由") && has_line(v, "TEST7-10>APRX29,TCPIP,IGATE-10*::TEST7") && v.count <= UI_MAX_LINES);
    r.shortp(2); ui_render(&r.ui, &sn, &v, 12000);
    CHECK(has_line(v, "種類: その他") && has_line(v, "経路: (直接)") && has_line(v, "UIDIGI 1.9"));
    r.shortp(0); r.shortp(0); ui_render(&r.ui, &sn, &v, 12000);                        // Mic-E (Kenwood)
    CHECK(has_line(v, "TEST4-1") && has_line(v, "機種: TM-D710") && has_line(v, "距離: "));
    // B短: その局へメッセージを作る (宛先を入れて本文の選択から)
    r.shortp(1); CHECK(r.ui.screen == UI_SCREEN_MSG && r.ui.cmp.active && r.ui.cmp.step == UI_CMP_TEXT && !strcmp(r.ui.cmp.to, "TEST4-1"));
    r.longp(1); CHECK(r.ui.cmp.step == UI_CMP_TO); r.longp(1); CHECK(!r.ui.cmp.active && r.ui.screen == UI_SCREEN_MSG);
    // 自局 (折り返し) の詳細では作成しない
    r.ui.screen = UI_SCREEN_RX;
    AprsPacket me = pk("NOCALL", 9, "APRS", "DIGI1-1", "=341 .  N/1393 .  E>/A=000050M5Stack", true);
    ui_rx_add(&r.ui, &me, true, 34.0, 139.0, at(18, 31, 0), 20000);
    r.ui.rxSel = me.src[0] ? r.ui.rxTotal : 0; ui_render(&r.ui, &sn, &v, 21000);
    CHECK(has_line(v, "NOCALL-9") && v.keyShort[1][0] == 0); r.shortp(1); CHECK(r.ui.screen == UI_SCREEN_RX && !r.ui.cmp.active);
    // B長で一覧へ
    r.longp(1); CHECK(!r.ui.rxDetail && r.ui.screen == UI_SCREEN_RX);
    // 一覧から押し出された1件を表示中
    r.ui.rxDetail = true; r.ui.rxSel = 1;
    for (int i = 0; i < RX_LIST_COUNT; i++) ui_rx_add(&r.ui, &ps[1], true, 34.0, 139.0, at(19, 0, 0), 30000);
    ui_render(&r.ui, &sn, &v, 31000); CHECK(has_line(v, "この受信は一覧から消えました") && v.keyShort[1][0] == 0);
    r.shortp(2); CHECK(r.ui.rxSel == r.ui.rx[(r.ui.rxHead + RX_LIST_COUNT - 1) % RX_LIST_COUNT].seq); }

  // ---- 英語表示: どの画面も13行・38桁・見出し23桁・ボタン13桁に収まり、日本語が残らない ----
  { Rig r; settings_set_language(&r.sm, UI_LANG_EN); ui_set_lang(UI_LANG_EN);
    UiSnapshot sn = snap_of(&r.sm); UiView v; static MsgCore mc4; msg_init(&mc4, 0); ui_set_msgs(&r.ui, &mc4);
    ui_set_my_call(&r.ui, "NOCALL-9");
    sn.previewValid = true; snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s", "NOCALL-9>APRS,WIDE1-1:");
    snprintf(sn.previewInfo, sizeof(sn.previewInfo), "%s", "!3540.87N/13946.03E>090/012/A=000123 M5APRS");
    auto pk = [](const char* src, int ssid, const char* dest, const char* path, const char* info) {
      AprsPacket p; uint8_t f[AX25_FRAME_MAX]; Ax25Params ap = { src, (uint8_t)ssid, dest, 0, path };
      size_t n = ax25_build_ui_frame(ap, (const uint8_t*)info, strlen(info), f, sizeof(f));
      aprs_parse_frame(f, n - 2, &p); return p; };
    LogTime lt = { true, 2026, 10, 25, 18, 0, 0 };
    AprsPacket ps[] = {
      pk("TEST2", 9, "STQRP0", "DIGI2-3", "`C0 l#O>/`\"4$}BLUE VAN 25W MOBILE_1\r"),
      pk("DIGI2", 3, "UIDIGI", "", "UIDIGI 1.9"),
      pk("IGATE", 10, "APU25N", "DIGI2-3", "}TEST7-10>APRX29,TCPIP,IGATE-10*::TEST7-10:UNIT.Rx Erlang\r"),
      pk("TEST4", 1, "STQSP5", "WIDE1-1", "'C0 l \x1c#1]144.66MHz Test Digi=\r"),
      pk("TEST5", 8, "APK005", "DIGI2-3", ":NOCALL-9 :Hello from the test!{61\r"),
      pk("TEST5", 8, "APK005", "DIGI2-3", ":IGATE-10 :Hello{62\r"),
      pk("IGATE", 10, "APU25N", "DIGI2-3", ">120848zTNC TEST status\r"),
      pk("IGATE", 10, "APU25N", "DIGI2-3", "<IGATE,MSG_CNT=201\r"),
      pk("TEST8", 0, "APRS", "", "@092345z3541.00N/13946.00E_090/005g010t068r000p000h62b10123Davis\r"),
      pk("TEST9", 0, "APRS", "", ";OBJ1     *092345z3541.00N/13946.00E>obj\r") };
    for (size_t i = 0; i < sizeof(ps) / sizeof(ps[0]); i++) {
      ui_rx_add(&r.ui, &ps[i], true, 35.68, 139.76, &lt, 1000u + (uint32_t)i * 1000u);
      ui_wx_add(&r.ui, &ps[i], true, 35.68, 139.76, &lt, 1000u + (uint32_t)i * 1000u);
      msg_on_packet(&mc4, &ps[i], "NOCALL-9", 1000u + (uint32_t)i * 1000u, &lt); }
    msg_send(&mc4, "TEST5-8", "73", 20000, &lt);
    ui_show_message(&r.ui, L("位置不明のため送信しません"), 0, 5000);
    auto ascii_only = [](const char* t) { for (const unsigned char* c = (const unsigned char*)t; *c; c++) if (*c >= 0x80) return false; return true; };
    int bad = 0;
    auto clean = [&](const char* where) {
      bool ok = v.count <= UI_MAX_LINES && line_cols(v.lines[0].text) <= UI_TITLE_COLS;
      for (int i = 0; i < v.count; i++) {
        const char* t = v.lines[i].text;
        if (line_cols(t) > UI_COLS) ok = false;
        if (!ascii_only(t) && !strstr(t, "日本語") && !strstr(t, "言語 / Language") && !strstr(t, "°") && !strstr(t, "→")) ok = false;
      }
      for (int k = 0; k < 3; k++) {
        char kb[64]; snprintf(kb, sizeof(kb), "%s", v.keyShort[k]); if (ui_fit(kb, 100) > 13 || !ascii_only(kb)) ok = false;
        snprintf(kb, sizeof(kb), "%s%s", K("長:"), v.keyLong[k][0] ? v.keyLong[k] : "-"); if (ui_fit(kb, 100) > 13 || !ascii_only(kb)) ok = false;
      }
      if (!ok && bad++ < 5) { printf("EN NG at %s:\n", where); for (int i = 0; i < v.count; i++) printf("  |%s|\n", v.lines[i].text);
        for (int k = 0; k < 3; k++) printf("  [%s / %s]\n", v.keyShort[k], v.keyLong[k]); }
      return ok; };
    for (int s = 0; s < UI_SCREEN_COUNT; s++) { r.ui.screen = (uint8_t)s; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("screen")); }
    r.ui.screen = UI_SCREEN_RX; r.shortp(1); CHECK(r.ui.rxDetail);
    for (int i = 0; i < RX_LIST_COUNT; i++) { ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("rx detail")); r.shortp(2); }
    r.longp(1);
    r.ui.screen = UI_SCREEN_WX; r.shortp(1);
    for (int i = 0; i < WX_STATION_COUNT; i++) { ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("wx detail")); r.shortp(2); }
    r.longp(1);
    r.ui.screen = UI_SCREEN_MSG; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("msg"));
    r.shortp(1); for (int i = 0; i < 4; i++) { ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("msg detail")); r.shortp(2); }
    r.longp(1); r.ui.screen = UI_SCREEN_MSG;
    r.ui.cmp.active = true; r.ui.cmp.step = UI_CMP_TO; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("cmp to"));
    r.ui.cmp.step = UI_CMP_TEXT; snprintf(r.ui.cmp.to, sizeof(r.ui.cmp.to), "TEST5-8"); ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("cmp text"));
    r.ui.cmp.step = UI_CMP_CONFIRM; snprintf(r.ui.cmp.text, sizeof(r.ui.cmp.text), "73"); ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("cmp confirm"));
    r.ui.cmp.active = false;
    r.ui.screen = UI_SCREEN_MENU;
    for (int item = 0; item < UI_MENU_COUNT; item++) {
      if (item == UI_MENU_LANGUAGE || item == UI_MENU_CALIB || item == UI_MENU_VERSION) { r.ui.menuItem = (uint8_t)item; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("menu")); continue; }
      r.ui.menuItem = (uint8_t)item;
      for (int k = 0; k < 9; k++) { ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("menu")); r.shortp(1); r.ui.screen = UI_SCREEN_MENU; r.ui.menuItem = (uint8_t)item; }
    }
    r.ui.screen = UI_SCREEN_CALIB; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("calib"));
    ui_set_calib_on(&r.ui, true, 30000); ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("calib on")); ui_set_calib_on(&r.ui, false, 30000);
    sn.gps = GPS_NO_DATA; sn.sdOk = false; sn.hasPos = false;
    for (int s = 0; s < UI_SCREEN_COUNT; s++) { r.ui.screen = (uint8_t)s; ui_render(&r.ui, &sn, &v, 30000); CHECK(clean("screen nogps")); }
    // 英語の見出しと主なボタン
    r.ui.screen = UI_SCREEN_OWN; ui_render(&r.ui, &sn, &v, 30000); CHECK(!strcmp(v.lines[0].text, "My station"));
    r.ui.screen = UI_SCREEN_RX; ui_render(&r.ui, &sn, &v, 30000); CHECK(!strcmp(v.keyShort[1], "Detail") && !strcmp(v.keyLong[2], "Menu"));
    settings_set_language(&r.sm, UI_LANG_JA); ui_set_lang(UI_LANG_JA);
    ui_render(&r.ui, &sn, &v, 30000); CHECK(!strcmp(v.keyShort[1], "詳細")); }   // 日本語に戻すとすぐ戻る

  // ---- シンボルのアイコン: 自局画面・受信詳細 (32x32)・英語の設定メニュー ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    sn.previewValid = true; snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s", "NOCALL-9>APRS:"); snprintf(sn.previewInfo, sizeof(sn.previewInfo), "%s", "!3540.87N/13946.03E>");
    settings_set_symbol(&r.sm, (uint8_t)symbol_find('/', 's')); ui_render(&r.ui, &sn, &v, 0);
    int k = -1; for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, "送信内容:")) k = i;
    CHECK(k > 0 && v.lines[k].icon == SYMBOL_CHOICES[symbol_find('/', 's')].icon && v.lines[k].iconCol == 0 && !strncmp(v.lines[k].text, "  送信内容:", strlen("  送信内容:")));
    sn.previewValid = false; settings_set_ssid(&r.sm, 8); ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, MY_CALLSIGN "-8 /s 船 (動力船)"));
    // 受信詳細: 1行目に32x32のアイコン。この2行は右端を空ける
    AprsPacket p; memset(&p, 0, sizeof(p)); p.micEMessage = "";
    snprintf(p.src, sizeof(p.src), "NOCALL-10"); p.kind = APRS_KIND_POSITION; p.hasPos = true; p.latDeg = 35.0; p.lonDeg = 140.0;
    p.symTable = 'I'; p.symCode = '&'; snprintf(p.comment, sizeof(p.comment), "igate");
    LogTime lt; memset(&lt, 0, sizeof(lt)); lt.valid = true; lt.hour = 1; lt.minute = 2; lt.second = 3;
    ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 1000);
    r.ui.screen = UI_SCREEN_RX; r.shortp(1); CHECK(r.ui.rxDetail);
    ui_render(&r.ui, &sn, &v, 400000);
    CHECK(v.lines[1].icon == SYMBOL_ICON_IGATE && v.lines[1].iconBig && line_cols(v.lines[1].text) <= UI_COLS - UI_BIGICON_COLS && line_cols(v.lines[2].text) <= UI_COLS - UI_BIGICON_COLS);
    for (int i = 2; i < v.count; i++) CHECK(v.lines[i].icon == 0);
    p.hasPos = false; p.kind = APRS_KIND_STATUS; ui_rx_add(&r.ui, &p, true, 35.0, 139.0, &lt, 2000); r.ui.rxSel = r.ui.rxTotal;
    ui_render(&r.ui, &sn, &v, 400000); CHECK(v.lines[1].icon == 0);                    // 位置のないものにはアイコンなし
    // 英語: シンボル21個・SSID16個のどれを選んでも38桁に収まり、日本語が残らない
    settings_set_language(&r.sm, UI_LANG_EN); ui_set_lang(UI_LANG_EN);
    r.ui.screen = UI_SCREEN_MENU;
    auto ascii = [&]() { for (int i = 0; i < v.count; i++) { if (line_cols(v.lines[i].text) > UI_COLS) return false; for (const char* c = v.lines[i].text; *c; c++) if ((unsigned char)*c >= 0x80) return false; } return true; };
    r.ui.menuItem = UI_MENU_SYMBOL; for (int i = 0; i < SYMBOL_CHOICE_COUNT; i++) { settings_set_symbol(&r.sm, (uint8_t)i); ui_render(&r.ui, &sn, &v, 0); CHECK(ascii()); }
    CHECK(has_line(v, ">    /#  Digipeater"));
    r.ui.menuItem = UI_MENU_SSID; for (int i = 0; i < 16; i++) { settings_set_ssid(&r.sm, (uint8_t)i); ui_render(&r.ui, &sn, &v, 0); CHECK(ascii()); }
    CHECK(has_line(v, "> 15  Generic") && has_line(v, "  10  Internet/iGate"));
    r.ui.screen = UI_SCREEN_TXSTAT; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "Symbol: /# Digipeater") && ascii());
    settings_set_language(&r.sm, UI_LANG_JA); ui_set_lang(UI_LANG_JA); }

  // ---- 設定ファイル: コールサイン未設定、読み込みの結果の表示 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    sn.previewValid = true; snprintf(sn.previewHeader, sizeof(sn.previewHeader), "%s", "N0CALL-9>APRS:"); snprintf(sn.previewInfo, sizeof(sn.previewInfo), "%s", "!3540.87N/13946.03E>");
    snprintf(g_cfg.callsign, sizeof(g_cfg.callsign), "%s", "NOCALL");
    ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "コールサイン未設定 (config.txt)") && !has_line(v, "送信可"));
    // 送信もしない (位置より先に調べる)
    static TxComposed c; OwnPos op; memset(&op, 0, sizeof(op)); op.valid = true; op.latDeg = 35; op.lonDeg = 139;
    CHECK(tx_compose(&r.sm, &op, &c) == TXC_NO_CALLSIGN && tx_compose_info(&r.sm, ":N0CALL   :hi{1", &c) == TXC_NO_CALLSIGN);
    op.valid = false; CHECK(tx_compose(&r.sm, &op, &c) == TXC_NO_CALLSIGN);
    test_cfg_setup(); ui_render(&r.ui, &sn, &v, 0); CHECK(!has_line(v, "コールサイン未設定") && has_line(v, "送信可"));
    // バージョン情報に読み込みの結果
    r.longp(2); r.ui.menuItem = UI_MENU_VERSION;
    memset(&g_cfgStatus, 0, sizeof(g_cfgStatus)); g_cfgStatus.source = CFG_SRC_FILE; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "設定: config.txt") && !has_line(v, "エラー") && v.count <= UI_MAX_LINES);
    { const char* ct = "callsign = N0CALL\nfoo = 1\nbar\n"; app_config_parse(&g_cfg, ct, strlen(ct), &g_cfgStatus); }; g_cfgStatus.source = CFG_SRC_FILE; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "設定: config.txt  エラー2件") && has_line(v, "  2行目: 不明な項目 foo") && v.count <= UI_MAX_LINES);
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    g_cfgStatus.source = CFG_SRC_CACHE; ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "設定: 本体の控え (SDなし)"));
    memset(&g_cfgStatus, 0, sizeof(g_cfgStatus)); ui_render(&r.ui, &sn, &v, 0); CHECK(has_line(v, "設定: 初期値 (config.txt なし)"));
    settings_set_language(&r.sm, UI_LANG_EN); ui_set_lang(UI_LANG_EN);
    { const char* ct = "callsign = N0CALL\nfoo = 1\nbar\n"; app_config_parse(&g_cfg, ct, strlen(ct), &g_cfgStatus); }; g_cfgStatus.source = CFG_SRC_FILE; ui_render(&r.ui, &sn, &v, 0);
    CHECK(has_line(v, "Config: config.txt  2 errors") && has_line(v, "  line 2: unknown key foo"));
    for (int i = 0; i < v.count; i++) CHECK(line_cols(v.lines[i].text) <= UI_COLS);
    settings_set_language(&r.sm, UI_LANG_JA); ui_set_lang(UI_LANG_JA); memset(&g_cfgStatus, 0, sizeof(g_cfgStatus)); test_cfg_setup(); }

  // ---- レーダー画面と局の表 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    static MsgCore mc6; msg_init(&mc6, 0); ui_set_msgs(&r.ui, &mc6); ui_set_my_call(&r.ui, MY_CALLSIGN "-9");
    sn.hasPos = true; sn.latDeg = 35.0; sn.lonDeg = 139.0;
    auto pp = [](const char* src, double lat, double lon, char tbl, char code) {
      AprsPacket p; memset(&p, 0, sizeof(p)); p.micEMessage = ""; snprintf(p.src, sizeof(p.src), "%.9s", src);
      p.kind = APRS_KIND_POSITION; p.hasPos = true; p.latDeg = lat; p.lonDeg = lon; p.symTable = tbl; p.symCode = code; return p; };
    LogTime lt; memset(&lt, 0, sizeof(lt)); lt.valid = true; lt.year = 2026; lt.month = 10; lt.day = 29; lt.hour = 12;
    AprsPacket n1 = pp("NOCAL1-9", 35.05, 139.0, '/', '>');     // 北 約5.6km
    AprsPacket e1 = pp("NOCAL2", 35.0, 139.06, '/', '-');       // 東 約5.5km
    AprsPacket f1 = pp("NOCAL3-7", 35.4, 139.0, '/', '[');      // 北 約44km
    AprsPacket me = pp(MY_CALLSIGN "-9", 35.001, 139.001, '/', '>');   // 自局の折り返し
    AprsPacket sk = pp("JA1SJS-7", 34.98, 138.99, '/', 'k');    // 撫子色
    ui_rx_add(&r.ui, &n1, true, 35.0, 139.0, &lt, 1000);
    ui_rx_add(&r.ui, &e1, true, 35.0, 139.0, &lt, 2000);
    ui_rx_add(&r.ui, &f1, true, 35.0, 139.0, &lt, 3000);
    ui_rx_add(&r.ui, &me, true, 35.0, 139.0, &lt, 4000);
    ui_rx_add(&r.ui, &sk, true, 35.0, 139.0, &lt, 5000);
    int used = 0; for (int i = 0; i < RADAR_STATION_COUNT; i++) if (r.ui.st[i].used) { used++; CHECK(!msg_call_equal(r.ui.st[i].call, MY_CALLSIGN "-9")); }
    CHECK(used == 4);                                                              // 自局は入れない
    n1.latDeg = 35.02; ui_rx_add(&r.ui, &n1, true, 35.0, 139.0, &lt, 6000);       // 同じ局は最後の位置に更新
    used = 0; for (int i = 0; i < RADAR_STATION_COUNT; i++) if (r.ui.st[i].used) used++;
    CHECK(used == 4);
    r.ui.screen = UI_SCREEN_RADAR;
    // 自動の距離: いちばん遠い局 (約44km) が入る 50km
    ui_render(&r.ui, &sn, &v, 10000);
    CHECK(v.radar.on && v.radar.hasOwn && v.radar.count == 4 && has_line(v, "レーダー 50km/1時間") && !strcmp(v.radar.info[0], "自動 50km") && !strcmp(v.radar.info[1], "時間 1時間"));
    CHECK(!strcmp(v.radar.ringLabel[0], "25") && !strcmp(v.radar.ringLabel[1], "50"));
    // 位置: 北は真上、東は右。新しい局ほど後 (上に描く)
    int in1 = -1, ie1 = -1, isk = -1;
    for (int i = 0; i < v.radar.count; i++) { const UiRadarPt* p = &v.radar.pts[i];
      if (!strcmp(p->label, "NOCAL1-9")) in1 = i;
      if (!strcmp(p->label, "NOCAL2")) ie1 = i;
      if (!strcmp(p->label, "JA1SJS-7")) isk = i; }
    if (in1 < 0 || ie1 < 0 || isk < 0) { CHECK(false); in1 = ie1 = isk = 0; }
    CHECK(in1 >= 0 && ie1 >= 0 && isk >= 0 && in1 == v.radar.count - 1 && ie1 == 0);
    CHECK(abs(v.radar.pts[in1].x - v.radar.cx) <= 1 && v.radar.pts[in1].y < v.radar.cy - 2 && v.radar.pts[in1].icon == 1);
    CHECK(abs(v.radar.pts[ie1].y - v.radar.cy) <= 1 && v.radar.pts[ie1].x > v.radar.cx + 5 && v.radar.pts[ie1].icon == 16);
    CHECK(v.radar.pts[isk].color == UI_COL_SAKURA && v.radar.pts[in1].color == UI_COL_NORMAL);
    for (int i = 0; i < v.radar.count; i++) { int dx = v.radar.pts[i].x - v.radar.cx, dy = v.radar.pts[i].y - v.radar.cy; CHECK(dx * dx + dy * dy <= (v.radar.r + 1) * (v.radar.r + 1)); }
    { bool nearOk = false; for (int i = 0; i < UI_RADAR_INFO; i++) if (!strcmp(v.radar.info[i], "NOCAL1-9") || !strcmp(v.radar.info[i], "JA1SJS-7")) nearOk = true; CHECK(nearOk); }
    for (int i = 0; i < UI_RADAR_INFO; i++) CHECK(line_cols(v.radar.info[i]) <= UI_RADAR_INFO_COLS);
    CHECK(!strcmp(v.keyShort[1], "切替") && !strcmp(v.keyShort[0], "前") && !strcmp(v.keyLong[0], "送信"));
    // 切り替えモード: B短で入る。A=距離 C=時間 (画面は変わらない)。B短で抜ける
    r.shortp(1); CHECK(r.ui.radarMode && r.ui.screen == UI_SCREEN_RADAR);
    ui_render(&r.ui, &sn, &v, 10000);
    CHECK(!strcmp(v.keyShort[0], "距離") && !strcmp(v.keyShort[1], "終了") && !strcmp(v.keyShort[2], "時間") && !strcmp(v.keyLong[0], "送信") && v.radar.infoColor[0] == UI_COL_HILITE);
    r.shortp(0); CHECK(r.sm.cur.radarRange == 0 && r.ui.screen == UI_SCREEN_RADAR);   // 自動 → 1km
    r.shortp(0); r.shortp(0); r.shortp(0); CHECK(settings_radar_range_km(&r.sm) == 10);
    ui_render(&r.ui, &sn, &v, 10000);
    CHECK(has_line(v, "レーダー 10km/1時間") && v.radar.count == 3 && !strcmp(v.radar.info[0], "距離 10km"));   // 44km の局は範囲外
    { bool outOk = false; for (int i = 0; i < UI_RADAR_INFO; i++) if (!strcmp(v.radar.info[i], "範囲外 1局")) outOk = true; CHECK(outOk); }
    r.shortp(2); CHECK(r.sm.cur.radarTime == 3 && settings_radar_time_min(&r.sm) == 180 && r.ui.screen == UI_SCREEN_RADAR);
    r.shortp(2); r.shortp(2); r.shortp(2); CHECK(settings_radar_time_min(&r.sm) == 1440);   // 6・12・24時間
    ui_render(&r.ui, &sn, &v, 10000); CHECK(has_line(v, "レーダー 10km/24時間") && !strcmp(v.radar.info[1], "時間 24時間"));
    r.shortp(2); CHECK(settings_radar_time_min(&r.sm) == 10);
    // 時間: 10分を過ぎた局は出さない
    ui_render(&r.ui, &sn, &v, 5000 + 10 * 60000u + 1000); CHECK(v.radar.count == 1);   // 最後に受信した NOCAL1-9 だけ
    ui_render(&r.ui, &sn, &v, 6000 + 10 * 60000u + 1000); CHECK(v.radar.count == 0 && !strcmp(v.radar.info[2], "受信局なし"));
    CHECK(r.longp(0) == UI_ACT_SEND && r.ui.radarMode);                            // 長押しはモード中も同じ
    r.shortp(1); CHECK(!r.ui.radarMode);
    r.shortp(2); CHECK(r.ui.screen == UI_SCREEN_MSG && !r.ui.radarMode);           // モードの外では C短で次の画面
    // 自局の位置がない
    r.ui.screen = UI_SCREEN_RADAR; sn.hasPos = false; ui_render(&r.ui, &sn, &v, 10000);
    CHECK(!v.radar.hasOwn && v.radar.count == 0 && !strcmp(v.radar.info[2], "自局位置なし"));
    sn.hasPos = true;
    // 表がいっぱい: 受信がいちばん古い局と入れ替える
    for (int i = 0; i < RADAR_STATION_COUNT; i++) { char c[16]; snprintf(c, sizeof(c), "NC%d", i); AprsPacket q = pp(c, 35.01, 139.01, '/', '>'); ui_rx_add(&r.ui, &q, true, 35, 139, &lt, 20000u + (uint32_t)i); }
    { bool hasN1 = false, hasNC0 = false; used = 0;
      for (int i = 0; i < RADAR_STATION_COUNT; i++) if (r.ui.st[i].used) { used++; if (!strcmp(r.ui.st[i].call, "NOCAL1-9")) hasN1 = true; if (!strcmp(r.ui.st[i].call, "NC0")) hasNC0 = true; }
      CHECK(used == RADAR_STATION_COUNT && !hasN1 && hasNC0); }
    // 英語: 右側の文字も14桁に収まる (距離・時間のすべての組み合わせ)
    settings_set_language(&r.sm, UI_LANG_EN); ui_set_lang(UI_LANG_EN);
    for (int a = 0; a < RADAR_RANGE_COUNT; a++) for (int b = 0; b < RADAR_TIME_COUNT; b++) {
      settings_set_radar_range(&r.sm, (uint8_t)a); settings_set_radar_time(&r.sm, (uint8_t)b); ui_render(&r.ui, &sn, &v, 30000);
      CHECK(line_cols(v.lines[0].text) <= UI_TITLE_COLS);
      for (int i = 0; i < UI_RADAR_INFO; i++) { CHECK(line_cols(v.radar.info[i]) <= UI_RADAR_INFO_COLS); for (const char* c = v.radar.info[i]; *c; c++) CHECK((unsigned char)*c < 0x80 || !strncmp(c, "°", 2) || c[-1] == (char)0xC2); } }
    settings_set_language(&r.sm, UI_LANG_JA); ui_set_lang(UI_LANG_JA);
    for (int a = 0; a < RADAR_RANGE_COUNT; a++) for (int b = 0; b < RADAR_TIME_COUNT; b++) {
      settings_set_radar_range(&r.sm, (uint8_t)a); settings_set_radar_time(&r.sm, (uint8_t)b); ui_render(&r.ui, &sn, &v, 30000);
      CHECK(line_cols(v.lines[0].text) <= UI_TITLE_COLS); for (int i = 0; i < UI_RADAR_INFO; i++) CHECK(line_cols(v.radar.info[i]) <= UI_RADAR_INFO_COLS); }
    // 再起動前の局: UTCが分かれば経過時間で絞る。UTCが無い局・今の時刻が分からないときは出さない
    ui_after_restore(&r.ui, 50000); settings_set_radar_time(&r.sm, 3); settings_set_radar_range(&r.sm, RADAR_RANGE_AUTO);
    sn.hasUtc = false; ui_render(&r.ui, &sn, &v, 50000); CHECK(v.radar.count == 0);
    sn.hasUtc = true; sn.utc.year = 2026; sn.utc.month = 10; sn.utc.day = 29; sn.utc.hour = 13; ui_render(&r.ui, &sn, &v, 50000); CHECK(v.radar.count == RADAR_STATION_COUNT);
    sn.utc.hour = 16; ui_render(&r.ui, &sn, &v, 50000); CHECK(v.radar.count == 0); }

  // ---- 受信詳細の1行目: いちばん長くても右端の大きいアイコンの手前 (33桁) に3桁以上の余裕 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v;
    AprsPacket p; memset(&p, 0, sizeof(p)); p.micEMessage = ""; snprintf(p.src, sizeof(p.src), "NOCAL4-15");
    p.kind = APRS_KIND_POSITION; p.hasPos = true; p.latDeg = 35.1; p.lonDeg = 139.1; p.symTable = '/'; p.symCode = '#';
    LogTime lt; memset(&lt, 0, sizeof(lt)); lt.valid = true; lt.year = 2026; lt.month = 10; lt.day = 30; lt.hour = 14; lt.minute = 22; lt.second = 41;
    ui_rx_add(&r.ui, &p, true, 35, 139, &lt, 1000);
    r.ui.screen = UI_SCREEN_RX; r.shortp(1);
    for (int lang = 0; lang < 2; lang++) {
      settings_set_language(&r.sm, (uint8_t)lang); ui_set_lang((uint8_t)lang);
      ui_render(&r.ui, &sn, &v, 1000 + 3599000u);                       // 59分59秒前
      CHECK(v.lines[1].iconBig && strstr(v.lines[1].text, "NOCAL4-15 14:22:41Z 59") && line_cols(v.lines[1].text) <= UI_COLS - UI_BIGICON_COLS - 3); }
    settings_set_language(&r.sm, UI_LANG_JA); ui_set_lang(UI_LANG_JA); }

  // ---- 撫子色: JA1SJS (SSIDは問わない) の受信一覧・詳細・メッセージ一覧 ----
  { Rig r; UiSnapshot sn = snap_of(&r.sm); UiView v; static MsgCore mc7; msg_init(&mc7, 0); ui_set_msgs(&r.ui, &mc7);
    AprsPacket a; memset(&a, 0, sizeof(a)); a.micEMessage = ""; snprintf(a.src, sizeof(a.src), "JA1SJS-9"); a.kind = APRS_KIND_POSITION; a.hasPos = true; a.latDeg = 35.1; a.lonDeg = 139.1; a.symTable = '/'; a.symCode = '>';
    AprsPacket b = a; snprintf(b.src, sizeof(b.src), "N0CALL");
    AprsPacket c = a; snprintf(c.src, sizeof(c.src), "JA1SJS"); c.hasPos = false; c.kind = APRS_KIND_STATUS; snprintf(c.comment, sizeof(c.comment), "hi");
    ui_rx_add(&r.ui, &a, true, 35, 139, NULL, 1000); ui_rx_add(&r.ui, &b, true, 35, 139, NULL, 2000); ui_rx_add(&r.ui, &c, true, 35, 139, NULL, 3000);
    r.ui.screen = UI_SCREEN_RX; ui_render(&r.ui, &sn, &v, 4000);
    for (int i = 1; i < v.count; i++) {
      bool sj = strstr(v.lines[i].text, "JA1SJS") != NULL, sjt = strstr(v.lines[i].text, "N0CALL") != NULL;
      if (sj) CHECK(v.lines[i].color == UI_COL_SAKURA && v.lines[i + 1].color == UI_COL_SAKURA);   // その局の2行
      if (sjt) CHECK(v.lines[i].color != UI_COL_SAKURA && v.lines[i + 1].color != UI_COL_SAKURA); }
    r.shortp(1); ui_render(&r.ui, &sn, &v, 4000); CHECK(has_line(v, "JA1SJS ") && v.lines[1].color == UI_COL_SAKURA);   // 位置なし (SSIDなし)
    r.shortp(2); ui_render(&r.ui, &sn, &v, 4000); CHECK(has_line(v, "N0CALL") && v.lines[1].color == UI_COL_HILITE);
    r.shortp(2); ui_render(&r.ui, &sn, &v, 4000); CHECK(has_line(v, "JA1SJS-9 ") && v.lines[1].color == UI_COL_SAKURA);
    // メッセージ一覧: 相手が JA1SJS なら撫子色 (未読は未読の色が優先)
    msg_send(&mc7, "JA1SJS-7", "73", 1000, NULL);
    r.longp(1); r.ui.screen = UI_SCREEN_MSG; r.ui.msgDetail = false; ui_render(&r.ui, &sn, &v, 4000);
    { bool ok = false; for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, "JA1SJS-7")) ok = (v.lines[i].color == UI_COL_SAKURA || v.lines[i].color != UI_COL_NORMAL); CHECK(ok); } }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
