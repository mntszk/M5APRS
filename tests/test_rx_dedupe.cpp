// test_rx_dedupe.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "rx_dedupe.h"
#include "packet_build.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// FCSなしのフレーム。usedDigi = デジピーターの「中継済み」ビットを立てる
static std::vector<uint8_t> fr(const char* src, int ssid, const char* dest, const char* path, const char* info, bool usedDigi = false) {
  uint8_t f[AX25_FRAME_MAX]; Ax25Params p = { src, (uint8_t)ssid, dest, 0, path };
  size_t n = ax25_build_ui_frame(p, (const uint8_t*)info, strlen(info), f, sizeof(f));
  if (n < 2) { printf("frame build failed: %s\n", info); return std::vector<uint8_t>(); }
  std::vector<uint8_t> v(f, f + n - 2);
  if (usedDigi && path[0]) v[20] |= 0x80;
  return v;
}

int main() {
  test_cfg_setup();
  RxDedupe d; rx_dedupe_init(&d);
  auto a1 = fr("TEST1", 0, "STQRP0", "DIGI1-1", "`C0 l!B-/`\"4D}_(\r", true);
  auto a2 = fr("TEST1", 0, "STQRP0", "DIGI2-3", "`C0 l!B-/`\"4D}_(\r", true);   // 別のデジピーター経由
  auto a3 = fr("TEST1", 0, "STQRP0", "", "`C0 l!B-/`\"4D}_(\r");                  // 直接 (送信元が最後のアドレス)
  CHECK(!rx_dedupe_check(&d, a1.data(), a1.size(), 1000));
  CHECK(rx_dedupe_check(&d, a2.data(), a2.size(), 2000));                          // 経路が違っても同じパケット
  CHECK(rx_dedupe_check(&d, a3.data(), a3.size(), 3000));
  CHECK(d.dupes == 2);
  CHECK(rx_dedupe_check(&d, a1.data(), a1.size(), 1000 + RX_DUPE_MS - 1));         // 最初に受けてから30秒以内
  CHECK(!rx_dedupe_check(&d, a1.data(), a1.size(), 1000 + RX_DUPE_MS));            // 30秒たったら新しいもの (送り直し・次のビーコン)
  // 違うものは重複にしない: 宛先 (Mic-Eの緯度)、送信元のSSID、情報欄
  auto b1 = fr("TEST1", 0, "STQRP1", "DIGI1-1", "`C0 l!B-/`\"4D}_(\r");
  auto b2 = fr("TEST1", 7, "STQRP0", "DIGI1-1", "`C0 l!B-/`\"4D}_(\r");
  auto b3 = fr("TEST1", 0, "STQRP0", "DIGI1-1", "`C0 l!C-/`\"4D}_(\r");
  CHECK(!rx_dedupe_check(&d, b1.data(), b1.size(), 50000));
  CHECK(!rx_dedupe_check(&d, b2.data(), b2.size(), 50000));
  CHECK(!rx_dedupe_check(&d, b3.data(), b3.size(), 50000));
  // メッセージ: 同じものを2つの経路で受けても1回 (Ackを2回返さない)
  auto m1 = fr("TEST5", 8, "APK005", "DIGI2-3", ":IGATE-10 :Hello{61", true);
  auto m2 = fr("TEST5", 8, "APK005", "DIGI2-3,DIGI1-1", ":IGATE-10 :Hello{61", true);
  CHECK(!rx_dedupe_check(&d, m1.data(), m1.size(), 60000) && rx_dedupe_check(&d, m2.data(), m2.size(), 64000));
  // たくさん受けても、覚えているのは直近 RX_DUPE_SLOTS 件
  for (int i = 0; i < RX_DUPE_SLOTS; i++) { char in[16]; snprintf(in, sizeof(in), ">s%d", i); auto x = fr("N0CALL", 0, "APRS", "", in); CHECK(!rx_dedupe_check(&d, x.data(), x.size(), 70000)); }
  CHECK(!rx_dedupe_check(&d, m1.data(), m1.size(), 70001));                         // 押し出された
  // 壊れたフレーム
  uint8_t bad[20]; memset(bad, 0x40, sizeof(bad));
  CHECK(!rx_dedupe_check(&d, bad, sizeof(bad), 0) && !rx_dedupe_check(&d, bad, 5, 0) && !rx_dedupe_check(&d, NULL, 0, 0));
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
