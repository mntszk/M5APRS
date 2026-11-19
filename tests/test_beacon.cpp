// test_beacon.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "beacon_sched.h"
#include <stdio.h>
#include <string.h>
static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// 時刻を進めながら poll し、最初に IDLE 以外になった時刻と結果を返す
static BeaconDecision run_until(BeaconSched* b, uint32_t* t, uint32_t limitMs, bool busy, bool canTx) {
  for (uint32_t end = *t + limitMs; *t <= end; *t += 100) {
    BeaconDecision d = beacon_poll(b, *t, busy, canTx);
    if (d != BEACON_IDLE) return d;
  }
  return BEACON_IDLE;
}

int main() {
  test_cfg_setup();
  // ---- 選択肢 ----
  CHECK(AUTO_TX_CHOICES == 11 && AUTO_TX_FLEX == 10);
  CHECK(auto_tx_interval_min(AUTO_TX_FLEX) == 0 && auto_tx_is_flex(AUTO_TX_FLEX) && !auto_tx_is_flex(9) && auto_tx_ack_enabled(AUTO_TX_FLEX));
  CHECK(auto_tx_beacon(AUTO_TX_FLEX) && auto_tx_beacon(2) && !auto_tx_beacon(AUTO_TX_OFF) && !auto_tx_beacon(AUTO_TX_ACK_ONLY) && !auto_tx_beacon(11));
  CHECK(auto_tx_interval_min(AUTO_TX_OFF) == 0 && auto_tx_interval_min(AUTO_TX_ACK_ONLY) == 0);
  CHECK(auto_tx_interval_min(2) == 1 && auto_tx_interval_min(5) == 5 && auto_tx_interval_min(9) == 60 && auto_tx_interval_min(11) == 0);
  CHECK(auto_tx_interval_ms(2) == 60000u && auto_tx_interval_ms(9) == 3600000u);
  CHECK(!auto_tx_ack_enabled(AUTO_TX_OFF) && auto_tx_ack_enabled(AUTO_TX_ACK_ONLY) && auto_tx_ack_enabled(4) && !auto_tx_ack_enabled(99));

  // ---- オフ・Ackのみ: ビーコンは送らない ----
  { BeaconSched b; beacon_init(&b, AUTO_TX_OFF, 0); uint32_t t = 0;
    CHECK(run_until(&b, &t, 7200000, false, true) == BEACON_IDLE && beacon_remaining_ms(&b, t) == 0);
    beacon_set_mode(&b, AUTO_TX_ACK_ONLY, t); CHECK(run_until(&b, &t, 7200000, false, true) == BEACON_IDLE); }

  // ---- 間隔: 選んだ時点から間隔が過ぎたら送る (すぐには送らない) ----
  { BeaconSched b; beacon_init(&b, AUTO_TX_OFF, 0); uint32_t t = 5000;
    beacon_set_mode(&b, 3, t);                                     // 3 = 2分
    CHECK(beacon_remaining_ms(&b, t) == 120000u && beacon_remaining_ms(&b, t + 30000) == 90000u);
    CHECK(beacon_poll(&b, t + 119900, false, true) == BEACON_IDLE);
    CHECK(beacon_poll(&b, t + 120000, false, true) == BEACON_SEND && b.sent == 1);
    CHECK(beacon_remaining_ms(&b, t + 120000) == 120000u);          // 送ったら数え直し
    CHECK(beacon_poll(&b, t + 120100, false, true) == BEACON_IDLE);
    // 同じモードを選び直しても数え直さない
    beacon_set_mode(&b, 3, t + 200000); CHECK(beacon_remaining_ms(&b, t + 200000) == 40000u);
    // 違うモードを選んだら、その時点から数え直す
    beacon_set_mode(&b, 2, t + 200000); CHECK(beacon_remaining_ms(&b, t + 200000) == 60000u);
    // 手動送信で数え直す
    beacon_note_tx(&b, t + 230000); CHECK(beacon_remaining_ms(&b, t + 230000) == 60000u);
    CHECK(beacon_poll(&b, t + 289999, false, true) == BEACON_IDLE && beacon_poll(&b, t + 290000, false, true) == BEACON_SEND);
    // 起動時に間隔のモードなら、起動から数える
    BeaconSched c; beacon_init(&c, 2, 1000); CHECK(beacon_remaining_ms(&c, 1000) == 60000u && beacon_poll(&c, 61000, false, true) == BEACON_SEND); }

  // ---- 他局の送信中は止むまで待つ。30秒待っても止まなければ見送り ----
  { BeaconSched b; beacon_init(&b, 2, 0); uint32_t t = 60000;
    CHECK(beacon_poll(&b, t, true, true) == BEACON_IDLE && b.waiting);
    CHECK(beacon_remaining_ms(&b, t + 5000) == 0);
    CHECK(beacon_poll(&b, t + 10000, true, true) == BEACON_IDLE);
    CHECK(beacon_poll(&b, t + 12000, false, true) == BEACON_SEND && !b.waiting);   // 止んだら送る
    uint32_t t2 = t + 12000 + 60000;
    CHECK(beacon_poll(&b, t2, true, true) == BEACON_IDLE);
    CHECK(beacon_poll(&b, t2 + BEACON_BUSY_WAIT_MS - 100, true, true) == BEACON_IDLE);
    CHECK(beacon_poll(&b, t2 + BEACON_BUSY_WAIT_MS, true, true) == BEACON_SKIP_BUSY && b.skipped == 1 && !b.waiting);
    CHECK(beacon_remaining_ms(&b, t2 + BEACON_BUSY_WAIT_MS) == 60000u);           // 見送ったら次の間隔へ
    CHECK(beacon_poll(&b, t2 + BEACON_BUSY_WAIT_MS + 100, false, true) == BEACON_IDLE); }

  // ---- 自局が送信中・調整トーン中は、送れるようになるまで待つ (見送りにしない) ----
  { BeaconSched b; beacon_init(&b, 2, 0); uint32_t t = 60000;
    CHECK(beacon_poll(&b, t, false, false) == BEACON_IDLE && beacon_poll(&b, t + 100000, false, false) == BEACON_IDLE);
    CHECK(b.skipped == 0 && beacon_poll(&b, t + 100100, false, true) == BEACON_SEND); }

  // ---- 待っている間に手動で送ったら、待ちを解く ----
  { BeaconSched b; beacon_init(&b, 2, 0);
    beacon_poll(&b, 60000, true, true); CHECK(b.waiting);
    beacon_note_tx(&b, 61000); CHECK(!b.waiting && beacon_remaining_ms(&b, 61000) == 60000u); }

  // ---- millis() の桁あふれ ----
  { BeaconSched b; uint32_t t0 = 0xFFFF0000u; beacon_init(&b, 2, t0);
    CHECK(beacon_poll(&b, t0 + 59900, false, true) == BEACON_IDLE && beacon_poll(&b, t0 + 60000, false, true) == BEACON_SEND); }

  // ==== フレックス (八重洲・ケンウッドの初期値) ====
  FlexConfig f; memset(&f, 0, sizeof(f));
  f.set = 0x7F; f.lowSpeed = 5; f.highSpeed = 70; f.slowRateMin = 30; f.fastRateSec = 120; f.turnAngle = 28; f.turnSlope = 26; f.turnTimeSec = 30;
  const double LAT = 35.0, LON = 139.0;
  // config.txt に値がなければ送らない
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); uint32_t t = 0; beacon_set_motion(&b, true, 60, true, 90, LAT, LON);
    CHECK(run_until(&b, &t, 7200000, false, true) == BEACON_IDLE && beacon_remaining_ms(&b, t) == 0 && beacon_flex_interval_ms(&b) == 0); }
  // 速度と間隔
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &f);
    beacon_set_motion(&b, false, 0, false, 0, 0, 0);       CHECK(beacon_flex_interval_ms(&b) == 1800000u);   // GPSが使えない: SLOW RATE
    beacon_set_motion(&b, true, 0, false, 0, LAT, LON);    CHECK(beacon_flex_interval_ms(&b) == 1800000u);   // 停止
    beacon_set_motion(&b, true, 4.9, true, 0, LAT, LON);   CHECK(beacon_flex_interval_ms(&b) == 1800000u);
    beacon_set_motion(&b, true, 5, true, 0, LAT, LON);     CHECK(beacon_flex_interval_ms(&b) == 1680000u);   // 120秒×70÷5
    beacon_set_motion(&b, true, 35, true, 0, LAT, LON);    CHECK(beacon_flex_interval_ms(&b) == 240000u);
    beacon_set_motion(&b, true, 70, true, 0, LAT, LON);    CHECK(beacon_flex_interval_ms(&b) == 120000u);
    beacon_set_motion(&b, true, 120, true, 0, LAT, LON);   CHECK(beacon_flex_interval_ms(&b) == 120000u);   // FAST RATE
    beacon_set_mode(&b, 3, 0); CHECK(beacon_flex_interval_ms(&b) == 0); }                                       // フレックスでない
  // GPSが使えないときは SLOW RATE ごとに送る
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 1000); beacon_set_flex(&b, &f); beacon_set_motion(&b, false, 0, false, 0, 0, 0);
    CHECK(beacon_remaining_ms(&b, 1000) == 1800000u && beacon_poll(&b, 1000 + 1799900, false, true) == BEACON_IDLE && beacon_poll(&b, 1000 + 1800000, false, true) == BEACON_SEND); }
  // まっすぐ 60km/h: 140秒ごと (120×70÷60)
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &f); beacon_set_motion(&b, true, 60, true, 90, LAT, LON);
    uint32_t t = 0; CHECK(run_until(&b, &t, 600000, false, true) == BEACON_SEND && t == 140000);
    CHECK(beacon_remaining_ms(&b, t) == 140000u); }
  // 曲がったら送る: 60km/h のしきい角は 28 + 260÷60 = 32.3度。前回から30秒は空ける
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &f); beacon_set_motion(&b, true, 60, true, 90, LAT, LON);
    uint32_t t = 0; run_until(&b, &t, 600000, false, true);                          // 1回送って、進行方向 90度が基準
    beacon_set_motion(&b, true, 60, true, 122, LAT, LON);                             // 32度: まだ
    CHECK(beacon_poll(&b, t + 60000, false, true) == BEACON_IDLE);
    beacon_set_motion(&b, true, 60, true, 123, LAT, LON);                             // 33度
    CHECK(beacon_poll(&b, t + 29900, false, true) == BEACON_IDLE);                   // 30秒たつまでは送らない
    CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_SEND);
    t += 30000;
    // 0度をまたぐ: 基準 123 → 今 350 は 133度の変化
    beacon_set_motion(&b, true, 60, true, 350, LAT, LON); CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_SEND);
    t += 30000;
    beacon_set_motion(&b, true, 60, true, 20, LAT, LON);  CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_IDLE);   // 30度
    beacon_set_motion(&b, true, 60, true, 23, LAT, LON);  CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_SEND);   // 33度
    // 遅いほど大きく曲がらないと送らない: 10km/h のしきい角は 28 + 26 = 54度
    t += 30000;
    beacon_set_motion(&b, true, 10, true, 23 + 54, LAT, LON); CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_IDLE);
    beacon_set_motion(&b, true, 10, true, 23 + 55, LAT, LON); CHECK(beacon_poll(&b, t + 30000, false, true) == BEACON_SEND);
    // LOW SPEED より遅いときは曲がっても送らない
    t += 30000;
    beacon_set_motion(&b, true, 4, true, 250, LAT, LON); CHECK(beacon_poll(&b, t + 60000, false, true) == BEACON_IDLE); }
  // 止まって送ったあと動き出したら、そのときの向きを基準にする
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &f); beacon_set_motion(&b, true, 0, false, 0, LAT, LON);
    beacon_note_tx(&b, 1000); CHECK(!b.refCourseOk);
    beacon_set_motion(&b, true, 30, true, 180, LAT, LON); CHECK(b.refCourseOk && b.refCourse == 180);
    CHECK(beacon_poll(&b, 40000, false, true) == BEACON_IDLE);
    beacon_set_motion(&b, true, 30, true, 270, LAT, LON); CHECK(beacon_poll(&b, 40000, false, true) == BEACON_SEND); }
  // 遅いときの距離 (独自。0 なら使わない)
  { FlexConfig g = f; g.slowDistM = 300;
    BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &g); beacon_set_motion(&b, true, 3, true, 0, LAT, LON);
    beacon_note_tx(&b, 1000);
    beacon_set_motion(&b, true, 3, true, 0, LAT + 0.0025, LON);                       // 約278m
    CHECK(beacon_poll(&b, 200000, false, true) == BEACON_IDLE);
    beacon_set_motion(&b, true, 3, true, 0, LAT + 0.0028, LON);                       // 約311m
    CHECK(beacon_poll(&b, 20000, false, true) == BEACON_IDLE);                         // 30秒たつまでは送らない
    CHECK(beacon_poll(&b, 200000, false, true) == BEACON_SEND);
    beacon_set_motion(&b, true, 0, false, 0, LAT + 0.0028 + 0.0001, LON);             // 止まって、ふらつき程度
    CHECK(beacon_poll(&b, 400000, false, true) == BEACON_IDLE);
    BeaconSched c; beacon_init(&c, AUTO_TX_FLEX, 0); beacon_set_flex(&c, &f); beacon_set_motion(&c, true, 3, true, 0, LAT, LON);
    beacon_set_motion(&c, true, 3, true, 0, LAT + 0.01, LON);                          // 1.1km 動いても、0 なら送らない
    CHECK(beacon_poll(&c, 600000, false, true) == BEACON_IDLE); }
  // 他局の送信中は待つ (間隔のモードと同じ)
  { BeaconSched b; beacon_init(&b, AUTO_TX_FLEX, 0); beacon_set_flex(&b, &f); beacon_set_motion(&b, true, 70, true, 0, LAT, LON);
    CHECK(beacon_poll(&b, 120000, true, true) == BEACON_IDLE && b.waiting);
    CHECK(beacon_poll(&b, 125000, false, true) == BEACON_SEND && !b.waiting); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
