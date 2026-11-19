// test_app_config.cpp  (パソコン上のテスト用。スケッチには入れない)
// 設定ファイル (config.txt) の読み取り
#include "test_cfg.h"
#include "app_config.h"
#include "fixed_pos.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static int parse(AppConfig* c, const char* t, CfgStatus* st) { return app_config_parse(c, t, strlen(t), st); }
static bool has_err(const CfgStatus& st, int line, uint8_t code) {
  for (int i = 0; i < st.errorCount && i < CFG_ERROR_MAX; i++) if (st.errors[i].line == line && st.errors[i].code == code) return true;
  return false;
}

int main() {
  test_cfg_setup();
  AppConfig c; CfgStatus st;
  // ---- 初期値 ----
  { app_config_defaults(&c);
    CHECK(!strcmp(c.callsign, "NOCALL") && !app_config_callsign_ok(&c) && !strcmp(c.comment, "M5Stack"));
    CHECK(!strcmp(c.digipath[0], "") && !strcmp(c.digipath[1], "WIDE1-1") && !strcmp(c.digipath[2], "WIDE1-1,WIDE2-1"));
    CHECK(c.msgPresetCount == 8 && !strcmp(c.msgPresets[0], "QSL") && !strcmp(c.msgPresets[7], "Arrived") && c.friendCount == 0);
    CHECK(c.txAmpMark == 100 && c.txAmpSpace == 55 && c.pttOnDelayMs == 300 && c.pttOffDelayMs == 50 && c.pttActiveHigh == 1 && c.txPreambleFlags == 60);
    CHECK(!strcmp(c.fixed[0].name, "Home") && !strcmp(c.fixed[7].name, "Spot8") && !pos_plausible(c.fixed[0].latDeg, c.fixed[0].lonDeg)); }

  // ---- 基本の書き方: コメント、空行、空白、大文字小文字、CRLF、BOM ----
  { const char* t = "\xEF\xBB\xBF# M5APRS\r\n\r\n  callsign = n0call \r\n# comment = no\r\nCOMMENT=144.66MHz #1 Mobile\r\n   # indented comment\r\ntx_amp_mark=90";
    CHECK(parse(&c, t, &st) == 0 && st.errorCount == 0);
    CHECK(!strcmp(c.callsign, "N0CALL") && app_config_callsign_ok(&c));
    CHECK(!strcmp(c.comment, "144.66MHz #1 Mobile"));                     // 値の途中の # は文字
    CHECK(c.txAmpMark == 90 && c.txAmpSpace == 55); }                      // 書かなかった項目は初期値

  // ---- 書き間違い: その行だけ無視し、行番号を記録 ----
  { const char* t = "callsign = N0CALL\n"      // 1
                    "callsing = N1CALL\n"      // 2 知らない項目
                    "just text\n"              // 3 = がない
                    "tx_amp_mark = 200\n"      // 4 範囲外
                    "comment = こんにちは\n"   // 5 ASCII以外
                    "ptt_on_delay = 12a\n"     // 6 数字でない
                    "tx_amp_space = 40\n";     // 7 正しい
    CHECK(parse(&c, t, &st) == 5 && st.errorCount == 5);
    CHECK(has_err(st, 2, CFG_E_UNKNOWN_KEY) && !strcmp(st.errors[0].key, "callsing") && has_err(st, 3, CFG_E_NO_EQUAL) && has_err(st, 4, CFG_E_BAD_VALUE));
    CHECK(has_err(st, 5, CFG_E_BAD_VALUE) && has_err(st, 6, CFG_E_BAD_VALUE));
    CHECK(!strcmp(c.callsign, "N0CALL") && c.txAmpMark == 100 && !strcmp(c.comment, "M5Stack") && c.pttOnDelayMs == 300 && c.txAmpSpace == 40); }
  // 多くの書き間違い: 記録は CFG_ERROR_MAX 件まで、数はすべて
  { char t[512] = ""; for (int i = 0; i < 12; i++) strcat(t, "bad line\n");
    CHECK(parse(&c, t, &st) == 12 && st.errorCount == 12 && st.errors[CFG_ERROR_MAX - 1].line == CFG_ERROR_MAX); }
  // 長すぎる行・値
  { char t[400]; memset(t, 'A', 300); t[300] = 0; char u[500]; snprintf(u, sizeof(u), "comment = %s\ncallsign = N0CALL", t);
    CHECK(parse(&c, u, &st) == 1 && has_err(st, 1, CFG_E_LINE_TOO_LONG) && !strcmp(c.callsign, "N0CALL"));
    CHECK(parse(&c, "comment = 12345678901234567890123456789012345678901234", &st) == 1 && has_err(st, 1, CFG_E_TOO_LONG));
    CHECK(parse(&c, "comment = 1234567890123456789012345678901234567890123", &st) == 0 && strlen(c.comment) == 43);
    CHECK(parse(&c, "comment = a|b", &st) == 1 && parse(&c, "comment =", &st) == 0 && c.comment[0] == 0); }   // 空のコメントは可

  // ---- コールサイン ----
  { const char* ok[] = { "N0CALL", "N1CALL", "NOCAL2", "TEST1", "TST1" };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) { char t[64]; snprintf(t, sizeof(t), "callsign = %s", ok[i]); CHECK(parse(&c, t, &st) == 0 && !strcmp(c.callsign, ok[i])); }
    const char* ng[] = { "N0CALL-9", "ABCDEF", "123456", "N0CALLX", "N0 CAL", "", "N0C_LL" };
    for (size_t i = 0; i < sizeof(ng) / sizeof(ng[0]); i++) { char t[64]; snprintf(t, sizeof(t), "callsign = %s", ng[i]); CHECK(parse(&c, t, &st) == 1 && !strcmp(c.callsign, "NOCALL")); }
    CHECK(parse(&c, "callsign = NOCALL", &st) == 0 && !app_config_callsign_ok(&c)); }   // 見本のままは未設定 (書き間違いではない)

  // ---- 固定位置 ----
  { CHECK(parse(&c, "fixed1 = Home, 35.68123, 139.76712, 12\nfixed3 = Office,34.5,-135.25\nfixed8 = Far, -33.8688, 151.2093", &st) == 0);
    CHECK(!strcmp(c.fixed[0].name, "Home") && fabs(c.fixed[0].latDeg - 35.68123) < 1e-9 && fabs(c.fixed[0].lonDeg - 139.76712) < 1e-9 && c.fixed[0].hasAlt && c.fixed[0].altM == 12.0);
    CHECK(!strcmp(c.fixed[2].name, "Office") && c.fixed[2].lonDeg == -135.25 && !c.fixed[2].hasAlt && !strcmp(c.fixed[1].name, "Spot2"));
    CHECK(c.fixed[7].latDeg < 0);
    g_cfg = c; CHECK(fixed_position_count_set() == 3 && fixed_position_set(0) && !fixed_position_set(1) && fixed_position_set(2) && fixed_position_set(7) && !fixed_position_set(8));
    test_cfg_setup();
    const char* ng[] = { "fixed1 = Home, 95, 139", "fixed1 = Home, 35, 181", "fixed1 = Home, 35", "fixed1 = , 35, 139", "fixed1 = Home, abc, 139",
                         "fixed1 = Home, 0, 0", "fixed1 = Home, 35, 139, 12, 5", "fixed1 = TooLongName, 35, 139", "fixed9 = Home, 35, 139", "fixed0 = Home, 35, 139" };
    for (size_t i = 0; i < sizeof(ng) / sizeof(ng[0]); i++) CHECK(parse(&c, ng[i], &st) == 1 && !pos_plausible(c.fixed[0].latDeg, c.fixed[0].lonDeg));
    CHECK(has_err(st, 1, CFG_E_UNKNOWN_KEY));                                                   // fixed0 は知らない項目
    CHECK(parse(&c, "fixed2 = Spot, 35, 139\nfixed2 =", &st) == 0 && !pos_plausible(c.fixed[1].latDeg, c.fixed[1].lonDeg)); }   // 空で未設定に戻す

  // ---- デジパス ----
  { CHECK(parse(&c, "digipath1 = wide2-2\ndigipath2 = WIDE1-1, WIDE2-1\ndigipath3 =", &st) == 0);
    CHECK(!strcmp(c.digipath[0], "WIDE2-2") && !strcmp(c.digipath[1], "WIDE1-1,WIDE2-1") && !strcmp(c.digipath[2], ""));
    const char* ng[] = { "digipath1 = WIDE1-16", "digipath1 = WIDE1-1,", "digipath1 = ,WIDE1-1", "digipath1 = WIDE1-1,,WIDE2-1", "digipath1 = WIDE_1", "digipath1 = TOOLONGX-1",
                         "digipath1 = A,B,C,D,E,F,G,H,I" };
    for (size_t i = 0; i < sizeof(ng) / sizeof(ng[0]); i++) CHECK(parse(&c, ng[i], &st) == 1 && !strcmp(c.digipath[0], "")); }

  // ---- 定型文・宛先の候補: 1つでも書けば、書いたものだけ (空は詰める) ----
  { CHECK(parse(&c, "msg2 = Hello\nmsg5 = QRV 145.00\nmsg3 =", &st) == 0 && c.msgPresetCount == 2 && !strcmp(c.msgPresets[0], "Hello") && !strcmp(c.msgPresets[1], "QRV 145.00"));
    CHECK(parse(&c, "msg1 = a{b", &st) == 1 && c.msgPresetCount == 8);                // 使えない文字。ほかに書いていなければ初期値のまま
    CHECK(parse(&c, "msg1 = 日本語", &st) == 1);
    CHECK(parse(&c, "friend1 = n1call-7\nfriend3 = N2CALL\nfriend2 = BAD", &st) == 1 && c.friendCount == 2 && !strcmp(c.friends[0], "N1CALL-7") && !strcmp(c.friends[1], "N2CALL"));
    CHECK(parse(&c, "friend1 = N1CALL-16", &st) == 1 && c.friendCount == 0); }

  // ---- 無線機・基板の値 ----
  { CHECK(parse(&c, "tx_amp_mark=127\ntx_amp_space=1\nptt_on_delay=0\nptt_off_delay=1000\nptt_active_high=0\ntx_preamble=255", &st) == 0);
    CHECK(c.txAmpMark == 127 && c.txAmpSpace == 1 && c.pttOnDelayMs == 0 && c.pttOffDelayMs == 1000 && c.pttActiveHigh == 0 && c.txPreambleFlags == 255);
    const char* ng[] = { "tx_amp_mark=0", "tx_amp_space=128", "ptt_on_delay=2001", "ptt_off_delay=-1", "ptt_active_high=2", "tx_preamble=0", "tx_preamble=", "tx_amp_mark= 1.5" };
    for (size_t i = 0; i < sizeof(ng) / sizeof(ng[0]); i++) CHECK(parse(&c, ng[i], &st) == 1); }

  // ---- 見本のファイル: 書き間違いなしで読めて、初期値と同じになる ----
  { static char t[CFG_FILE_MAX]; size_t n = app_config_template(t, sizeof(t));
    CHECK(n > 500 && n + 2048 < CFG_FILE_MAX && strstr(t, "callsign = NOCALL") && strstr(t, "fixed8 =") && strstr(t, "tx_preamble = 60"));
    AppConfig d; app_config_defaults(&d);
    CHECK(!app_config_flex_ok(&d) && d.flex.set == 0);                                // 初期値にはフレックスの値がない (config.txt にだけ書く)
    // 見本にはフレックスの値 (八重洲・ケンウッドの初期値) が書いてある
    d.flex.set = 0xFF; d.flex.lowSpeed = 5; d.flex.highSpeed = 70; d.flex.slowRateMin = 30; d.flex.fastRateSec = 120;
    d.flex.turnAngle = 28; d.flex.turnSlope = 26; d.flex.turnTimeSec = 30; d.flex.slowDistM = 0;
    CHECK(app_config_parse(&c, t, n, &st) == 0 && memcmp(&c, &d, sizeof(c)) == 0 && app_config_flex_ok(&c));
    CHECK(app_config_template(t, 100) == 0); }                                       // 入りきらなければ 0
  // ---- フレックス ----
  { const char* all = "flex_low_speed=2\nflex_high_speed=90\nflex_slow_rate=100\nflex_fast_rate=10\nflex_turn_angle=5\nflex_turn_slope=255\nflex_turn_time=180\nflex_slow_dist=5000\n";
    CHECK(app_config_parse(&c, all, strlen(all), &st) == 0 && app_config_flex_ok(&c));
    CHECK(c.flex.lowSpeed == 2 && c.flex.highSpeed == 90 && c.flex.slowRateMin == 100 && c.flex.fastRateSec == 10 &&
          c.flex.turnAngle == 5 && c.flex.turnSlope == 255 && c.flex.turnTimeSec == 180 && c.flex.slowDistM == 5000);
    // flex_slow_dist は書かなくてよい。ほかの7つは1つでも欠けたら選べない
    const char* noDist = "flex_low_speed=5\nflex_high_speed=70\nflex_slow_rate=30\nflex_fast_rate=120\nflex_turn_angle=28\nflex_turn_slope=26\nflex_turn_time=30\n";
    CHECK(app_config_parse(&c, noDist, strlen(noDist), &st) == 0 && app_config_flex_ok(&c) && c.flex.slowDistM == 0);
    const char* lack = "flex_low_speed=5\nflex_high_speed=70\nflex_slow_rate=30\nflex_fast_rate=120\nflex_turn_angle=28\nflex_turn_slope=26\n";
    CHECK(app_config_parse(&c, lack, strlen(lack), &st) == 0 && !app_config_flex_ok(&c));
    // LOW SPEED が HIGH SPEED 以上なら選べない
    const char* inv = "flex_low_speed=30\nflex_high_speed=30\nflex_slow_rate=30\nflex_fast_rate=120\nflex_turn_angle=28\nflex_turn_slope=26\nflex_turn_time=30\n";
    CHECK(app_config_parse(&c, inv, strlen(inv), &st) == 0 && !app_config_flex_ok(&c));
    // 範囲外は書き間違い (その行は無視)
    const char* ng[] = { "flex_low_speed=1", "flex_low_speed=31", "flex_high_speed=91", "flex_slow_rate=0", "flex_slow_rate=101",
                         "flex_fast_rate=9", "flex_fast_rate=181", "flex_turn_angle=4", "flex_turn_angle=91", "flex_turn_slope=0",
                         "flex_turn_slope=256", "flex_turn_time=4", "flex_turn_time=181", "flex_slow_dist=-1", "flex_slow_dist=5001", "flex_turn_time=" };
    for (size_t i = 0; i < sizeof(ng) / sizeof(ng[0]); i++) { CHECK(parse(&c, ng[i], &st) == 1 && c.flex.set == 0); } }
  // 空のファイル・NULL の状態
  { CHECK(app_config_parse(&c, "", 0, NULL) == 0 && app_config_parse(&c, "x", 1, NULL) == 1); }
  // 書き間違いの説明
  { for (uint8_t k = CFG_E_NO_EQUAL; k <= CFG_E_LINE_TOO_LONG; k++) CHECK(app_config_error_text(k)[0] && app_config_error_text_en(k)[0]); }
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
