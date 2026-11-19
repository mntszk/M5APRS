// test_nmea.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "nmea.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { checks++; double _a = (a), _b = (b); if (!(fabs(_a - _b) <= (tol))) { failures++; printf("FAIL line %d: %s=%.7f vs %.7f\n", __LINE__, #a, _a, _b); } } while (0)

// チェックサムを付けた1行を作る (本文は '$' と '*' を除く)
static std::string nm(const std::string& body) {
  uint8_t x = 0; for (size_t i = 0; i < body.size(); i++) x ^= (uint8_t)body[i];
  char h[8]; snprintf(h, sizeof(h), "*%02X", x);
  return "$" + body + h + "\r\n";
}
static size_t feed(GpsState& g, const std::string& s, uint32_t now) { return gps_feed(&g, s.data(), s.size(), now); }

int main() {
  test_cfg_setup();
  GpsState g; GpsPosition pos; double alt;

  // ---- チェックサム (有名な例文) ----
  CHECK(nmea_checksum_ok("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
  CHECK(nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
  CHECK(nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47") );
  CHECK(!nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48"));
  CHECK(!nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));   // * なし
  CHECK(!nmea_checksum_ok("$GPGGA,1*4")); CHECK(!nmea_checksum_ok("$GPGGA,1*")); CHECK(!nmea_checksum_ok("$GPGGA,1*ZZ"));
  CHECK(!nmea_checksum_ok("GPGGA,1*00")); CHECK(!nmea_checksum_ok("")); CHECK(!nmea_checksum_ok(NULL));
  CHECK(nmea_checksum_ok("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a"));   // 小文字

  // ---- RMC: 有名な例文 ----
  gps_init(&g);
  CHECK(feed(g, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", 1000) == 1);
  CHECK(gps_get_position(&g, 1000, 300, &pos));
  NEAR(pos.latDeg, 48 + 7.038 / 60, 1e-9); NEAR(pos.lonDeg, 11 + 31.0 / 60, 1e-9);
  CHECK(pos.hasSpeed && fabs(pos.speedKnots - 22.4) < 1e-9 && pos.hasCourse && fabs(pos.courseDeg - 84.4) < 1e-9);
  CHECK(g.hasTime && g.year == 2094);                                    // 年は2000年代として扱う
  CHECK(g.hour == 12 && g.minute == 35 && g.second == 19 && g.day == 23 && g.month == 3);

  // ---- GNRMC (ATGM336H風)、南緯・西経 ----
  gps_init(&g);
  CHECK(feed(g, nm("GNRMC,093045.000,A,3352.1300,S,15112.5600,W,0.52,87.30,091026,,,A"), 5000) == 1);
  CHECK(gps_get_position(&g, 5000, 300, &pos));
  NEAR(pos.latDeg, -(33 + 52.13 / 60), 1e-9); NEAR(pos.lonDeg, -(151 + 12.56 / 60), 1e-9);
  CHECK(g.year == 2026 && g.month == 10 && g.day == 9 && g.hour == 9 && g.minute == 30 && g.second == 45);
  CHECK(pos.ageSec == 0);
  // 速度・進行方向が空
  gps_init(&g);
  feed(g, nm("GNRMC,093045.000,A,3539.5200,N,13944.7200,E,,,091026,,,A"), 0);
  CHECK(gps_get_position(&g, 0, 300, &pos) && !pos.hasSpeed && !pos.hasCourse);
  // 進行方向 360 は 0 にそろえる
  feed(g, nm("GNRMC,093046.000,A,3539.5200,N,13944.7200,E,1.0,360.0,091026,,,A"), 10);
  CHECK(gps_get_position(&g, 10, 300, &pos) && pos.hasCourse && pos.courseDeg == 0.0);

  // ---- 無効(V)や モード N は位置に使わない ----
  gps_init(&g);
  feed(g, nm("GNRMC,093045.000,V,,,,,,,091026,,,N"), 0);
  CHECK(!gps_get_position(&g, 0, 300, &pos));
  CHECK(gps_get_status(&g, 0, 5000, 300) == GPS_NO_FIX);                  // NMEAは届いているが測位なし
  feed(g, nm("GNRMC,093045.000,A,3539.5200,N,13944.7200,E,0.1,10.0,091026,,,N"), 10);
  CHECK(!gps_get_position(&g, 10, 300, &pos));
  feed(g, nm("GNRMC,093046.000,A,3539.5200,N,13944.7200,E,0.1,10.0,091026,,,A"), 20);
  CHECK(gps_get_position(&g, 20, 300, &pos) && g.lastRmcValid);
  // Vが来ても、直前の位置は残る (位置は更新されない)
  feed(g, nm("GNRMC,093047.000,V,3500.0000,N,13900.0000,E,0.1,10.0,091026,,,A"), 1000);
  CHECK(gps_get_position(&g, 1000, 300, &pos));
  NEAR(pos.latDeg, 35 + 39.52 / 60, 1e-9); CHECK(!g.lastRmcValid);
  CHECK(gps_get_status(&g, 1000, 5000, 300) == GPS_STALE);

  // ---- 状態の遷移と5分ルール ----
  gps_init(&g);
  CHECK(gps_get_status(&g, 0, 5000, 300) == GPS_NO_DATA);
  feed(g, nm("GNGGA,093045.000,,,,,0,00,99.99,,,,,,"), 100);               // 測位なし
  CHECK(gps_get_status(&g, 100, 5000, 300) == GPS_NO_FIX);
  feed(g, nm("GNRMC,093046.000,A,3539.5200,N,13944.7200,E,0.1,10.0,091026,,,A"), 1000);
  CHECK(gps_get_status(&g, 1000, 5000, 300) == GPS_FIX);
  CHECK(gps_get_status(&g, 5999, 5000, 300) == GPS_FIX);
  CHECK(gps_get_status(&g, 6000, 5000, 300) == GPS_NO_DATA);              // 5秒途絶 → 受信なし表示
  CHECK(gps_get_position(&g, 6000, 300, &pos) && pos.ageSec == 5);        // でも位置は5分間は使える
  CHECK(gps_get_position(&g, 1000 + 300000, 300, &pos) && pos.ageSec == 300);
  CHECK(!gps_get_position(&g, 1000 + 300001, 300, &pos));                  // 5分を超えたら使わない
  // データは来るが測位できない状態: 位置の期限は最後の測位から
  feed(g, nm("GNRMC,093046.000,V,,,,,,,091026,,,N"), 200000);
  CHECK(gps_get_status(&g, 200000, 5000, 300) == GPS_STALE);
  feed(g, nm("GNRMC,093046.000,V,,,,,,,091026,,,N"), 301001);
  CHECK(gps_get_status(&g, 301001, 5000, 300) == GPS_NO_FIX);
  feed(g, nm("GNRMC,093046.000,A,3540.0000,N,13945.0000,E,0.1,10.0,091026,,,A"), 302000);
  CHECK(gps_get_status(&g, 302000, 5000, 300) == GPS_FIX);

  // ---- millis() の一周 (約49.7日) をまたぐ ----
  gps_init(&g);
  feed(g, nm("GNRMC,093046.000,A,3539.5200,N,13944.7200,E,0.1,10.0,091026,,,A"), 0xFFFFFF00u);
  CHECK(gps_get_position(&g, 0xFFFFFF00u + 1000u, 300, &pos) && pos.ageSec == 1);
  CHECK(gps_get_status(&g, 0xFFFFFF00u + 2000u, 5000, 300) == GPS_FIX);
  CHECK(!gps_get_position(&g, 0xFFFFFF00u + 301000u, 300, &pos));

  // ---- GGA: 高度 ----
  gps_init(&g);
  CHECK(feed(g, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n", 0) == 1);
  CHECK(gps_get_altitude(&g, 0, 300, &alt) && fabs(alt - 545.4) < 1e-9 && g.satellites == 8);
  CHECK(!gps_get_position(&g, 0, 300, &pos));                              // GGAだけでは位置にしない (RMCを使う)
  CHECK(gps_get_altitude(&g, 300000, 300, &alt) && !gps_get_altitude(&g, 300001, 300, &alt));
  feed(g, nm("GNGGA,093045.000,3539.5200,N,13944.7200,E,1,12,0.8,-12.5,M,39.0,M,,"), 10);
  CHECK(gps_get_altitude(&g, 10, 300, &alt) && fabs(alt + 12.5) < 1e-9);    // 負の高度
  feed(g, nm("GNGGA,093045.000,3539.5200,N,13944.7200,E,0,03,0.8,99.0,M,39.0,M,,"), 20);   // 品質0は更新しない
  CHECK(gps_get_altitude(&g, 20, 300, &alt) && fabs(alt + 12.5) < 1e-9);
  feed(g, nm("GNGGA,093045.000,3539.5200,N,13944.7200,E,1,12,0.8,99.0,F,39.0,M,,"), 30);    // 単位がフィートは無視
  CHECK(fabs(g.altM + 12.5) < 1e-9);
  feed(g, nm("GNGGA,093045.000,3539.5200,N,13944.7200,E,1,12,0.8,,M,39.0,M,,"), 30);        // 空
  CHECK(fabs(g.altM + 12.5) < 1e-9);


  // ---- 現在のUTC日時 (受信時の日時 + 経過時間) ----
  { GpsUtc u; gps_init(&g);
    CHECK(!gps_get_utc(&g, 0, &u));
    feed(g, nm("GNRMC,235958.000,A,3539.5200,N,13944.7200,E,0.1,10.0,281226,,,A"), 1000);       // 2026-12-28 23:59:58
    CHECK(gps_get_utc(&g, 1000, &u) && u.year == 2026 && u.month == 12 && u.day == 28 && u.hour == 23 && u.minute == 59 && u.second == 58);
    CHECK(gps_get_utc(&g, 2999, &u) && u.second == 59 && u.day == 28);
    CHECK(gps_get_utc(&g, 3000, &u) && u.day == 29 && u.hour == 0 && u.minute == 0 && u.second == 0);   // 日付をまたぐ
    CHECK(gps_get_utc(&g, 1000 + 2 * 86400000u + 2000, &u) && u.year == 2026 && u.month == 12 && u.day == 31 && u.hour == 0 && u.second == 0);
    CHECK(gps_get_utc(&g, 1000 + 3 * 86400000u + 2000, &u) && u.year == 2027 && u.month == 1 && u.day == 1);   // 年をまたぐ
    feed(g, nm("GNRMC,235959.000,A,3539.5200,N,13944.7200,E,0.1,10.0,280224,,,A"), 0);          // 2024年2月28日 (うるう年)
    CHECK(gps_get_utc(&g, 1000, &u) && u.month == 2 && u.day == 29 && u.hour == 0 && u.second == 0);
    CHECK(gps_get_utc(&g, 1000 + 86400000u, &u) && u.month == 3 && u.day == 1);
    feed(g, nm("GNRMC,235959.000,A,3539.5200,N,13944.7200,E,0.1,10.0,280225,,,A"), 0);          // 2025年 (うるう年でない)
    CHECK(gps_get_utc(&g, 1000, &u) && u.month == 3 && u.day == 1);
    feed(g, nm("GNRMC,120000.000,A,3539.5200,N,13944.7200,E,0.1,10.0,010126,,,A"), 0xFFFFFF00u);   // millis一周
    CHECK(gps_get_utc(&g, 0xFFFFFF00u + 61000u, &u) && u.hour == 12 && u.minute == 1 && u.second == 1);
    feed(g, nm("GNRMC,120000.000,V,,,,,,,010126,,,N"), 5000);                                     // 測位前 (V) でも日付があれば使う
    CHECK(gps_get_utc(&g, 5000, &u) && u.year == 2026 && u.month == 1 && u.day == 1 && u.hour == 12 && u.second == 0);
    feed(g, nm("GNRMC,000012.000,V,,,,,,,060180,,,N"), 6000);                                     // 時刻合わせ前の既定の日付 (1980年 → 2080年) は使わない
    CHECK(gps_get_utc(&g, 6000, &u) && u.year == 2026 && u.second == 1);
    feed(g, nm("GNRMC,000012.000,V,,,,,,,,,,N"), 7000);                                           // 日付なし
    CHECK(gps_get_utc(&g, 7000, &u) && u.year == 2026 && u.second == 2);
    gps_init(&g); feed(g, nm("GNRMC,093045.000,V,,,,,,,271026,,,N"), 100);                        // 測位していなくても時刻は取れる
    CHECK(gps_get_utc(&g, 100, &u) && u.day == 27 && u.hour == 9 && gps_get_status(&g, 100, 5000, 300) == GPS_NO_FIX); }

  // ---- 壊れた入力 ----
  gps_init(&g);
  CHECK(feed(g, "$GNRMC,093045.000,A,3539.5200,N,13944.7200,E,0.52,87.30,091026,,,A*00\r\n", 0) == 0);
  CHECK(g.checksumErrors == 1 && !g.hasPos);
  CHECK(feed(g, "$GNRMC,093045.000,A,3539.5200,N,13944.7200,E,0.52,87.30,091026,,,A\r\n", 0) == 0);   // * なし
  CHECK(feed(g, nm("GNRMC,093045.000,A,3560.0000,N,13944.7200,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);  // 60分
  CHECK(feed(g, nm("GNRMC,093045.000,A,9100.0000,N,13944.7200,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);  // 91度
  CHECK(feed(g, nm("GNRMC,093045.000,A,3539.5200,X,13944.7200,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);
  CHECK(feed(g, nm("GNRMC,093045.000,A,3539.5200,N,18100.0000,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);
  CHECK(feed(g, nm("GNRMC,093045.000,A,35a9.5200,N,13944.7200,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);
  CHECK(feed(g, nm("GNRMC,093045.000,A,3539.5200,N,13944.7200,E,0.5,8.0,091026"), 0) == 1 && g.hasPos);   // 項目が少なくても必須分があれば可
  gps_init(&g);
  CHECK(feed(g, nm("GNRMC,093045.000,A"), 0) == 1 && !g.hasPos);                                    // 項目不足
  CHECK(feed(g, nm("GNGSV,3,1,11,01,40,083,46"), 0) == 1 && !g.hasPos);                             // 関係ないセンテンス
  CHECK(feed(g, nm("PMTK010,001"), 0) == 1 && !g.hasPos);                                          // 独自 (受信としては数える)
  CHECK(feed(g, nm("GNRMCX,093045.000,A,3539.5200,N,13944.7200,E,0.5,8.0,091026,,,A"), 0) == 1 && !g.hasPos);   // 名前が長い

  // ---- 行の区切り・分割入力・ゴミ ----
  gps_init(&g);
  std::string a = nm("GNRMC,093045.000,A,3539.5200,N,13944.7200,E,0.52,87.30,091026,,,A");
  size_t ok = 0;
  for (size_t i = 0; i < a.size(); i++) ok += gps_feed_char(&g, a[i], 0) ? 1 : 0;      // 1文字ずつ
  CHECK(ok == 1 && g.hasPos);
  gps_init(&g);
  std::string b = "garbage\x01\xFF$$$GNRMC,xx\n" + a + "\n\n" + nm("GNGGA,093045.000,3539.5200,N,13944.7200,E,1,12,0.8,50.0,M,39.0,M,,");
  CHECK(feed(g, b, 0) == 2 && g.hasPos && g.hasAlt);                                  // 途中の '$' でやり直す
  gps_init(&g);
  std::string lf = a.substr(0, a.size() - 2) + "\n";                                   // LFのみ
  CHECK(feed(g, lf, 0) == 1);
  lf = a.substr(0, a.size() - 2) + "\r";                                               // CRのみ
  CHECK(feed(g, lf, 0) == 1);
  // 長すぎる行は捨てて、次の行は正常に読める
  gps_init(&g);
  std::string longLine = "$GNRMC,";
  for (int i = 0; i < 300; i++) longLine += 'A';
  longLine += "\r\n";
  CHECK(feed(g, longLine + a, 0) == 1 && g.overflows == 1 && g.hasPos);
  // 改行なしで終わった行は、次の '$' で捨てられる
  gps_init(&g);
  CHECK(feed(g, "$GNRMC,093045.000,A,3539.52", 0) == 0);
  CHECK(feed(g, a, 0) == 1 && g.hasPos);

  // ---- 乱数入力で落ちない ----
  srand(5);
  gps_init(&g);
  for (int i = 0; i < 300000; i++) {
    char c = (char)rand();
    if (i % 7 == 0) c = "$,*\r\n"[rand() % 5];
    gps_feed_char(&g, c, (uint32_t)i);
  }
  CHECK(g.len < NMEA_LINE_MAX);
  // 正しい行の1文字だけを壊す: 必ずチェックサムエラー(または無視)で、位置が変わらない
  gps_init(&g);
  feed(g, a, 0);
  GpsState ref = g;
  int accepted = 0;
  for (size_t i = 1; i + 2 < a.size(); i++) {
    if (a[i] == '*' || a[i] == ',') continue;
    std::string m = a; m[i] = (char)(m[i] ^ 0x01);
    if (m[i] == '*' || m[i] == '\r' || m[i] == '\n' || m[i] == '$' || m[i] == ',') continue;
    GpsState t = ref; feed(t, m, 100);
    if (fabs(t.latDeg - ref.latDeg) > 1e-12 || fabs(t.lonDeg - ref.lonDeg) > 1e-12) accepted++;
  }
  CHECK(accepted == 0);

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
