// test_packet_parse.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "packet_parse.h"
#include "packet_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { checks++; double _a = (a), _b = (b); if (!(fabs(_a - _b) <= (tol))) { failures++; printf("FAIL line %d: %s=%.6f vs %.6f (tol %g)\n", __LINE__, #a, _a, _b, (double)(tol)); } } while (0)

// FCSなしのフレームを作る
static std::vector<uint8_t> frame(const char* src, int ssid, const char* dest, int dssid, const char* digis,
                                  const uint8_t* info, size_t n) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { src, (uint8_t)ssid, dest, (uint8_t)dssid, digis };
  size_t len = ax25_build_ui_frame(p, info, n, f, sizeof(f));
  return std::vector<uint8_t>(f, f + (len ? len - 2 : 0));
}
static std::vector<uint8_t> frame_s(const char* dest, const char* info, const char* digis = "WIDE1-1") {
  return frame("NOCALL", 9, dest, 0, digis, (const uint8_t*)info, strlen(info));
}
static bool parse(const std::vector<uint8_t>& f, AprsPacket* p) { return aprs_parse_frame(f.data(), f.size(), p); }

// ---- Mic-E の符号化 (仕様の表から、デコーダとは別に書いたもの) ----
static std::string mice_dest(double lat, double lon, bool west, int amb, int msgABC = 4 /*1/0/0*/) {
  bool north = lat >= 0; lat = fabs(lat);
  long t = lround(lat * 6000.0);
  int deg = (int)(t / 6000), min = (int)(t % 6000) / 100, hun = (int)(t % 6000) % 100;
  int dig[6] = { deg / 10, deg % 10, min / 10, min % 10, hun / 10, hun % 10 };
  bool off100 = (fabs(lon) >= 100.0) || (fabs(lon) < 10.0);
  char out[8];
  for (int i = 0; i < 6; i++) {
    int flag = 0;
    if (i < 3) flag = (msgABC >> (2 - i)) & 1;
    else if (i == 3) flag = north;
    else if (i == 4) flag = off100;
    else flag = west;
    bool blank = (i >= 6 - amb);
    if (blank) out[i] = flag ? 'Z' : 'L';
    else out[i] = (char)((flag ? 'P' : '0') + dig[i]);
  }
  out[6] = 0;
  return out;
}
static std::string mice_info(double lon, int speedKn, int course, char code, char table, const char* tail = "") {
  lon = fabs(lon);
  long t = lround(lon * 6000.0);
  int deg = (int)(t / 6000), min = (int)(t % 6000) / 100, hun = (int)(t % 6000) % 100;
  int d;
  if (deg <= 9) d = deg + 90; else if (deg <= 99) d = deg; else if (deg <= 109) d = deg - 20; else d = deg - 100;
  int m = min < 10 ? min + 60 : min;
  char b[16];
  b[0] = '`';
  b[1] = (char)(d + 28); b[2] = (char)(m + 28); b[3] = (char)(hun + 28);
  b[4] = (char)((speedKn / 10) + 28);
  b[5] = (char)((speedKn % 10) * 10 + course / 100 + 28);
  b[6] = (char)(course % 100 + 28);
  b[7] = code; b[8] = table; b[9] = 0;
  return std::string(b, 9) + tail;
}

int main() {
  test_cfg_setup();
  AprsPacket p;

  // ===== 距離と方位 (独立に計算した値と比較) =====
  NEAR(geo_distance_km(35.6762, 139.6503, 34.6937, 135.5023), 392.4418, 0.01);
  NEAR(geo_bearing_deg(35.6762, 139.6503, 34.6937, 135.5023), 255.0402, 0.001);
  NEAR(geo_distance_km(35.0, 139.0, 35.0, 140.0), 91.0853, 0.01);
  NEAR(geo_bearing_deg(35.0, 139.0, 35.0, 140.0), 89.7132, 0.001);
  NEAR(geo_distance_km(0, 0, 0, 90), 10007.5572, 0.01);
  NEAR(geo_bearing_deg(0, 0, 0, 90), 90.0, 1e-9);
  NEAR(geo_distance_km(35.6586, 139.7454, 43.0618, 141.3545), 834.694, 0.01);
  NEAR(geo_bearing_deg(35.6586, 139.7454, 43.0618, 141.3545), 9.0353, 0.001);
  NEAR(geo_distance_km(-33.8688, 151.2093, 35.6762, 139.6503), 7825.829, 0.05);
  NEAR(geo_bearing_deg(-33.8688, 151.2093, 35.6762, 139.6503), 350.049, 0.001);
  NEAR(geo_distance_km(51.5, -0.12, 40.7, -74.0), 5571.498, 0.05);
  NEAR(geo_bearing_deg(51.5, -0.12, 40.7, -74.0), 288.3237, 0.001);
  NEAR(geo_distance_km(10, 20, 10, 20), 0.0, 1e-9);
  NEAR(geo_bearing_deg(10, 20, 10, 20), 0.0, 1e-9);
  NEAR(geo_distance_km(0, 0, 0, 180), 20015.1144, 0.01);                // 対蹠点
  NEAR(geo_bearing_deg(10, 0, 20, 0), 0.0, 1e-9);                       // 真北
  NEAR(geo_bearing_deg(20, 0, 10, 0), 180.0, 1e-9);                     // 真南
  NEAR(geo_bearing_deg(0, 10, 0, 0), 270.0, 1e-9);                      // 真西
  NEAR(geo_distance_km(35, 179.9, 35, -179.9), geo_distance_km(35, 0, 35, 0.2), 1e-6);   // 日付変更線をまたぐ
  CHECK(geo_bearing_deg(35, 179.9, 35, -179.9) > 0 && geo_bearing_deg(35, 179.9, 35, -179.9) < 90);
  NEAR(aprs_knots_to_kmh(10), 18.52, 1e-9);

  // ===== 非圧縮位置 =====
  { auto f = frame_s("APRS", "!3539.52N/13944.72E>090/012/A=000200M5Stack");
    CHECK(parse(f, &p));
    CHECK(p.kind == APRS_KIND_POSITION && p.dti == '!' && p.hasPos);
    NEAR(p.latDeg, 35 + 39.52 / 60, 1e-9); NEAR(p.lonDeg, 139 + 44.72 / 60, 1e-9);
    CHECK(p.symTable == '/' && p.symCode == '>' && p.ambiguity == 0);
    CHECK(p.hasCourse && p.courseDeg == 90 && p.hasSpeed && p.speedKnots == 12);
    CHECK(p.hasAltitude); NEAR(p.altitudeM, 200 * 0.3048, 1e-9);
    CHECK(strcmp(p.comment, "M5Stack") == 0);
    CHECK(strcmp(p.src, "NOCALL-9") == 0 && strcmp(p.dest, "APRS") == 0 && strcmp(p.via, "WIDE1-1") == 0); }
  { auto f = frame_s("APRS", "=3352.13S/15112.56W-test");
    CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION);
    NEAR(p.latDeg, -(33 + 52.13 / 60), 1e-9); NEAR(p.lonDeg, -(151 + 12.56 / 60), 1e-9);
    CHECK(!p.hasCourse && !p.hasSpeed && !p.hasAltitude && strcmp(p.comment, "test") == 0); }
  { auto f = frame_s("APRS", "@092345z4903.50N/07201.75W>088/036");     // 時刻つき
    CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION);
    NEAR(p.latDeg, 49 + 3.5 / 60, 1e-9); NEAR(p.lonDeg, -(72 + 1.75 / 60), 1e-9);
    CHECK(p.courseDeg == 88 && p.speedKnots == 36); }
  { auto f = frame_s("APRS", "/092345h4903.50N/07201.75W>");
    CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION && p.dti == '/'); }
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>360/005");             // 360 = 真北
    CHECK(parse(f, &p)); CHECK(p.hasCourse && p.courseDeg == 0 && p.speedKnots == 5);
    f = frame_s("APRS", "!3500.00N/13900.00E>000/000");
    CHECK(parse(f, &p)); CHECK(!p.hasCourse && p.hasSpeed && p.speedKnots == 0); }
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>/A=-00033x");          // 負の高度
    CHECK(parse(f, &p)); CHECK(p.hasAltitude); NEAR(p.altitudeM, -33 * 0.3048, 1e-9); CHECK(strcmp(p.comment, "x") == 0); }
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>abc/A=000100def");      // 高度がコメントの途中
    CHECK(parse(f, &p)); CHECK(p.hasAltitude); CHECK(strcmp(p.comment, "abcdef") == 0); }
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>/A=00x100");            // 不正な高度は無視してコメントに残す
    CHECK(parse(f, &p)); CHECK(!p.hasAltitude && strcmp(p.comment, "/A=00x100") == 0); }

  // 曖昧化 (中央の位置になる)
  { const char* in[5] = { "!4903.50N/07245.00W>", "!4903.5 N/07245.0 W>", "!4903.  N/07245.  W>",
                          "!490 .  N/0724 .  W>", "!49  .  N/072  .  W>" };
    const double cell[5] = {0, 0.1, 1, 10, 60};
    const double latMin[5] = {3.5, 3.5, 3.0, 0.0, 0.0};      // 切り捨てた値(分)
    for (int a = 0; a < 5; a++) {
      auto f = frame_s("APRS", in[a]);
      CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION);
      CHECK(p.ambiguity == a);
      double expLat = 49 + (a == 0 ? 3.5 : latMin[a] + cell[a] / 2) / 60.0;
      double lonMinTrunc = a == 0 ? 0.0 : floor(45.0 / cell[a] + 1e-9) * cell[a];
      double lonMin = a == 0 ? 45.0 : lonMinTrunc + cell[a] / 2;
      if (a == 0) lonMin = 45.0;
      NEAR(p.latDeg, expLat, 1e-9); NEAR(p.lonDeg, -(72 + lonMin / 60.0), 1e-9);
    } }
  // 不正な位置は「位置なし」になり、情報フィールドが見える
  { const char* bad[] = { "!3539.52X/13944.72E>", "!3539.52N/13944.72>", "!3569.52N/13944.72E>", "!9100.00N/13944.72E>",
                          "!3539.52N/18144.72E>", "!35 9.52N/13944.72E>", "!3539.5 N/1394 .72E>", "!3539.52N/13944.72E", "!", "!35" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      auto f = frame_s("APRS", bad[i]);
      CHECK(parse(f, &p)); CHECK(p.kind == APRS_KIND_OTHER && !p.hasPos);
      CHECK(strcmp(p.comment, bad[i]) == 0);
    } }

  // ---- 圧縮形式の位置 (APRS101 第9章の例と、実際に受信したパケット) ----
  { auto near = [](double a, double b, double e) { return fabs(a - b) <= e; };
    auto f = frame_s("APRS", "!/5L!!<*e7>7P[");                          // 進行方向・速度
    CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION && p.compressed && p.hasPos);
    CHECK(near(p.latDeg, 49.5, 1e-5) && near(p.lonDeg, -72.75, 1e-5) && p.symTable == '/' && p.symCode == '>');
    CHECK(p.hasCourse && p.courseDeg == 88 && p.hasSpeed && p.speedKnots == 36 && !p.hasAltitude && p.comment[0] == 0);
    f = frame_s("APRS", "=/5L!!<*e7OS]S");                                // 高度 (GGA)
    CHECK(parse(f, &p) && p.compressed && p.symCode == 'O' && p.hasAltitude && near(p.altitudeM, 10004.52 * 0.3048, 0.5) && !p.hasSpeed && !p.hasCourse);
    f = frame_s("APRS", "@092345z/5L!!<*e7>{?!Comment");                  // 時刻付き、電波の範囲 (使わない)、コメント
    CHECK(parse(f, &p) && p.compressed && near(p.latDeg, 49.5, 1e-5) && !p.hasSpeed && !p.hasAltitude && !strcmp(p.comment, "Comment"));
    f = frame_s("APRS", "!/<c#yqQzA[  !Kuboshi");                           // cs なし (空白)
    CHECK(parse(f, &p) && p.compressed && near(p.latDeg, 35.15147, 1e-4) && near(p.lonDeg, 138.65139, 1e-4) && p.symCode == '[' && !p.hasSpeed && !p.hasCourse && !strcmp(p.comment, "Kuboshi"));
    f = frame_s("APRS", "!/<dmSqVkd[#8_Kuboshi\r");                         // 進行方向 8度・約4.9ノット
    CHECK(parse(f, &p) && near(p.latDeg, 35.11215, 1e-4) && near(p.lonDeg, 138.86179, 1e-4) && p.hasCourse && p.courseDeg == 8 && p.speedKnots == 5 && !strcmp(p.comment, "Kuboshi"));
    f = frame_s("APRS", "!a5L!!<*e7#7P[");                                 // オーバーレイの数字は a〜j (a=0)
    CHECK(parse(f, &p) && p.compressed && p.symTable == '0' && p.symCode == '#');
    f = frame_s("APRS", "!\\5L!!<*e7>7P[/A=001234");                       // 代替表、コメントの高度
    CHECK(parse(f, &p) && p.symTable == '\\' && p.hasAltitude && near(p.altitudeM, 1234 * 0.3048, 0.01));
    f = frame_s("APRS", "@092345z/5L!!<*e7_7P[g005t077r000p000P000h50b09900wRSW");   // 気象局: cs は風向・風速
    CHECK(parse(f, &p) && p.compressed && p.weather.present && p.weather.hasWindDir && p.weather.windDirDeg == 88);
    CHECK(near(p.weather.windSpeedMs, 36.23 * 0.514444, 0.05) && p.weather.hasTemp && near(p.weather.tempC, 25.0, 0.1) && p.weather.hasHumidity && p.weather.humidityPct == 50 && !p.hasSpeed);
    // 壊れたもの: 短い、91進数の範囲外、記号の表として使えない文字
    const char* bad[] = { "!/5L!!<*e7>7P", "!/5L!~<*e7>7P[", "!k5L!!<*e7>7P[", "!#5L!!<*e7>7P[", "!/5L!!<*e7 7P[" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) { f = frame_s("APRS", bad[i]); CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER && !p.hasPos); }
    // 非圧縮は compressed にならない
    f = frame_s("APRS", "!3540.80N/13945.60E>"); CHECK(parse(f, &p) && p.kind == APRS_KIND_POSITION && !p.compressed); }

  // 位置以外のパケット (メッセージ、状態など)
  { struct { const char* in; AprsKind k; const char* c; } oth[] = {
      { ":N0CALL   :Hello{12", APRS_KIND_MESSAGE, ":N0CALL   :Hello{12" },
      { ">Status text", APRS_KIND_STATUS, "Status text" },
      { ">120848zTNC TEST 1.2k9.6kbps status\r", APRS_KIND_STATUS, "TNC TEST 1.2k9.6kbps status" },   // 時刻は除く
      { ">123456 not a time", APRS_KIND_STATUS, "123456 not a time" },
      { ">", APRS_KIND_STATUS, "" },
      { "T#005,199,000,255,073,123,01101001", APRS_KIND_TELEMETRY, "T#005,199,000,255,073,123,01101001" },
      { "<IGATE,MSG_CNT=199,LOC_CNT=7\r", APRS_KIND_CAPS, "IGATE,MSG_CNT=199,LOC_CNT=7" },
      { "}TEST7-10>APRX29,TCPIP,IGATE-10*::TEST7-10:UNIT.Rx Erlang", APRS_KIND_THIRDPARTY, "TEST7-10>APRX29,TCPIP,IGATE-10*::TEST7-10:UNIT.Rx Erlang" },
      { "Test beacon", APRS_KIND_OTHER, "Test beacon" } };
    for (size_t i = 0; i < sizeof(oth) / sizeof(oth[0]); i++) {
      auto f = frame_s("APRS", oth[i].in);
      CHECK(parse(f, &p)); CHECK(p.kind == oth[i].k && !p.hasPos && strcmp(p.comment, oth[i].c) == 0);
      if (strcmp(p.comment, oth[i].c)) printf("other %zu: [%s]\n", i, p.comment);
    } }

  // ===== Mic-E: APRS101 の例 =====
  // 宛先 S32U6T の最初の3文字が S32 → 標準メッセージ 1/0/0 = M3: Returning。
  // 経度のオフセットを+100にしたもの (第5文字を V に) を使い、情報フィールド `(_fn"Oj/ を復号する。
  { std::string info = "`(_fn\"Oj/";
    auto f = frame_s("S32UVT", info.c_str());
    CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE && p.hasPos);
    NEAR(p.latDeg, 33 + 25.64 / 60, 1e-9);
    NEAR(p.lonDeg, -(112 + 7.74 / 60), 1e-9);
    CHECK(p.speedKnots == 20 && p.hasSpeed);
    CHECK(p.hasCourse && p.courseDeg == 251);
    CHECK(p.symCode == 'j' && p.symTable == '/');
    CHECK(strcmp(p.micEMessage, "Returning") == 0 && p.ambiguity == 0); }
  // 宛先の例 (オフセット+0, 西)
  { auto f = frame_s("S32U6T", "`(_fn\"Oj/");
    CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
    NEAR(p.latDeg, 33 + 25.64 / 60, 1e-9); NEAR(p.lonDeg, -(12 + 7.74 / 60), 1e-9); }
  // 速度・進行方向の2通りの符号化 (73ノット/294度、86ノット/194度)
  { const char sp73[2] = { 's', '#' }, dc73[2] = { '@', '<' };
    for (int k = 0; k < 2; k++) {
      char info[16] = { '`', '(', '_', 'f', sp73[k], dc73[k], 'z', '>', '/', 0 };
      auto f = frame_s("S32UVT", info);
      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
      CHECK(p.speedKnots == 73 && p.courseDeg == 294);
    }
    const char sp86[2] = { 't', '$' }, dc86[2] = { ']', 'Y' };
    for (int k = 0; k < 2; k++) {
      char info[16] = { '`', '(', '_', 'f', sp86[k], dc86[k], 'z', '>', '/', 0 };
      auto f = frame_s("S32UVT", info);
      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
      CHECK(p.speedKnots == 86 && p.courseDeg == 194);
    } }
  // 曖昧化: 宛先 T4SQZZ (下2桁が空白)。経度 112度7.74分 → 範囲の中央
  { auto f = frame_s("T4SQZZ", "`(_fn\"Oj/");
    CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE && p.ambiguity == 2);
    NEAR(p.latDeg, 44 + 31.5 / 60, 1e-9);             // 44度31分台 → 31.5分
    NEAR(p.lonDeg, -(112 + 7.5 / 60), 1e-9); }
  // メッセージの種類
  { struct { const char* d; const char* m; } t[] = {
      { "S32UVT", "Returning" }, { "234UVT", "Emergency" }, { "F2DUVT", "Custom-2" }, { "PRQUVT", "Off Duty" },
      { "S2DUVT", "" } };                              // 標準とカスタムの混在 = 不明
    for (size_t i = 0; i < 5; i++) {
      auto f = frame_s(t[i].d, "`(_fn\"Oj/");
      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
      if (strcmp(p.micEMessage, t[i].m) != 0) printf("msg %s -> [%s]\n", t[i].d, p.micEMessage);
      CHECK(strcmp(p.micEMessage, t[i].m) == 0);
    } }
  // 経度の4つの範囲
  { const double lons[] = { 0.0, 0.5, 5.25, 9.99, 10.0, 12.5, 99.99, 100.0, 105.5, 109.99, 110.0, 139.7454, 179.99 };
    for (size_t i = 0; i < sizeof(lons) / sizeof(lons[0]); i++) {
      for (int w = 0; w < 2; w++) {
        double lon = w ? -lons[i] : lons[i];
        std::string d = mice_dest(35.6586, lon, w != 0, 0);
        std::string in = mice_info(lon, 12, 90, '>', '/');
        auto f = frame_s(d.c_str(), in.c_str());
        CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
        NEAR(p.lonDeg, lon, 0.0051 / 60.0 + 1e-9); NEAR(p.latDeg, 35.6586, 0.0051 / 60.0 + 1e-9);
      }
    } }
  // 乱数で往復 (緯度経度、速度、進行方向)
  { srand(7);
    int bad = 0;
    for (int i = 0; i < 4000; i++) {
      double lat = (rand() / (double)RAND_MAX) * 178.0 - 89.0;
      double lon = (rand() / (double)RAND_MAX) * 359.0 - 179.5;
      int sp = rand() % 400, cs = rand() % 361;
      std::string d = mice_dest(lat, lon, lon < 0, 0, rand() % 8);
      std::string in = mice_info(lon, sp, cs, 'k', '\\');
      auto f = frame_s(d.c_str(), in.c_str());
      if (!parse(f, &p) || p.kind != APRS_KIND_MICE) { bad++; continue; }
      bool ok = fabs(p.latDeg - lat) <= 0.0051 / 60 && fabs(p.lonDeg - lon) <= 0.0051 / 60 &&
                p.speedKnots == sp && p.symCode == 'k' && p.symTable == '\\';
      if (cs % 360 == 0) ok = ok && !(cs == 0 && p.hasCourse) && (cs == 0 || (p.hasCourse && p.courseDeg == 0));
      else ok = ok && p.hasCourse && p.courseDeg == cs;
      if (!ok) { bad++; if (bad < 5) printf("roundtrip miss: lat %.5f lon %.5f sp %d cs %d -> %.5f %.5f %d %d\n", lat, lon, sp, cs, p.latDeg, p.lonDeg, p.speedKnots, p.courseDeg); }
    }
    CHECK(bad == 0); }
  // 曖昧化1〜4 (範囲の中に元の位置が入る)
  { srand(11);
    int bad = 0;
    const double cell[5] = {0, 0.1, 1, 10, 60};
    for (int i = 0; i < 2000; i++) {
      double lat = (rand() / (double)RAND_MAX) * 170.0 - 85.0;
      double lon = (rand() / (double)RAND_MAX) * 350.0 - 175.0;
      int a = 1 + rand() % 4;
      std::string d = mice_dest(lat, lon, lon < 0, a);
      std::string in = mice_info(lon, 5, 100, '>', '/');
      auto f = frame_s(d.c_str(), in.c_str());
      if (!parse(f, &p) || p.ambiguity != a) { bad++; continue; }
      double tol = cell[a] / 2.0 / 60.0 + 0.0051 / 60.0;
      if (fabs(p.latDeg - lat) > tol || fabs(p.lonDeg - lon) > tol) bad++;
    }
    CHECK(bad == 0); }
  // 高度、テレメトリ、状態テキスト
  { // 200ft = 61m → "4T}  (仕様の例)
    std::string in = mice_info(139.7454, 0, 0, '>', '/', "\"4T}Hello");
    auto f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
    CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE);
    CHECK(p.hasAltitude); NEAR(p.altitudeM, 61.0, 1e-9); CHECK(strcmp(p.comment, "Hello") == 0);
    in = mice_info(139.7454, 0, 0, '>', '/', ">\"4T}");                      // Kenwood TH-D7
    f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
    CHECK(parse(f, &p) && p.hasAltitude); NEAR(p.altitudeM, 61.0, 1e-9); CHECK(strcmp(p.comment, "") == 0);
    in = mice_info(139.7454, 0, 0, '>', '/', "]\"4T}");                      // Kenwood TM-D700
    f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
    CHECK(parse(f, &p) && p.hasAltitude); NEAR(p.altitudeM, 61.0, 1e-9);
    // 機種の印 (APRS 1.2) と末尾の改行は取り除く。実際に受信したもの
    struct { const char* tail; const char* want; bool alt; const char* model; } mk[] = {
      { "`\"6=}_(\r", "", true, "FT2D" },                          // Yaesu FT2D (高度つき、コメントなし)
      { "`\"3y}BLUE VAN FTM300D 25W MOBILE TEST BEACON ONAIR!!_1\r", "BLUE VAN FTM300D 25W MOBILE TEST BEACON ONAIR!!", true, "FTM-300D" },
      { "`_$\r", "", false, "FT1D" },                               // 高度なし
      { "`_\"", "", false, "FTM-350" },
      { "`hello_9", "hello", false, "不明(_9)" },                     // 表にない印
      { "]PHG33302/W1,TEST DIGI 1200bps=\r", "PHG33302/W1,TEST DIGI 1200bps", false, "TM-D710" },
      { "]old radio", "old radio", false, "TM-D700" },
      { ">Test handheld comment text&\r", "Test handheld comment text", false, "TH-D75" },
      { ">&\r", "", false, "TH-D75" },
      { ">hi^", "hi", false, "TH-D74" },
      { ">hi", "hi", false, "TH-D7A" },
      { "'on duty|3", "on duty", false, "TinyTrak3" },                // Byonics (メッセージ非対応)
      { "\"6.}Kenwood TM-D710  11.9V 30C", "Kenwood TM-D710  11.9V 30C", true, "" },   // 印なし: そのまま
      { "Hello=", "Hello=", false, "" },                              // 印なしなら末尾も消さない
      { "`7200ab status", "7200ab status", false, "" } };            // 古い16進テレメトリとしては扱わない
    for (size_t i = 0; i < sizeof(mk) / sizeof(mk[0]); i++) {
      in = mice_info(139.7454, 0, 0, '>', '/', mk[i].tail);
      f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE && p.hasAltitude == mk[i].alt);
      if (strcmp(p.comment, mk[i].want)) printf("mic-e tail %zu: [%s]\n", i, p.comment);
      CHECK(strcmp(p.comment, mk[i].want) == 0);
      if (strcmp(p.model, mk[i].model)) printf("mic-e model %zu: [%s]\n", i, p.model);
      CHECK(strcmp(p.model, mk[i].model) == 0);
    }
    // ---- 宛先 (TOCALL) から機種 (Mic-E 以外。0.14.0) ----
    { struct { const char* dest; const char* model; } tc[] = {
        { "APM5AP", "M5APRS" },                                     // M5APRS 自身
        { "APNU19", "UIdigi" }, { "APOG77", "OpenGD77" }, { "APU25N", "UI-View32" },
        { "APJYC1", "YAAC" }, { "APTT4", "TinyTrak" },               // ?・* の形
        { "APZ123", "Experimental" },
        { "APRS", "" }, { "BEACON", "" }, { "ZZZZZZ", "" } };        // 汎用・表にない
      for (size_t i = 0; i < sizeof(tc) / sizeof(tc[0]); i++) {
        CHECK(parse(frame_s(tc[i].dest, "=3400.00N/13900.00E-test"), &p) && p.kind == APRS_KIND_POSITION);
        if (strcmp(p.model, tc[i].model)) printf("tocall %s: [%s]\n", tc[i].dest, p.model);
        CHECK(!strcmp(p.model, tc[i].model));
      }
      CHECK(parse(frame_s("APM5AP", ">status"), &p) && !strcmp(p.model, "M5APRS"));      // 位置以外でも
      CHECK(parse(frame_s("APM5AP", ":N0CALL   :hi{1"), &p) && !strcmp(p.model, "M5APRS"));
      // n は数字1文字だけに合う
      CHECK(aprs_tocall_model("APKEY1") && !strcmp(aprs_tocall_model("APKEY1"), "ESP8266 APRS") && aprs_tocall_model("APKEYX") == NULL);
      CHECK(!strcmp(aprs_tocall_model("APM5AP-7"), "M5APRS"));                        // SSID は無視
      CHECK(aprs_tocall_model(NULL) == NULL && aprs_tocall_model("APRS") == NULL && aprs_tocall_model("") == NULL);
      // 表は「決まった文字が多いもの」が先: 完全一致がワイルドカードより先に合う
      CHECK(!strcmp(aprs_tocall_model("APM5AP"), "M5APRS"));
      // Mic-E は宛先に緯度が入るので、宛先の表は使わない (印のない Mic-E は機種なし)
      in = mice_info(139.7454, 0, 0, '>', '/', "");
      f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE && p.model[0] == 0); }
    // 末尾の改行は、どの形式でも取り除く
    CHECK(parse(frame_s("APRS", "=3400.00N/13900.00E- {UIV32N}\r"), &p) && !strcmp(p.comment, " {UIV32N}"));
    CHECK(parse(frame_s("APRS", ">status text\r\n"), &p) && !strcmp(p.comment, "status text"));
    // 高度の符号化: 0m, -10000m, 海面下(負)、高い所
    struct { const char* s; double m; } al[] = { { "!!!}", -10000 }, { "\"4T}", 61 }, { "\"X!}", 0 }, { "\"8E}", 1000 + 0 } };
    for (size_t i = 0; i < 3; i++) {
      in = mice_info(139.7454, 0, 0, '>', '/', al[i].s);
      f = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), in.c_str());
      CHECK(parse(f, &p) && p.hasAltitude);
      if (fabs(p.altitudeM - al[i].m) > 1e-9 && i < 2) printf("alt %s -> %.1f\n", al[i].s, p.altitudeM);
      if (i < 2) NEAR(p.altitudeM, al[i].m, 1e-9);
    } }
  // Mic-Eの不正: 短い、宛先に使えない文字、範囲外
  { auto f = frame_s("S32UVT", "`(_fn\"Oj");                                 CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);
    f = frame_s("S32UVT", "`(_fn");                                           CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);
    f = frame_s("S32AVT", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);   // 4文字目にA〜K
    f = frame_s("S32UAT", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);
    f = frame_s("S32U6M", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);   // 使えない文字M
    f = frame_s("PZ2UVT", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);   // 途中の空白
    f = frame_s("YY2UVT", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);   // 緯度99度
    f = frame_s("S32UVT", "`\x10_fn\"Oj/");                                   CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);   // 範囲外の経度
    f = frame_s("S32UVT", "`(_fn\"Oj/");                                      CHECK(parse(f, &p) && p.kind == APRS_KIND_MICE); }

  // ===== AX.25 アドレス部 =====
  { // デジパス (使用済みの印)
    auto f = frame("NOCALL", 0, "APRS", 0, "WIDE1-1,WIDE2-1", (const uint8_t*)"!3500.00N/13900.00E>", 20);
    f[14 + 6] |= 0x80;                                  // 1つ目のデジピーターが送信済み
    CHECK(parse(f, &p) && strcmp(p.via, "WIDE1-1*,WIDE2-1") == 0 && strcmp(p.src, "NOCALL") == 0);
    f[21 + 6] |= 0x80;
    CHECK(parse(f, &p) && strcmp(p.via, "WIDE1-1,WIDE2-1*") == 0);
    f = frame("NOCALL", 15, "APRS", 3, "", (const uint8_t*)"!3500.00N/13900.00E>", 20);
    CHECK(parse(f, &p) && strcmp(p.src, "NOCALL-15") == 0 && strcmp(p.dest, "APRS-3") == 0 && p.via[0] == 0);
    f = frame("NOCALL", 0, "APRS", 0, "A,B,C,D,E,F,G,H", (const uint8_t*)"!3500.00N/13900.00E>", 20);
    CHECK(parse(f, &p) && strcmp(p.via, "A,B,C,D,E,F,G,H") == 0); }
  // UI以外・情報なし
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>");
    f[14 + 7 * 1] ^= 0;                                 // (デジピーター1つ → 制御は index 21)
    f[21] = 0x13;                                       // SABM など
    CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);
    f = frame_s("APRS", "!3500.00N/13900.00E>"); f[22] = 0xCF;
    CHECK(parse(f, &p) && p.kind == APRS_KIND_OTHER);
    f = frame("NOCALL", 0, "APRS", 0, "", (const uint8_t*)"", 0);
    CHECK(parse(f, &p) == false || p.kind == APRS_KIND_OTHER); }
  // 壊れたフレーム
  { uint8_t z[300]; memset(z, 0x40, sizeof(z));
    CHECK(!aprs_parse_frame(z, 0, &p)); CHECK(!aprs_parse_frame(z, 15, &p));
    CHECK(!aprs_parse_frame(NULL, 20, &p)); CHECK(!aprs_parse_frame(z, 20, NULL));
    for (size_t i = 0; i < 300; i += 2) z[i] = 0x40;    // 最後のアドレスの印が一度も立たない → 失敗
    CHECK(!aprs_parse_frame(z, 300, &p));
    auto f = frame_s("APRS", "!3500.00N/13900.00E>");
    for (size_t n = 0; n < f.size(); n++) { bool r = aprs_parse_frame(f.data(), n, &p); (void)r; }  // 途中で切れても落ちない
    srand(3);
    for (int i = 0; i < 20000; i++) {                   // 乱数フレームで落ちない
      uint8_t g[80]; size_t n = 16 + rand() % 64;
      for (size_t k = 0; k < n; k++) g[k] = (uint8_t)rand();
      if (i % 2) { g[13] |= 1; g[6] &= 0xFE; }
      if (i % 3 == 0) { g[14] = 0x03; g[15] = 0xF0; g[16] = (uint8_t)(i % 5 == 0 ? '`' : i % 5 == 1 ? '!' : i % 5 == 2 ? '@' : 0x27); }
      aprs_parse_frame(g, n, &p);
      CHECK(strlen(p.comment) < PARSE_COMMENT_MAX && strlen(p.via) < PARSE_VIA_MAX);
    } }
  // 長い情報フィールドの切り詰め
  { std::string big = "!3500.00N/13900.00E>";
    for (int i = 0; i < 100; i++) big += (char)('a' + i % 26);
    auto f = frame_s("APRS", big.c_str());
    CHECK(parse(f, &p) && strlen(p.comment) == PARSE_COMMENT_MAX - 1); }
  // 表示できない文字は ? になる
  { auto f = frame_s("APRS", "!3500.00N/13900.00E>a\x01" "b\xE3\x81\x82" "c");
    CHECK(parse(f, &p) && strcmp(p.comment, "a?b???c") == 0); }


  // ===== TNC2形式の文字列 =====
  { char s[300];
    auto f = frame_s("APRS", "!3539.52N/13944.72E>090/012/A=000200M5Stack");
    CHECK(aprs_tnc2_string(f.data(), f.size(), s, sizeof(s)) > 0);
    CHECK(strcmp(s, "NOCALL-9>APRS,WIDE1-1:!3539.52N/13944.72E>090/012/A=000200M5Stack") == 0);
    auto f2 = frame("NOCALL", 0, "APRS", 0, "WIDE1-1,WIDE2-1", (const uint8_t*)"x", 1); f2[14 + 6] |= 0x80;
    CHECK(aprs_tnc2_string(f2.data(), f2.size(), s, sizeof(s)) > 0 && strcmp(s, "NOCALL>APRS,WIDE1-1*,WIDE2-1:x") == 0);
    auto f3 = frame("NOCALL", 0, "APRS", 0, "", (const uint8_t*)"", 0);
    size_t n3 = aprs_tnc2_string(f3.data(), f3.size(), s, sizeof(s));
    CHECK(n3 == 0 || strcmp(s, "NOCALL>APRS:") == 0);
    std::string mi = mice_info(139.7454, 12, 90, '>', '\\');
    auto f4 = frame_s(mice_dest(35.6586, 139.7454, false, 0).c_str(), mi.c_str());
    size_t n4 = aprs_tnc2_string(f4.data(), f4.size(), s, sizeof(s));
    CHECK(n4 > 0 && strstr(s, "\\x") != NULL && strstr(s, "\\\\") != NULL);                  // 制御文字は \xHH、バックスラッシュは二重に
    for (size_t i = 0; i < n4; i++) CHECK((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7F);
    CHECK(aprs_tnc2_string(f.data(), f.size(), s, 10) == 0 && s[0] == 0);                      // 入りきらない
    CHECK(aprs_tnc2_string(f.data(), f.size(), s, 1) == 0);
    CHECK(aprs_tnc2_string(f.data(), 5, s, sizeof(s)) == 0);
    // 切り詰めの境界: ちょうど入る長さ
    size_t need = strlen("NOCALL-9>APRS,WIDE1-1:!3539.52N/13944.72E>090/012/A=000200M5Stack");
    char exact[200]; CHECK(aprs_tnc2_string(f.data(), f.size(), exact, need + 1) == need);
    CHECK(aprs_tnc2_string(f.data(), f.size(), exact, need) == 0); }

  // ---- 気象データ: 位置付き (シンボル '_') ----
  { AprsPacket p;
    CHECK(parse(frame_s("APRS", "@092345z4903.50N/07201.75W_220/004g005t077r000p000P000h50b09900wRSW"), &p));
    CHECK(p.kind == APRS_KIND_POSITION && p.hasPos && p.symCode == '_' && p.weather.present);
    NEAR(p.latDeg, 49.0583, 0.001); NEAR(p.lonDeg, -72.0292, 0.001);
    CHECK(!p.hasSpeed && !p.hasCourse);                                   // ddd/sss は進行方向・速度ではなく風向・風速
    CHECK(p.weather.hasWindDir && p.weather.windDirDeg == 220);
    CHECK(p.weather.hasWindSpeed); NEAR(p.weather.windSpeedMs, 4 * 0.44704, 1e-6);
    CHECK(p.weather.hasGust); NEAR(p.weather.gustMs, 5 * 0.44704, 1e-6);
    CHECK(p.weather.hasTemp); NEAR(p.weather.tempC, 25.0, 1e-6);           // 77°F
    CHECK(p.weather.hasRain1h && p.weather.rain1hMm == 0 && p.weather.hasRain24h && p.weather.hasRainMid);
    CHECK(p.weather.hasHumidity && p.weather.humidityPct == 50);
    CHECK(p.weather.hasPressure); NEAR(p.weather.pressureHpa, 990.0, 1e-6);
    CHECK(!strcmp(p.comment, "wRSW"));                                    // 残りはコメント (ソフトの種類など)
    // 不明の項目 ("..." や空白)、負の気温、湿度 00 = 100%、照度
    CHECK(parse(frame_s("APRS", "!3540.00N/13945.00E_.../...g...t-05r010p...P...h00b10132L450 Home WX"), &p));
    CHECK(p.weather.present && !p.weather.hasWindDir && !p.weather.hasWindSpeed && !p.weather.hasGust);
    CHECK(p.weather.hasTemp); NEAR(p.weather.tempC, (-5 - 32) * 5.0 / 9.0, 1e-6);
    CHECK(p.weather.hasRain1h); NEAR(p.weather.rain1hMm, 2.54, 1e-6);
    CHECK(!p.weather.hasRain24h && !p.weather.hasRainMid);
    CHECK(p.weather.hasHumidity && p.weather.humidityPct == 100);
    CHECK(p.weather.hasPressure); NEAR(p.weather.pressureHpa, 1013.2, 1e-6);
    CHECK(p.weather.hasLuminosity && p.weather.luminosityWm2 == 450);
    CHECK(!strcmp(p.comment, " Home WX"));
    CHECK(parse(frame_s("APRS", "!3540.00N/13945.00E_000/000l050"), &p) && p.weather.luminosityWm2 == 1050);
    // 気象データが途中で崩れていたら、そこから先はコメント
    CHECK(parse(frame_s("APRS", "!3540.00N/13945.00E_090/010t0x5h40"), &p));
    CHECK(p.weather.present && p.weather.windDirDeg == 90 && !p.weather.hasTemp && !p.weather.hasHumidity && !strcmp(p.comment, "t0x5h40"));
    // 風向/風速なしでいきなり項目が来る局もある (c/s 形式)
    CHECK(parse(frame_s("APRS", "=3540.00N/13945.00E_c180s003g008t050"), &p));
    CHECK(p.weather.windDirDeg == 180 && p.weather.hasWindSpeed && p.weather.hasGust && p.weather.hasTemp);
    // 気象のシンボルでなければ、ddd/sss は進行方向・速度のまま
    CHECK(parse(frame_s("APRS", "!3540.00N/13945.00E>090/010g005t077"), &p) && !p.weather.present && p.hasSpeed && p.speedKnots == 10); }

  // ---- 気象データ: 位置なし (データ種別 '_') ----
  { AprsPacket p;
    CHECK(parse(frame_s("APRS", "_10090556c220s004g005t077r000p000P000h50b09900wRSW"), &p));
    CHECK(p.kind == APRS_KIND_WEATHER && !p.hasPos && p.weather.present && p.dti == '_');
    CHECK(p.weather.windDirDeg == 220 && p.weather.hasTemp && p.weather.humidityPct == 50 && !strcmp(p.comment, "wRSW"));
    CHECK(parse(frame_s("APRS", "_1009"), &p) && p.kind == APRS_KIND_OTHER && !p.weather.present);   // 短すぎる
    CHECK(parse(frame_s("APRS", "_1009x556t077"), &p) && p.kind == APRS_KIND_OTHER && !p.weather.present);
    char s[200]; auto f = frame_s("APRS", "_10090556t077");
    CHECK(aprs_tnc2_string(f.data(), f.size(), s, sizeof(s)) > 0 && strstr(s, ":_10090556t077")); }

  // ---- メッセージ ----
  { AprsPacket p;
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :Hello 73{12"), &p));
    CHECK(p.kind == APRS_KIND_MESSAGE && p.msg.present && !strcmp(p.msg.addressee, "NOCALL-7") && !strcmp(p.msg.text, "Hello 73") && !strcmp(p.msg.msgNo, "12"));
    CHECK(!p.msg.isAck && !p.msg.isRej && p.msg.ackNo[0] == 0 && !p.hasPos && !strcmp(p.comment, ":NOCALL-7 :Hello 73{12"));
    CHECK(parse(frame_s("APRS", ":NOCALL   :no number"), &p) && !strcmp(p.msg.addressee, "NOCALL") && !strcmp(p.msg.text, "no number") && p.msg.msgNo[0] == 0);
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack12"), &p) && p.msg.isAck && !strcmp(p.msg.ackNo, "12") && p.msg.text[0] == 0);
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :rejAB3"), &p) && p.msg.isRej && !strcmp(p.msg.ackNo, "AB3"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :acknowledged{5"), &p) && !p.msg.isAck && !strcmp(p.msg.text, "acknowledged"));   // ack で始まる普通の本文
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :reply{MM}AA"), &p) && !strcmp(p.msg.msgNo, "MM") && !strcmp(p.msg.ackNo, "AA") && !strcmp(p.msg.text, "reply"));   // 返信Ack形式
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack7}"), &p) && p.msg.isAck && !strcmp(p.msg.ackNo, "7"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack12}AB"), &p) && p.msg.isAck && !strcmp(p.msg.ackNo, "12"));          // 返信Ack形式のAck
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ackABCDE"), &p) && p.msg.isAck && !strcmp(p.msg.ackNo, "ABCDE"));        // 番号は5文字まで
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ackermann"), &p) && !p.msg.isAck && !strcmp(p.msg.text, "ackermann"));   // 6文字以上は本文
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :rejected123"), &p) && !p.msg.isRej && !strcmp(p.msg.text, "rejected123"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack1}2}"), &p) && !p.msg.isAck);                                         // 形が違う
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack OK"), &p) && !p.msg.isAck && !strcmp(p.msg.text, "ack OK"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :ack"), &p) && !p.msg.isAck && !strcmp(p.msg.text, "ack"));
    CHECK(parse(frame_s("APRS", ":BLN1     :Net tonight 21:00"), &p) && !strcmp(p.msg.addressee, "BLN1") && !strcmp(p.msg.text, "Net tonight 21:00"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :\xE3\x81\x82" "abc{1"), &p) && !strcmp(p.msg.text, "???abc"));   // ASCII以外は '?'
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :{1"), &p) && p.msg.present && p.msg.text[0] == 0 && !strcmp(p.msg.msgNo, "1"));
    CHECK(parse(frame_s("APRS", ":NOCALL-7 :abc{123456"), &p) && !strcmp(p.msg.msgNo, "12345"));        // 番号は最大5文字
    std::string longText(80, 'x'); std::string inf = ":NOCALL-7 :" + longText;
    CHECK(parse(frame_s("APRS", inf.c_str()), &p) && strlen(p.msg.text) == APRS_MSG_TEXT_MAX);
    CHECK(parse(frame_s("APRS", ":NOCALL-7 Hello"), &p) && p.kind == APRS_KIND_OTHER && !p.msg.present);   // 形が違う
    CHECK(parse(frame_s("APRS", ":         :x"), &p) && !p.msg.present);                                    // 宛先が空
    CHECK(parse(frame_s("APRS", ":SHORT"), &p) && !p.msg.present); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
