// test_state.cpp  (パソコン上のテスト用。スケッチには入れない)
// 再起動後の復元 (state_store) と、UTCの秒、経過時間の表示
#include "test_cfg.h"
#include "state_store.h"
#include "packet_build.h"
#include "packet_parse.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static AprsPacket rxp(const char* src, const char* info) {
  AprsPacket p; memset(&p, 0, sizeof(p));
  uint8_t f[AX25_FRAME_MAX]; Ax25Params ap = { "N0CALL", 7, "APRS", 0, "" };
  size_t n = ax25_build_ui_frame(ap, (const uint8_t*)info, strlen(info), f, sizeof(f)); aprs_parse_frame(f, n - 2, &p);
  snprintf(p.src, sizeof(p.src), "%s", src); return p;
}
static LogTime lt(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  LogTime t; t.valid = true; t.year = y; t.month = mo; t.day = d; t.hour = h; t.minute = mi; t.second = s; return t;
}
static bool has_line(const UiView& v, const char* sub) {
  for (int i = 0; i < v.count; i++) if (strstr(v.lines[i].text, sub)) return true;
  return false;
}

// メモリ上のファイル (StateFileOps)
static std::vector<uint8_t> g_tmp, g_file;
static int g_begins = 0, g_appends = 0, g_commits = 0;
static bool g_failAppend = false;
static bool f_begin() { g_begins++; g_tmp.clear(); return true; }
static bool f_append(const uint8_t* d, size_t n) { if (g_failAppend) return false; g_appends++; g_tmp.insert(g_tmp.end(), d, d + n); return true; }
static bool f_commit() { g_commits++; g_file = g_tmp; return true; }
static const StateFileOps OPS = { f_begin, f_append, f_commit };

static uint8_t buf[STATE_MAX_BYTES], buf2[STATE_MAX_BYTES];

int main() {
  test_cfg_setup();
  // ---- UTCの秒 ----
  { LogTime t = lt(2000, 1, 1, 0, 0, 0); CHECK(log_time_epoch(&t) == 946684800u);
    t = lt(2024, 2, 29, 12, 34, 56); CHECK(log_time_epoch(&t) == 1709210096u);
    t = lt(2026, 10, 27, 23, 59, 59); CHECK(log_time_epoch(&t) == 1793145599u);
    t = lt(2099, 12, 31, 23, 59, 59); CHECK(log_time_epoch(&t) == 4102444799u);
    t.valid = false; CHECK(log_time_epoch(&t) == 0); CHECK(log_time_epoch(NULL) == 0);
    t = lt(1999, 12, 31, 0, 0, 0); CHECK(log_time_epoch(&t) == 0);
    t = lt(2026, 13, 1, 0, 0, 0); CHECK(log_time_epoch(&t) == 0); }

  // ---- 保存して読み込む ----
  static UiState ui; static MsgCore mc;
  ui_init(&ui); msg_init(&mc, 100); ui_set_msgs(&ui, &mc);
  LogTime t0 = lt(2026, 10, 27, 3, 10, 0);
  for (int i = 0; i < 25; i++) {                                    // 受信一覧は直近20件 (循環)
    char s[16]; snprintf(s, sizeof(s), "ST%d", i);
    AprsPacket p = rxp(s, "!3540.00N/13945.00E>hello"); ui_rx_add(&ui, &p, true, 35.6, 139.7, &t0, 1000 + i);
  }
  AprsPacket w1 = rxp("WX1", "@031010z3540.00N/13945.00E_270/005g010t068r000p000P000h55b10123");
  AprsPacket w2 = rxp("WX2", "@031010z3541.00N/13946.00E_090/002g004t050r000p000P000h70b10200");
  LogTime tw1 = lt(2026, 10, 27, 3, 0, 0), tw2 = lt(2026, 10, 27, 3, 5, 0);
  ui_rx_add(&ui, &w1, true, 35.6, 139.7, &tw1, 2000); ui_rx_add(&ui, &w2, true, 35.6, 139.7, &tw2, 3000);
  AprsPacket m1 = rxp("N0CALL-7", ":NOCALL-9 :Hello there{12");
  CHECK(msg_on_packet(&mc, &m1, "NOCALL-9", 4000, &t0) == MSG_RX_NEW);
  int out = msg_send(&mc, "N0CALL-7", "QSL", 5000, &t0);
  CHECK(out >= 0 && mc.e[out].state == MSG_ST_SENDING);
  CHECK(ui.rxTotal == 27 && ui.rxCount == RX_LIST_COUNT && msg_unread(&mc) == 1);

  size_t n = state_serialize(&ui, &mc, buf, sizeof(buf));
  CHECK(n == STATE_MAX_BYTES && !memcmp(buf, "M5ST", 4));
  CHECK(state_serialize(&ui, &mc, buf2, STATE_MAX_BYTES - 1) == 0);
  CHECK(state_serialize(&ui, &mc, buf2, sizeof(buf2)) == n && !memcmp(buf, buf2, n));   // 同じ内容なら同じバイト列

  static UiState u2; static MsgCore m2;
  ui_init(&u2); msg_init(&m2, 500); ui_set_msgs(&u2, &m2);
  uint16_t no0 = m2.nextNo;
  CHECK(state_restore(&u2, &m2, buf, n, 50) == STATE_LOAD_OK);
  CHECK(u2.rxTotal == 27 && u2.rxCount == RX_LIST_COUNT && u2.rxHead == ui.rxHead && !u2.hasLastRx);
  { bool same = true;                                                         // 中身は同じ。再起動前の印が付く
    for (int i = 0; i < RX_LIST_COUNT; i++) {
      const UiRxEntry *a = &u2.rx[i], *b = &ui.rx[i];
      if (a->seq != b->seq || strcmp(a->src, b->src) || strcmp(a->comment, b->comment) || a->latDeg != b->latDeg || a->utc != b->utc || !a->restored) same = false;
    }
    CHECK(same); }
  uint8_t o[WX_STATION_COUNT]; CHECK(ui_wx_order(&u2, o) == 2);
  CHECK(!strcmp(u2.wx[o[0]].src, "WX2") && !strcmp(u2.wx[o[1]].src, "WX1") && u2.wx[o[0]].restored);   // 新しい順を保つ
  CHECK(u2.wx[o[0]].utc == log_time_epoch(&tw2));
  CHECK(msg_unread(&m2) == 1 && m2.nextNo == no0 && m2.seq == mc.seq);
  { int used = 0, found = -1;                                                 // レーダーの局の表も戻る (再起動前の印付き)
    for (int i = 0; i < RADAR_STATION_COUNT; i++) if (u2.st[i].used) { used++; CHECK(u2.st[i].restored); if (!strcmp(u2.st[i].call, "ST24")) found = i; }
    CHECK(used == 27 && found >= 0 && u2.st[found].latDeg == ui.st[found].latDeg && u2.st[found].utc == log_time_epoch(&t0)); }
  CHECK(m2.e[out].state == MSG_ST_FAILED && m2.e[out].restored);         // 送り直しの途中は「応答なし」(勝手に送らない)
  { MsgTx tx; CHECK(msg_poll(&m2, 60, false, true, true, &tx) == MSGP_IDLE); }
  // 起動後に受信したものは、再起動前のものより新しい
  AprsPacket w3 = rxp("WX3", "@031010z3542.00N/13947.00E_000/000g000t060r000p000P000h60b10100");
  ui_rx_add(&u2, &w3, true, 35.6, 139.7, &t0, 100); CHECK(ui_wx_order(&u2, o) == 3 && !strcmp(u2.wx[o[0]].src, "WX3") && !u2.wx[o[0]].restored);
  AprsPacket m3 = rxp("N0CALL-7", ":NOCALL-9 :After reboot{13");
  CHECK(msg_on_packet(&m2, &m3, "NOCALL-9", 200, &t0) == MSG_RX_NEW);
  { uint8_t mo[MSG_STORE_COUNT]; msg_order(&m2, mo); CHECK(!strcmp(m2.e[mo[0]].text, "After reboot")); }
  // 同じ番号の送り直しを再起動後に受けたら、1件にまとめる (Ackは返し直す)
  CHECK(msg_on_packet(&m2, &m1, "NOCALL-9", 300, &t0) == MSG_RX_REPEAT);

  // ---- 経過時間の表示: GPSの時刻が取れるまで「--」 ----
  { SettingsMgr sm; settings_init(&sm, NULL);
    UiSnapshot sn; memset(&sn, 0, sizeof(sn)); sn.settings = &sm; sn.gps = GPS_FIX; sn.sdOk = true;
    UiView v;
    u2.screen = UI_SCREEN_WX; u2.wxDetail = true; snprintf(u2.wxSel, sizeof(u2.wxSel), "WX2");
    ui_render(&u2, &sn, &v, 1000);
    CHECK(has_line(v, "WX2  03:05:00 UTC (--)") && has_line(v, "(再起動前のデータ)") && v.lines[1].color == UI_COL_DIM);
    sn.hasUtc = true; sn.utc.year = 2026; sn.utc.month = 10; sn.utc.day = 27; sn.utc.hour = 3; sn.utc.minute = 15; sn.utc.second = 30;
    ui_render(&u2, &sn, &v, 1000);
    CHECK(has_line(v, "(10分30秒前)") && !has_line(v, "再起動前") && v.lines[1].color == UI_COL_HILITE);
    sn.utc.hour = 4; sn.utc.minute = 5; sn.utc.second = 0;                  // 60分たったら古い扱い
    ui_render(&u2, &sn, &v, 1000); CHECK(has_line(v, "(1時間前)") && has_line(v, "60分以上前") && v.lines[1].color == UI_COL_DIM);
    u2.wxDetail = false; ui_render(&u2, &sn, &v, 1000); CHECK(has_line(v, "1時間前"));
    sn.hasUtc = false; ui_render(&u2, &sn, &v, 1000); CHECK(has_line(v, "WX2") && has_line(v, " --"));
    // メッセージの詳細
    u2.screen = UI_SCREEN_MSG; u2.msgDetail = true; u2.msgSel = m2.e[out].seq;
    ui_render(&u2, &sn, &v, 1000); CHECK(has_line(v, "送信  03:10:00 UTC (--)") && has_line(v, "応答なし"));
    sn.hasUtc = true; sn.utc.hour = 3; sn.utc.minute = 12; sn.utc.second = 0;
    ui_render(&u2, &sn, &v, 1000); CHECK(has_line(v, "(2分00秒前)"));
    // 起動後のものは millis で数える
    u2.msgSel = m2.e[msg_find_seq(&m2, m2.seq)].seq; ui_render(&u2, &sn, &v, 5200); CHECK(has_line(v, "(5秒前)")); }

  // ---- 読み込めないとき (何も変えない) ----
  { static UiState u3; static MsgCore m3; ui_init(&u3); msg_init(&m3, 7);
    CHECK(state_restore(&u3, &m3, buf, 0, 0) == STATE_LOAD_EMPTY);
    CHECK(state_restore(&u3, &m3, buf, 10, 0) == STATE_LOAD_BAD_HEADER);
    memcpy(buf2, buf, n); buf2[0] = 'X'; CHECK(state_restore(&u3, &m3, buf2, n, 0) == STATE_LOAD_BAD_HEADER);
    memcpy(buf2, buf, n); buf2[4] = 9;   CHECK(state_restore(&u3, &m3, buf2, n, 0) == STATE_LOAD_MISMATCH);   // 形式の版が違う
    memcpy(buf2, buf, n); buf2[6] ^= 1;  CHECK(state_restore(&u3, &m3, buf2, n, 0) == STATE_LOAD_MISMATCH);   // 構造体の大きさが違う
    CHECK(state_restore(&u3, &m3, buf, n - 1, 0) == STATE_LOAD_MISMATCH);
    memcpy(buf2, buf, n); buf2[n - 1] ^= 0x55; CHECK(state_restore(&u3, &m3, buf2, n, 0) == STATE_LOAD_BAD_CRC);
    CHECK(u3.rxCount == 0 && u3.rxTotal == 0 && m3.seq == 0 && msg_unread(&m3) == 0); }

  // ---- 書き込みのタイミング ----
  { StateSaver s; state_saver_init(&s, 0); g_file.clear();
    CHECK(state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), 1000, false) == 0 && g_begins == 0);      // 間隔がまだ
    CHECK(state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), STATE_SAVE_INTERVAL_MS, true) == 0 && g_begins == 0);   // 受信中は書かない
    CHECK(state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), STATE_SAVE_INTERVAL_MS + 1, false) == 0 && g_begins == 1 && s.writing);
    uint32_t t = STATE_SAVE_INTERVAL_MS + 2; int r = 0, calls = 0;
    while (s.writing && calls < 20) { bool busy = (calls == 1); r = state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), t++, busy); calls++; }
    size_t chunks = (n + STATE_WRITE_CHUNK - 1) / STATE_WRITE_CHUNK;
    CHECK(r == 1 && (size_t)g_appends == chunks && (size_t)calls == chunks + 1 && g_commits == 1 && s.writes == 1);   // 1回に1かたまり。受信中は休む
    CHECK(g_file.size() == n && !memcmp(g_file.data(), buf, n));
    // 変わっていなければ書かない
    t += STATE_SAVE_INTERVAL_MS; CHECK(state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), t, false) == 0 && g_begins == 1 && !s.writing);
    // 変わったら書く (既読にした)
    msg_mark_read(&mc, msg_find_seq(&mc, 1));
    t += STATE_SAVE_INTERVAL_MS; state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), t, false); CHECK(g_begins == 2 && s.writing);
    g_failAppend = true; CHECK(state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), t + 1, false) == -1 && !s.writing && s.writeErrors == 1);
    g_failAppend = false;
    t += STATE_SAVE_INTERVAL_MS; state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), t, false); CHECK(g_begins == 3);   // 次の間隔でやり直す
    while (s.writing) state_service(&s, &OPS, &ui, &mc, buf2, sizeof(buf2), ++t, false);
    CHECK(s.writes == 2);
    static UiState u4; static MsgCore m4; ui_init(&u4); msg_init(&m4, 1);
    CHECK(state_restore(&u4, &m4, g_file.data(), g_file.size(), 0) == STATE_LOAD_OK && msg_unread(&m4) == 0); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
