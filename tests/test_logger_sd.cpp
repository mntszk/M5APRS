// test_logger_sd.cpp  (パソコン上のテスト用。スケッチには入れない)
// logger_sd.cpp を、stubs/ のメモリ上のSDカードで動かして、Loggerとのつなぎを確認する。
#include "test_cfg.h"
#include "Arduino.h"
#include "SD.h"
#include "logger_sd.h"
#include "packet_build.h"
#include <stdio.h>
#include <string.h>

StubHw g_stub; StubCard g_card; SDClass SD; SPIClass SPI;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static Logger lg;

int main() {
  test_cfg_setup();
  LogTime t; t.valid = true; t.year = 2026; t.month = 10; t.day = 9; t.hour = 1; t.minute = 2; t.second = 3;
  logger_init(&lg, logger_sd_sink(), "/aprs");

  // カードなし: 失敗として扱い、記録は溜まる
  g_card.present = false;
  logger_log_event(&lg, "boot", "", &t, 0);
  CHECK(logger_service(&lg, 1000, false, &t) == 0 && !logger_sd_ok(&lg) && g_card.beginCalls == 1);
  CHECK(logger_service(&lg, 5000, false, &t) == 0 && g_card.beginCalls == 1);               // 10秒は再試行しない
  // カードを挿入: 再試行で、ディレクトリとヘッダつきファイルが作られる
  g_card.present = true;
  CHECK(logger_service(&lg, 12000, false, &t) >= 0);
  while (logger_pending_bytes(&lg)) logger_service(&lg, 12000 + 20000, false, &t);
  CHECK(logger_sd_ok(&lg) && g_card.mounted && g_card.dirs.count("/aprs"));
  std::string ops = g_card.files["/aprs/ops_20261009.log"];
  CHECK(ops.find("time_utc,uptime_ms,event,detail\n") == 0 && ops.find("boot") != std::string::npos && ops.find("sd_error") != std::string::npos && ops.find("sd_ok") != std::string::npos);

  // 追記: ヘッダを重ねない・マウントし直さない
  int b0 = g_card.beginCalls;
  uint8_t info[] = "!3539.52N/13944.72E>x"; uint8_t f[AX25_FRAME_MAX]; Ax25Params ap = { "NOCALL", 9, "APRS", 0, "WIDE1-1" };
  size_t n = ax25_build_ui_frame(ap, info, sizeof(info) - 1, f, sizeof(f));
  logger_log_tx(&lg, f, n, &t, 20000); logger_service(&lg, 50000, false, &t);
  CHECK(g_card.beginCalls == b0);
  ops = g_card.files["/aprs/ops_20261009.log"];
  size_t first = ops.find("time_utc"), second = ops.find("time_utc", first + 1);
  CHECK(first == 0 && second == std::string::npos && ops.find("tx,\"NOCALL-9>APRS,WIDE1-1:") != std::string::npos);

  // 書き込み失敗 → アンマウント → 次回マウントし直して回復
  g_card.failWrite = true;
  logger_log_event(&lg, "x", "y", &t, 1); int e0 = g_card.endCalls;
  CHECK(logger_service(&lg, 60000, false, &t) == 0 && !logger_sd_ok(&lg) && g_card.endCalls == e0 + 1 && !g_card.mounted);
  g_card.failWrite = false;
  while (logger_pending_bytes(&lg)) logger_service(&lg, 100000, false, &t), t.second = t.second;
  CHECK(logger_sd_ok(&lg) && g_card.mounted);
  CHECK(g_card.files["/aprs/ops_20261009.log"].find(",x,y") != std::string::npos);

  // ファイルを開けない
  g_card.failOpen = true; logger_log_event(&lg, "z", "", &t, 2);
  CHECK(logger_service(&lg, 200000, false, &t) == 0 && !logger_sd_ok(&lg));
  g_card.failOpen = false;
  while (logger_pending_bytes(&lg)) logger_service(&lg, 300000, false, &t);
  CHECK(logger_sd_ok(&lg));

  // 状態ファイル: 一時ファイルに少しずつ書いて置き換え、読み戻す
  { const uint8_t a[] = { 1, 2, 3 }, b[] = { 4, 5 };
    uint8_t rb[16];
    CHECK(logger_sd_read_file("/aprs", "state.bin", rb, sizeof(rb)) == 0);            // まだない
    CHECK(logger_sd_file_begin("/aprs", "state.bin") && logger_sd_file_append("/aprs", "state.bin", a, 3) && logger_sd_file_append("/aprs", "state.bin", b, 2));
    CHECK(g_card.files["/aprs/state.bin.tmp"].size() == 5 && !g_card.files.count("/aprs/state.bin"));
    CHECK(logger_sd_file_commit("/aprs", "state.bin") && !g_card.files.count("/aprs/state.bin.tmp"));
    CHECK(logger_sd_read_file("/aprs", "state.bin", rb, sizeof(rb)) == 5 && rb[0] == 1 && rb[4] == 5);
    CHECK(logger_sd_read_file("/aprs", "state.bin", rb, 4) == 0);                      // 入りきらない
    // 2回目: 書き始めで一時ファイルを空にする。置き換えに失敗しても前のファイルは残る
    g_card.files["/aprs/state.bin.tmp"] = "garbage";
    CHECK(logger_sd_file_begin("/aprs", "state.bin") && g_card.files["/aprs/state.bin.tmp"].empty());
    CHECK(logger_sd_file_append("/aprs", "state.bin", b, 2));
    g_card.failRename = true; CHECK(!logger_sd_file_commit("/aprs", "state.bin")); g_card.failRename = false;
    CHECK(logger_sd_file_begin("/aprs", "state.bin") && logger_sd_file_append("/aprs", "state.bin", b, 2) && logger_sd_file_commit("/aprs", "state.bin"));
    CHECK(logger_sd_read_file("/aprs", "state.bin", rb, sizeof(rb)) == 2 && rb[0] == 4);
    g_card.failWrite = true; CHECK(!logger_sd_file_append("/aprs", "state.bin", a, 3)); g_card.failWrite = false;
    g_card.present = false; g_card.mounted = false; CHECK(!logger_sd_file_begin("/aprs", "state.bin") && logger_sd_read_file("/aprs", "state.bin", rb, sizeof(rb)) == 0);
    g_card.present = true; }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
