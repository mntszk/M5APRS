// test_logger.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "logger.h"
#include "nmea.h"
#include "packet_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <map>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// ---- 試験用のSD (メモリ上のファイル) ----
struct MockSd {
  std::map<std::string, std::string> files;
  bool fail = false; int appends = 0;
  size_t lastBatch = 0;
};
static bool mock_append(void* c, const char* dir, const char* name, const char* header, const char* data, size_t len) {
  MockSd* m = (MockSd*)c; m->appends++;
  if (m->fail) return false;
  std::string path = std::string(dir) + "/" + name;
  std::string& f = m->files[path];
  if (f.empty() && header) f = header;
  f.append(data, len); m->lastBatch = len;
  return true;
}
static int lockResult = 1, lockCalls = 0, unlockCalls = 0;
static bool g_lock() { lockCalls++; return lockResult != 0; }
static void g_unlock() { unlockCalls++; }

static std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> v; size_t p = 0;
  while (p < s.size()) { size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size(); v.push_back(s.substr(p, e - p)); p = e + 1; }
  return v;
}
// CSVの1行を項目に分ける (引用符に対応)
static std::vector<std::string> csv(const std::string& l) {
  std::vector<std::string> f; std::string cur; bool q = false;
  for (size_t i = 0; i < l.size(); i++) {
    char c = l[i];
    if (q) { if (c == '"') { if (i + 1 < l.size() && l[i + 1] == '"') { cur += '"'; i++; } else q = false; } else cur += c; }
    else if (c == '"') q = true; else if (c == ',') { f.push_back(cur); cur.clear(); } else cur += c;
  }
  f.push_back(cur); return f;
}

static std::vector<uint8_t> frame(const char* src, int ssid, const char* dest, const char* digis, const uint8_t* info, size_t n) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { src, (uint8_t)ssid, dest, 0, digis };
  size_t len = ax25_build_ui_frame(p, info, n, f, sizeof(f));
  return std::vector<uint8_t>(f, f + len - 2);     // FCSなし
}
static std::vector<uint8_t> frame_s(const char* info, const char* src = "N0CALL", int ssid = 7) {
  return frame(src, ssid, "APRS", "WIDE1-1", (const uint8_t*)info, strlen(info));
}

static LogTime T(int y, int mo, int d, int h, int mi, int s) { LogTime t; t.valid = true; t.year = (uint16_t)y; t.month = (uint8_t)mo; t.day = (uint8_t)d; t.hour = (uint8_t)h; t.minute = (uint8_t)mi; t.second = (uint8_t)s; return t; }
static LogTime NOT() { LogTime t; memset(&t, 0, sizeof(t)); return t; }

static Logger lg;   // 大きいのでstatic

static void setup(MockSd* sd) {
  LogSink s = { sd, mock_append };
  logger_init(&lg, &s, "/aprs");
}

int main() {
  test_cfg_setup();
  // ===== CSVの項目 =====
  { char o[64];
    CHECK(logger_csv_field(o, sizeof(o), "abc") == 3 && strcmp(o, "abc") == 0);
    CHECK(logger_csv_field(o, sizeof(o), "a,b") == 5 && strcmp(o, "\"a,b\"") == 0);
    CHECK(strcmp((logger_csv_field(o, sizeof(o), "say \"hi\""), o), "\"say \"\"hi\"\"\"") == 0);
    CHECK(logger_csv_field(o, sizeof(o), "") == 0 && o[0] == 0);
    CHECK(logger_csv_field(o, sizeof(o), "line\nbreak") > 0 && o[0] == '"');
    CHECK(logger_csv_field(o, 4, "abcd") == 0);                                        // 入らない
    CHECK(logger_csv_field(o, 5, "abcd") == 4);
    CHECK(logger_csv_field(o, 5, "a,bc") == 0); }

  // ===== 受信ログの1行: 非圧縮位置 =====
  { auto f = frame_s("!3539.52N/13944.72E>090/012/A=000200M5,Stack");
    AprsPacket p; CHECK(aprs_parse_frame(f.data(), f.size(), &p));
    LogOwnPos own = { true, 35.0, 139.0 }; LogTime t = T(2026, 10, 9, 9, 30, 45);
    char line[1500]; size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, &own);
    CHECK(n > 0 && line[n - 1] == '\n' && n == strlen(line));
    auto c = csv(std::string(line, n - 1));
    CHECK(c.size() == 12);
    CHECK(c[0] == "2026-10-09T09:30:45Z" && c[1] == "N0CALL-7" && c[2] == "WIDE1-1" && c[3] == "uncompressed");
    CHECK(c[4] == "35.65867" && c[5] == "139.74533");
    CHECK(c[6] == "61.0");
    double dist = geo_distance_km(35.0, 139.0, 35 + 39.52 / 60, 139 + 44.72 / 60);
    char exp[32]; snprintf(exp, sizeof(exp), "%.2f", dist); CHECK(c[7] == exp);
    int br = (int)lround(geo_bearing_deg(35.0, 139.0, 35 + 39.52 / 60, 139 + 44.72 / 60)) % 360;
    snprintf(exp, sizeof(exp), "%d", br); CHECK(c[8] == exp);
    CHECK(c[9] == "/>" && c[10] == "M5,Stack");                                          // コメントの , は引用符で保護される
    CHECK(c[11] == "N0CALL-7>APRS,WIDE1-1:!3539.52N/13944.72E>090/012/A=000200M5,Stack");
    // 自局位置が不明: 距離・方位が空欄
    LogOwnPos none = { false, 0, 0 };
    n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, &none);
    c = csv(std::string(line, n - 1)); CHECK(c.size() == 12 && c[7] == "" && c[8] == "" && c[4] == "35.65867");
    n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, NULL);
    c = csv(std::string(line, n - 1)); CHECK(c[7] == "" && c[8] == "");
    // 時刻が不明: 空欄
    LogTime nt = NOT();
    n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &nt, &own);
    c = csv(std::string(line, n - 1)); CHECK(c[0] == "" && c[1] == "N0CALL-7"); }
  // 高度がないパケット: 高度だけ空欄
  { auto f = frame_s("!3539.52N/13944.72E>test");
    AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    LogOwnPos own = { true, 35.0, 139.0 }; LogTime t = T(2026, 1, 2, 3, 4, 5);
    char line[1500]; size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, &own);
    auto c = csv(std::string(line, n - 1)); CHECK(c.size() == 12 && c[6] == "" && c[7] != "" && c[8] != ""); }
  // 位置のないパケット
  { auto f = frame_s(":NOCALL   :Hello{1");
    AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    LogOwnPos own = { true, 35.0, 139.0 }; LogTime t = T(2026, 1, 2, 3, 4, 5);
    char line[1500]; size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, &own);
    auto c = csv(std::string(line, n - 1));
    CHECK(c.size() == 12 && c[3] == "message" && c[4] == "" && c[5] == "" && c[6] == "" && c[7] == "" && c[8] == "" && c[9] == "");
    CHECK(c[10] == ":NOCALL   :Hello{1" && c[11] == "N0CALL-7>APRS,WIDE1-1::NOCALL   :Hello{1"); }
  // Mic-E
  { const uint8_t info[] = { '`', '(', '_', 'f', 'n', '"', 'O', 'j', '/' };
    auto f = frame("N0CALL", 0, "S32UVT", "", info, sizeof(info));
    AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    LogTime t = T(2026, 1, 2, 3, 4, 5); char line[1500];
    size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, NULL);
    auto c = csv(std::string(line, n - 1));
    CHECK(c[3] == "mic-e" && c[4] == "33.42733" && c[5] == "-112.12900" && c[9] == "/j");
    CHECK(c[11] == "N0CALL>S32UVT:`(_fn\"Oj/"); }
  // 圧縮形式の位置
  { auto f = frame_s("!/5L!!<*e7>7P[hi");
    AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    LogTime t = T(2026, 1, 2, 3, 4, 5); char line[1500];
    size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, NULL);
    auto c = csv(std::string(line, n - 1));
    CHECK(c[3] == "compressed" && c[4] == "49.50000" && c[5] == "-72.75000" && c[9] == "/>" && c[10] == "hi"); }
  // シンボルが , のとき
  { auto f = frame_s("!3500.00N/13900.00E,x");
    AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p); LogTime t = T(2026, 1, 2, 3, 4, 5); char line[1500];
    size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, NULL);
    auto c = csv(std::string(line, n - 1)); CHECK(c.size() == 12 && c[9] == "/," && c[10] == "x"); }
  // 解釈できないフレーム: 16進で残す
  { uint8_t junk[20]; for (int i = 0; i < 20; i++) junk[i] = 0x40;
    AprsPacket p; bool ok = aprs_parse_frame(junk, 20, &p); CHECK(!ok);
    LogTime t = T(2026, 1, 2, 3, 4, 5); char line[1500];
    size_t n = logger_format_rx_line(line, sizeof(line), NULL, junk, 20, &t, NULL);
    auto c = csv(std::string(line, n - 1)); CHECK(c.size() == 12 && c[3] == "invalid" && c[11].substr(0, 8) == "HEX:4040"); }
  // 長いコメント・長い生パケットでも1行に収まる
  { std::string big = "!3500.00N/13900.00E>";
    for (int i = 0; i < 100; i++) big += (char)('!' + i % 60);
    auto f = frame_s(big.c_str()); AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    LogTime t = T(2026, 1, 2, 3, 4, 5); char line[1500];
    size_t n = logger_format_rx_line(line, sizeof(line), &p, f.data(), f.size(), &t, NULL);
    CHECK(n > 0 && n < 1500 && line[n - 1] == '\n' && strchr(line, '\n') == line + n - 1); }

  // ===== 動作ログの1行 =====
  { char line[200]; LogTime t = T(2026, 10, 9, 1, 2, 3);
    size_t n = logger_format_event_line(line, sizeof(line), "gps", "fix", &t, 12345);
    CHECK(strcmp(line, "2026-10-09T01:02:03Z,12345,gps,fix\n") == 0 && n == strlen(line));
    LogTime nt = NOT();
    logger_format_event_line(line, sizeof(line), "tx", "a,b", &nt, 7);
    CHECK(strcmp(line, ",7,tx,\"a,b\"\n") == 0);
    CHECK(logger_format_event_line(line, 10, "event", "a long long detail", &nt, 1) == 0); }

  // ===== ファイルへの書き込み、ヘッダ、日付ごとの分割 =====
  { MockSd sd; setup(&sd);
    LogOwnPos own = { true, 35.0, 139.0 };
    LogTime t1 = T(2026, 10, 9, 23, 59, 58), t2 = T(2026, 10, 10, 0, 0, 1), nt = NOT();
    auto f = frame_s("!3539.52N/13944.72E>a"); AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    CHECK(logger_log_rx(&lg, &p, f.data(), f.size(), &t1, &own, 100));
    CHECK(logger_log_rx(&lg, &p, f.data(), f.size(), &t1, &own, 200));
    CHECK(logger_log_rx(&lg, &p, f.data(), f.size(), &t2, &own, 300));
    CHECK(logger_log_rx(&lg, &p, f.data(), f.size(), &nt, &own, 400));
    CHECK(logger_log_event(&lg, "boot", "v1", &t1, 0));
    CHECK(logger_pending_bytes(&lg) > 0);
    CHECK(logger_service(&lg, 1000, true, &t1) == 0 && sd.appends == 0);                // 受信中は書かない
    CHECK(logger_service(&lg, 1000, false, &t1) == 2 && sd.appends == 1);               // 同じファイル向けの2件をまとめて1回で
    CHECK(logger_service(&lg, 1001, false, &t1) == 1);                                  // 日付が変わった分
    CHECK(logger_service(&lg, 1002, false, &t1) == 1);                                  // 日付不明の分
    CHECK(logger_service(&lg, 1003, false, &t1) == 1);                                  // 動作ログ
    CHECK(logger_service(&lg, 1004, false, &t1) == 0 && logger_pending_bytes(&lg) == 0);
    CHECK(sd.files.size() == 4);
    CHECK(sd.files.count("/aprs/rx_20261009.csv") && sd.files.count("/aprs/rx_20261010.csv") &&
          sd.files.count("/aprs/rx_nodate.csv") && sd.files.count("/aprs/ops_20261009.log"));
    auto l = lines(sd.files["/aprs/rx_20261009.csv"]);
    CHECK(l.size() == 3 && l[0] == "time_utc,src,via,type,lat,lon,alt_m,dist_km,bearing_deg,symbol,comment,raw");   // ヘッダは先頭に1回だけ
    CHECK(lines(sd.files["/aprs/ops_20261009.log"])[0] == "time_utc,uptime_ms,event,detail");
    CHECK(lines(sd.files["/aprs/ops_20261009.log"]).size() == 2);
    // 追記: ヘッダは重ねない
    logger_log_rx(&lg, &p, f.data(), f.size(), &t1, &own, 500); logger_service(&lg, 2000, false, &t1);
    CHECK(lines(sd.files["/aprs/rx_20261009.csv"]).size() == 4); }

  // ===== 送信の記録 (FCS込みのフレームを渡す) =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    uint8_t full[AX25_FRAME_MAX]; Ax25Params ap = { "NOCALL", 9, "APRS", 0, "WIDE1-1" };
    const char* info = "!3539.52N/13944.72E>";
    size_t n = ax25_build_ui_frame(ap, (const uint8_t*)info, strlen(info), full, sizeof(full));
    CHECK(logger_log_tx(&lg, full, n, &t, 5000)); logger_service(&lg, 6000, false, &t);
    auto l = lines(sd.files["/aprs/ops_20261009.log"]);
    CHECK(l.size() == 2 && l[1] == "2026-10-09T01:02:03Z,5000,tx,\"NOCALL-9>APRS,WIDE1-1:!3539.52N/13944.72E>\"");
    CHECK(logger_log_tx(&lg, full, 1, &t, 5000)); logger_service(&lg, 6001, false, &t);
    CHECK(lines(sd.files["/aprs/ops_20261009.log"]).back().find("unparsable") != std::string::npos); }

  // ===== GPS状態は変化したときだけ =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    logger_note_gps_status(&lg, GPS_NO_DATA, &t, 0); logger_note_gps_status(&lg, GPS_NO_DATA, &t, 1);
    logger_note_gps_status(&lg, GPS_NO_FIX, &t, 2); logger_note_gps_status(&lg, GPS_FIX, &t, 3);
    logger_note_gps_status(&lg, GPS_FIX, &t, 4); logger_note_gps_status(&lg, GPS_STALE, &t, 5);
    logger_note_gps_status(&lg, GPS_NO_DATA, &t, 6);
    logger_service(&lg, 10, false, &t);
    auto l = lines(sd.files["/aprs/ops_20261009.log"]);
    CHECK(l.size() == 1 + 5);
    CHECK(l[1].find("gps,no_data") != std::string::npos && l[2].find("gps,no_fix") != std::string::npos &&
          l[3].find("gps,fix") != std::string::npos && l[4].find("gps,stale") != std::string::npos && l[5].find("gps,no_data") != std::string::npos); }

  // ===== カウンタの増加だけ記録 =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    logger_note_counters(&lg, 0, 0, 0, &t, 0); CHECK(logger_pending_bytes(&lg) == 0);
    logger_note_counters(&lg, 120, 1, 0, &t, 1); logger_note_counters(&lg, 120, 1, 0, &t, 2);
    logger_note_counters(&lg, 150, 1, 3, &t, 3);
    logger_service(&lg, 10, false, &t);
    auto l = lines(sd.files["/aprs/ops_20261009.log"]);
    CHECK(l.size() == 1 + 4);
    CHECK(l[1].find("rx_overrun,+120 total=120") != std::string::npos && l[2].find("rx_discard,+1 total=1") != std::string::npos &&
          l[3].find("rx_overrun,+30 total=150") != std::string::npos && l[4].find("rx_queue_drop,+3 total=3") != std::string::npos); }

  // ===== SDの失敗と回復 =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    auto f = frame_s("!3539.52N/13944.72E>a"); AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    sd.fail = true;
    logger_log_rx(&lg, &p, f.data(), f.size(), &t, NULL, 0);
    CHECK(logger_sd_ok(&lg));
    CHECK(logger_service(&lg, 1000, false, &t) == 0 && sd.appends == 1 && !logger_sd_ok(&lg) && lg.writeErrors == 1);
    CHECK(logger_service(&lg, 5000, false, &t) == 0 && sd.appends == 1);                  // 10秒たつまで再試行しない
    CHECK(logger_service(&lg, 11000, false, &t) == 0 && sd.appends == 2 && lg.writeErrors == 2);
    logger_log_rx(&lg, &p, f.data(), f.size(), &t, NULL, 0);                              // 失敗中も受信の記録は溜まる
    sd.fail = false;
    CHECK(logger_service(&lg, 15000, false, &t) == 0);                                    // まだ待ち時間中
    CHECK(logger_service(&lg, 21001, false, &t) == 1);                                    // 回復: 溜めた分を順に書く (ファイルが変わるごとに1回)
    CHECK(logger_sd_ok(&lg));
    CHECK(logger_service(&lg, 21002, false, &t) == 1);                                    // sd_error の記録
    CHECK(logger_service(&lg, 21003, false, &t) == 1);                                    // 失敗中に溜まった受信
    CHECK(logger_service(&lg, 21004, false, &t) == 1);                                    // sd_ok の記録
    CHECK(logger_service(&lg, 21005, false, &t) == 0 && logger_pending_bytes(&lg) == 0);
    auto l = lines(sd.files["/aprs/ops_20261009.log"]);
    CHECK(l.size() == 3 && l[1].find("sd_error") != std::string::npos && l[2].find("sd_ok") != std::string::npos);   // 失敗は1回だけ記録
    CHECK(lines(sd.files["/aprs/rx_20261009.csv"]).size() == 3); }

  // ===== 待ち行列があふれたとき =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    auto f = frame_s("!3539.52N/13944.72E>a"); AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    sd.fail = true; logger_service(&lg, 0, false, &t);                                    // (空なので何もしない)
    // 古いものから捨てて、新しいものを残す (コメントに通し番号を入れて確かめる)
    int ok = 0;
    for (int i = 0; i < 200; i++) {
      char c[40]; snprintf(c, sizeof(c), "!3539.52N/13944.72E>n%03d", i);
      auto fi = frame_s(c); AprsPacket pi; aprs_parse_frame(fi.data(), fi.size(), &pi);
      if (logger_log_rx(&lg, &pi, fi.data(), fi.size(), &t, NULL, 0)) ok++;
    }
    CHECK(ok == 200 && lg.entriesDropped > 0);
    CHECK(logger_pending_bytes(&lg) <= LOGGER_QUEUE_BYTES);
    uint32_t kept = 200 - lg.entriesDropped;
    sd.fail = false;
    for (uint32_t now = 100000; logger_pending_bytes(&lg) > 0 && now < 1000000; now += 1) logger_service(&lg, now, false, &t);
    auto l = lines(sd.files["/aprs/rx_20261009.csv"]); CHECK(l.size() == 1 + kept);
    CHECK(l.size() > 2 && l.back().find("n199") != std::string::npos);                      // 最後に入れたものが残る
    { char first[8]; snprintf(first, sizeof(first), "n%03u", 200 - kept); CHECK(l[1].find(first) != std::string::npos); }   // 残ったのは新しい順に kept 件
    logger_service(&lg, 2000000, false, &t);
    bool reported = false; for (auto& s : lines(sd.files["/aprs/ops_20261009.log"])) if (s.find("log_dropped") != std::string::npos) reported = true;
    CHECK(reported); }

  // ===== SPIバスの排他 =====
  { MockSd sd; setup(&sd); LogTime t = T(2026, 10, 9, 1, 2, 3);
    LogBusGuard g = { g_lock, g_unlock }; logger_set_bus_guard(&lg, &g);
    logger_log_event(&lg, "boot", "", &t, 0);
    lockResult = 0; lockCalls = unlockCalls = 0;
    CHECK(logger_service(&lg, 1, false, &t) == 0 && sd.appends == 0 && lockCalls == 1 && unlockCalls == 0);   // 画面が使用中: 見送る
    lockResult = 1;
    CHECK(logger_service(&lg, 2, false, &t) == 1 && sd.appends == 1 && lockCalls == 2 && unlockCalls == 1);
    logger_log_event(&lg, "x", "", &t, 0); sd.fail = true; lockCalls = unlockCalls = 0;
    CHECK(logger_service(&lg, 20000, false, &t) == 0 && unlockCalls == 1);                  // 失敗しても解放する
    logger_set_bus_guard(&lg, NULL); sd.fail = false; CHECK(logger_service(&lg, 40000, false, &t) >= 1); }

  // ===== 長時間・乱数: 件数が合う =====
  { MockSd sd; setup(&sd); srand(1); LogTime t = T(2026, 10, 9, 1, 2, 3);
    auto f = frame_s("!3539.52N/13944.72E>a"); AprsPacket p; aprs_parse_frame(f.data(), f.size(), &p);
    uint32_t now = 0; int queued = 0, written = 0;
    for (int i = 0; i < 5000; i++) {
      now += 100;
      if (rand() % 3 == 0) { if (rand() % 2) { if (logger_log_rx(&lg, &p, f.data(), f.size(), &t, NULL, now)) queued++; }
                             else { if (logger_log_event(&lg, "e", "d", &t, now)) queued++; } }
      sd.fail = (i % 700) > 650;
      written += logger_service(&lg, now, (rand() % 4) == 0, &t);
    }
    sd.fail = false; for (int k = 0; k < 2000; k++) written += logger_service(&lg, now += 20000, false, &t);
    CHECK(logger_pending_bytes(&lg) == 0);
    CHECK(lg.entriesWritten == (uint32_t)written);
    int rows = 0; for (auto& kv : sd.files) { int n = (int)lines(kv.second).size() - 1; rows += n; }
    CHECK(rows == written); CHECK(lg.entriesWritten == lg.entriesQueued); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
