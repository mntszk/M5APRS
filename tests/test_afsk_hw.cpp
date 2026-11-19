// test_afsk_hw.cpp  (パソコン上のテスト用。スケッチには入れない)
// ESP32の代用品 (stubs/) を使い、afsk_hw.cpp のタイマー割り込みを手動で起こして、
// 「送信 → DAC出力を記録 → それをADC入力として受信」のループバックで全体のつなぎを確認する。
// ※ 実機のタイミング、DAC/ADCの実際の特性、外付け回路は確認できない。
#include "Arduino.h"
#include "driver/adc.h"
#include "driver/dac.h"
#include <stdlib.h>
#include "afsk_hw.h"
#include "afsk_tx_core.h"
#include "packet_build.h"
#include "packet_parse.h"
#include "test_cfg.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>

StubHw g_stub;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

#define TXT 3
#define RXT 2

static std::vector<int> g_src; static size_t g_idx = 0;
static int src_next() { return g_idx < g_src.size() ? g_src[g_idx++] : 2048; }

static std::vector<uint8_t> make_frame(const char* info, int ssid = 9) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { "NOCALL", (uint8_t)ssid, "APRS", 0, "WIDE1-1" };
  size_t n = ax25_build_ui_frame(p, (const uint8_t*)info, strlen(info), f, sizeof(f));
  return std::vector<uint8_t>(f, f + n);
}

// 送信してDAC出力を記録し、受信側へ渡すADC値の列 (13200Hz) にして返す
static std::vector<int> transmit(const std::vector<uint8_t>& frame, bool* ok, int* dacEnabledDuring, int* rxTimerDuring) {
  g_stub.dacLog.clear(); g_stub.logDac = true;
  *ok = afsk_hw_send(frame.data(), frame.size());
  *dacEnabledDuring = g_stub.dacEnabled; *rxTimerDuring = g_stub.timers[RXT].enabled;
  int guard = 0;
  while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); }
  g_stub.logDac = false;
  std::vector<int> adc;
  for (size_t i = 0; i < g_stub.dacLog.size(); i += 2) adc.push_back(2048 + ((int)g_stub.dacLog[i] - 128) * 16);
  return adc;
}

static void receive(const std::vector<int>& adc, int chunk = 200) {
  g_src = adc; g_idx = 0; g_stub.adcSource = src_next;
  size_t total = adc.size() + 3000;
  for (size_t done = 0; done < total; done += chunk) {
    stub_fire_timer(RXT, chunk);
    g_stub.nowMs += 15;
    afsk_hw_poll(millis());
  }
}

int main() {
  test_cfg_setup();
  g_stub.adcSource = src_next;

  // ===== 初期化 =====
  CHECK(afsk_hw_begin());
  CHECK(g_stub.adcWidth == ADC_WIDTH_BIT_12 && g_stub.adcAtten == ADC_ATTEN_DB_11 && g_stub.adcCh == ADC1_CHANNEL_0);   // GPIO36
  CHECK(g_stub.timers[TXT].divider == 2 && g_stub.timers[RXT].divider == 2);
  CHECK(g_stub.timers[TXT].alarm == 40000000ull / TX_SAMPLE_RATE_HZ && g_stub.timers[TXT].autoreload);   // 26400Hz → 1515
  CHECK(g_stub.timers[RXT].alarm == 40000000ull / RX_SAMPLE_RATE_HZ && g_stub.timers[RXT].autoreload);   // 13200Hz → 3030
  CHECK(g_stub.timers[RXT].enabled && !g_stub.timers[TXT].enabled);                                       // 起動後は受信中
  CHECK(!g_stub.dacEnabled);                                                                              // 受信中はDACを使わない
  CHECK(!afsk_hw_busy() && !afsk_hw_tx_busy());

  // ===== 無音の受信: 何も出ない =====
  g_src.clear(); g_idx = 0;
  for (int i = 0; i < 20; i++) { stub_fire_timer(RXT, 1000); afsk_hw_poll(millis()); }
  { QueuedPacket p; CHECK(!afsk_hw_pop_frame(&p)); }
  CHECK(afsk_hw_stats().rxSamples == 20000 && afsk_hw_stats().rxOverruns == 0 && !afsk_hw_busy());

  // ===== ループバック: 送信 → 受信 =====
  std::vector<uint8_t> fr = make_frame("!3539.52N/13944.72E>090/012M5Stack");
  bool ok; int dacOn, rxOn;
  std::vector<int> adc = transmit(fr, &ok, &dacOn, &rxOn);
  CHECK(ok); CHECK(dacOn == 1 && rxOn == 0);                               // 送信中: DAC有効・受信停止
  CHECK(!afsk_hw_tx_busy());                                               // 終わった
  CHECK(!g_stub.dacEnabled && g_stub.timers[RXT].enabled && !g_stub.timers[TXT].enabled);   // DAC停止・受信再開
  CHECK(afsk_hw_stats().txCount == 1);
  CHECK(adc.size() > 10000);
  // 送信音の最初と最後は中心値 (段差を出さない)
  CHECK(g_stub.dacLog.size() > 0 && g_stub.dacLog.front() == 128 && g_stub.dacLog.back() == 128);
  // 振幅の範囲: 128±100 以内
  { int mn = 255, mx = 0; for (size_t i = 0; i < g_stub.dacLog.size(); i++) { if (g_stub.dacLog[i] < mn) mn = g_stub.dacLog[i]; if (g_stub.dacLog[i] > mx) mx = g_stub.dacLog[i]; }
    CHECK(mn >= 128 - TX_AMP_MARK && mx <= 128 + TX_AMP_MARK && mn < 60 && mx > 190); }
  // 時間: プリアンブル60フラグ + フレーム + 後尾 ≒ ビット数/1200 秒
  { double sec = g_stub.dacLog.size() / (double)TX_SAMPLE_RATE_HZ; double expect = (60 * 8 + fr.size() * 8 + 8 + 4 * 8) / 1200.0;     // プリアンブル+フレーム+閉じのフラグ+後尾 (スタッフィングで少し増える)
    printf("TX time %.3f s (about %.3f s)\n", sec, expect); CHECK(sec >= expect - 0.001 && sec < expect + 0.03); }

  receive(adc);
  { QueuedPacket p; bool got = afsk_hw_pop_frame(&p);
    CHECK(got && p.len == fr.size() - 2 && memcmp(p.data, fr.data(), p.len) == 0);
    AprsPacket ap; CHECK(got && aprs_parse_frame(p.data, p.len, &ap) && ap.kind == APRS_KIND_POSITION);
    CHECK(strcmp(ap.src, "NOCALL-9") == 0 && ap.speedKnots == 12 && strcmp(ap.comment, "M5Stack") == 0);
    CHECK(!afsk_hw_pop_frame(&p)); }
  CHECK(afsk_hw_stats().rxFramesOk == 1 && afsk_hw_stats().rxFcsErrors == 0 && afsk_hw_stats().rxOverruns == 0);
  CHECK(!afsk_hw_busy());                                                  // 信号が終わって少し経てば、busyでなくなる

  // ===== 受信中は busy (設定の書き込みを避ける) =====
  { g_src = adc; g_idx = 0; bool sawBusy = false;
    for (size_t done = 0; done < adc.size(); done += 200) { stub_fire_timer(RXT, 200); afsk_hw_poll(millis()); if (afsk_hw_busy()) sawBusy = true; if (done == 6000) CHECK(afsk_hw_busy()); }
    CHECK(sawBusy);
    stub_fire_timer(RXT, 3000); afsk_hw_poll(millis()); CHECK(!afsk_hw_busy());
    QueuedPacket p; while (afsk_hw_pop_frame(&p)) {} }

  // ===== 送信の制約 =====
  CHECK(!afsk_hw_send(NULL, 10)); CHECK(!afsk_hw_send(fr.data(), 0));
  { std::vector<uint8_t> huge(AFSK_TX_MAX_BITS / 8 + 100, 0x55);              // ビット列に収まらない
    CHECK(!afsk_hw_send(huge.data(), huge.size())); CHECK(g_stub.timers[RXT].enabled && !afsk_hw_tx_busy()); }   // 失敗しても受信は続く
  CHECK(afsk_hw_send(fr.data(), fr.size()));
  CHECK(!afsk_hw_send(fr.data(), fr.size()));                                // 送信中は受け付けない
  CHECK(afsk_hw_busy() && afsk_hw_tx_busy());
  { int guard = 0; while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); } }
  CHECK(!afsk_hw_tx_busy() && afsk_hw_stats().txCount == 2);

  // ===== 送信直後に受信へ戻ったとき、古いサンプルを引きずらない =====
  { // 送信中にたまっていたサンプルは捨てられ、無音から復調を始める
    stub_fire_timer(RXT, 500);                                              // 少したまっている
    std::vector<int> a2 = adc; receive(a2);
    QueuedPacket p; CHECK(afsk_hw_pop_frame(&p) && p.len == fr.size() - 2); while (afsk_hw_pop_frame(&p)) {} }

  // ===== 連続して複数のフレーム =====
  { std::vector<int> all;
    std::vector<std::vector<uint8_t> > frames;
    for (int i = 0; i < 3; i++) {
      char info[64]; snprintf(info, sizeof(info), "!3500.0%dN/13900.0%dE>pkt%d", i, i, i);
      frames.push_back(make_frame(info, i));
      bool ok2; int a, b; auto x = transmit(frames.back(), &ok2, &a, &b); CHECK(ok2);
      all.insert(all.end(), 2000, 2048); all.insert(all.end(), x.begin(), x.end());
    }
    receive(all);
    QueuedPacket p; int n = 0;
    while (afsk_hw_pop_frame(&p)) { CHECK(n < 3 && p.len == frames[n].size() - 2 && memcmp(p.data, frames[n].data(), p.len) == 0); n++; }
    CHECK(n == 3); }

  // ===== loopが遅れてリングバッファがあふれたとき =====
  { QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    uint32_t before = afsk_hw_stats().rxOverruns;
    g_src.assign(5000, 2048); g_idx = 0;
    stub_fire_timer(RXT, 3000);                                             // pollせずに3000回
    uint32_t cap = RX_SAMPLE_RATE_HZ * RX_RINGBUF_MS / 1000;                // 入れられる数
    CHECK(afsk_hw_stats().rxOverruns - before == 3000 - cap);
    afsk_hw_poll(millis()); afsk_hw_poll(millis());                         // 回復
    CHECK(afsk_hw_stats().rxSamples > 0); }


  // ===== 受信の途中でリングバッファがあふれたとき: そのフレームは捨て、次のフレームは受かる =====
  { QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    g_src = adc; g_idx = 0; uint32_t d0 = afsk_hw_stats().rxDiscards, ok0 = afsk_hw_stats().rxFramesOk;
    for (size_t done = 0; done < 8000; done += 200) { stub_fire_timer(RXT, 200); afsk_hw_poll(millis()); }
    stub_fire_timer(RXT, 3000); g_idx += 0;                                // loopが止まっていた間に3000サンプルが流れる
    for (size_t done = 0; done < adc.size() + 3000; done += 200) { stub_fire_timer(RXT, 200); afsk_hw_poll(millis()); }
    CHECK(afsk_hw_stats().rxDiscards - d0 >= 1);
    CHECK(afsk_hw_stats().rxFramesOk == ok0);                              // 欠けたフレームは受からない
    CHECK(!afsk_hw_pop_frame(&p));
    receive(adc);                                                           // 次のフレームは問題なし
    CHECK(afsk_hw_pop_frame(&p) && p.len == fr.size() - 2 && afsk_hw_stats().rxFramesOk == ok0 + 1); }

  // ===== 受信待ち行列があふれたとき =====
  { QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    uint32_t dropBefore = afsk_hw_stats().queueDropped;
    for (int i = 0; i < RX_PACKET_QUEUE_COUNT + 3; i++) { receive(adc); }   // 取り出さずに受け続ける
    int n = 0; while (afsk_hw_pop_frame(&p)) n++;
    CHECK(n == RX_PACKET_QUEUE_COUNT);
    CHECK(afsk_hw_stats().queueDropped - dropBefore == 3); }

  // ===== モニター出力 (DAC25: 内蔵スピーカー) =====
  { static const uint16_t tbl[MONITOR_LEVEL_MAX + 1] = MONITOR_SCALE_TABLE;
    const int MAIN = (AFSK_TX_DAC_PIN == 26) ? 1 : 0, MON = (AFSK_TX_MONITOR_PIN == 26) ? 1 : 0;
    CHECK(AFSK_TX_DAC_PIN != AFSK_TX_MONITOR_PIN && afsk_hw_monitor_level() == DEFAULT_MONITOR_LEVEL);
    for (int lv = 0; lv <= MONITOR_LEVEL_MAX; lv++) {
      afsk_hw_set_monitor_level((uint8_t)lv); CHECK(afsk_hw_monitor_level() == lv);
      g_stub.ch[MAIN].log.clear(); g_stub.ch[MON].log.clear(); g_stub.logDac = true;
      int monEnBefore = g_stub.ch[MON].enableCalls, monDisBefore = g_stub.ch[MON].disableCalls;
      CHECK(afsk_hw_send(fr.data(), fr.size()));
      bool monDuring = g_stub.ch[MON].enabled;
      int guard = 0; while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); }
      g_stub.logDac = false;
      const std::vector<uint8_t>& mainLog = g_stub.ch[MAIN].log; const std::vector<uint8_t>& monLog = g_stub.ch[MON].log;
      CHECK(!g_stub.ch[MON].enabled && !g_stub.ch[MAIN].enabled);                  // 終わったら両方止める
      if (lv == 0) {
        CHECK(!monDuring && monLog.empty());                                       // ミュート: モニター側のDACは動かさない
        CHECK(g_stub.ch[MON].enableCalls == monEnBefore && g_stub.ch[MON].disableCalls == monDisBefore);
      } else {
        CHECK(monDuring && g_stub.ch[MON].enableCalls == monEnBefore + 1 && g_stub.ch[MON].disableCalls == monDisBefore + 1);
        CHECK(monLog.size() == mainLog.size() && monLog.size() > 1000);            // 同じ数のサンプル
        bool same = true, ctr = (monLog.front() == 128 && monLog.back() == 128);
        for (size_t i = 0; i < mainLog.size(); i++) {
          int expect = 128 + (((int)mainLog[i] - 128) * (int)tbl[lv] >> 8);
          if (monLog[i] != expect) same = false;
        }
        CHECK(same && ctr);                                                        // 無線機側の波形に、段階ごとの倍率を掛けたもの
        int mx = 0; for (size_t i = 0; i < monLog.size(); i++) { int d = abs((int)monLog[i] - 128); if (d > mx) mx = d; }
        CHECK(mx <= (TX_AMP_MARK * tbl[lv] + 255) / 256 + 1 && mx >= (TX_AMP_MARK * tbl[lv]) / 256 - 1);
        if (lv == MONITOR_LEVEL_MAX) CHECK(monLog == mainLog);                     // 最大は無線機側と同じ
      }
      // 無線機側の波形は、モニター音量に左右されない
      static std::vector<uint8_t> ref; if (lv == 0) ref = mainLog; else CHECK(mainLog == ref);
      QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    }
    // 範囲外は最大にそろえる。送信中は音量を変えない
    afsk_hw_set_monitor_level(200); CHECK(afsk_hw_monitor_level() == MONITOR_LEVEL_MAX);
    afsk_hw_set_monitor_level(2);
    g_stub.ch[MON].log.clear(); g_stub.logDac = true;
    CHECK(afsk_hw_send(fr.data(), fr.size()));
    stub_fire_timer(TXT, 3000); afsk_hw_set_monitor_level(0); stub_fire_timer(TXT, 3000);   // 送信の途中でミュートにしても
    int guard = 0; while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); }
    g_stub.logDac = false;
    { int mx = 0; for (size_t i = 0; i < g_stub.ch[MON].log.size(); i++) { int d = abs((int)g_stub.ch[MON].log[i] - 128); if (d > mx) mx = d; }
      CHECK(mx >= 15 && mx <= 21); }                                               // この送信は段階2のまま最後まで出る (振幅 100*51/256 ≒ 20)
    CHECK(afsk_hw_monitor_level() == 0);
    afsk_hw_set_monitor_level(DEFAULT_MONITOR_LEVEL); }

  // ===== 受信音 (モニターのスピーカー、受信の割り込みで鳴らす。音量はモニター音量と別) =====
  { const int MON = (AFSK_TX_MONITOR_PIN == 26) ? 1 : 0, MAIN = 1 - MON;
    QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    afsk_hw_set_monitor_level(0);                                                      // モニター音量がミュートでも鳴る
    g_stub.ch[MON].log.clear(); g_stub.logDac = true;
    int en = g_stub.ch[MON].enableCalls;
    // メッセージ「ピピピッ」: 1100Hz 100ms ×3、間 60ms
    CHECK(afsk_hw_chime(AFSK_CHIME_MSG, MONITOR_LEVEL_MAX) && g_stub.ch[MON].enabled && g_stub.ch[MON].enableCalls == en + 1);
    stub_fire_timer(RXT, RX_SAMPLE_RATE_HZ * 420 / 1000 + 50); afsk_hw_poll(millis());
    { const std::vector<uint8_t>& lg = g_stub.ch[MON].log;
      int hi = 0, lo = 0, mid = 0, runs = 0; bool inTone = false;
      for (size_t i = 0; i < lg.size(); i++) {
        bool t = (lg[i] == 148 || lg[i] == 108);
        if (lg[i] == 148) hi++; else if (lg[i] == 108) lo++; else if (lg[i] == 128) mid++;
        if (t && !inTone) runs++;
        inTone = t;
      }
      CHECK(hi + lo == RX_SAMPLE_RATE_HZ * 300 / 1000 && runs == 3 && mid >= RX_SAMPLE_RATE_HZ * 120 / 1000 && lg.back() == 128);   // ±20 の矩形波が3回
      // 1100Hz: 6サンプルごとに高低が入れ替わる
      size_t f = 0; while (f < lg.size() && lg[f] != 148) f++;
      CHECK(f + 12 < lg.size() && lg[f + 5] == 148 && lg[f + 6] == 108 && lg[f + 11] == 108 && lg[f + 12] == 148); }
    CHECK(!g_stub.ch[MON].enabled && !g_stub.ch[MAIN].enabled);                                // 終わったら止める
    // パケット「ポッ」825Hz 40ms (8サンプルごと)、音量の段階で振幅が変わる (段階2: ±4)
    g_stub.ch[MON].log.clear();
    CHECK(afsk_hw_chime(AFSK_CHIME_PACKET, 2));
    stub_fire_timer(RXT, RX_SAMPLE_RATE_HZ * 40 / 1000 + 50); afsk_hw_poll(millis());
    { const std::vector<uint8_t>& lg = g_stub.ch[MON].log; int t = 0; for (size_t i = 0; i < lg.size(); i++) if (lg[i] == 132 || lg[i] == 124) t++;
      size_t f = 0; while (f < lg.size() && lg[f] != 132) f++;
      CHECK(t == RX_SAMPLE_RATE_HZ * 40 / 1000 && f + 8 < lg.size() && lg[f + 7] == 132 && lg[f + 8] == 124); }
    // 折り返し・Ack: 2つの音の長さの合計
    g_stub.ch[MON].log.clear();
    CHECK(afsk_hw_chime(AFSK_CHIME_ACK, MONITOR_LEVEL_MAX));
    stub_fire_timer(RXT, RX_SAMPLE_RATE_HZ * 280 / 1000 + 50); afsk_hw_poll(millis());
    { const std::vector<uint8_t>& lg = g_stub.ch[MON].log; int t = 0; for (size_t i = 0; i < lg.size(); i++) if (lg[i] == 148 || lg[i] == 108) t++;
      CHECK(t == RX_SAMPLE_RATE_HZ * 280 / 1000 && !g_stub.ch[MON].enabled); }
    CHECK(afsk_hw_chime(AFSK_CHIME_ECHO, 1)); stub_fire_timer(RXT, RX_SAMPLE_RATE_HZ / 5); afsk_hw_poll(millis()); CHECK(!g_stub.ch[MON].enabled);
    // 音量 0・知らない種類は鳴らさない
    CHECK(!afsk_hw_chime(AFSK_CHIME_MSG, 0) && !afsk_hw_chime(AFSK_CHIME_COUNT, 2) && !g_stub.ch[MON].enabled);
    // 鳴っている途中に別の音: 入れ替える
    CHECK(afsk_hw_chime(AFSK_CHIME_MSG, 2)); stub_fire_timer(RXT, 100); CHECK(afsk_hw_chime(AFSK_CHIME_PACKET, 2));
    stub_fire_timer(RXT, RX_SAMPLE_RATE_HZ * 40 / 1000 + 10); afsk_hw_poll(millis()); CHECK(!g_stub.ch[MON].enabled);
    CHECK(afsk_hw_chime(AFSK_CHIME_MSG, 2));
    stub_fire_timer(RXT, 100);
    CHECK(afsk_hw_send(fr.data(), fr.size()) && !afsk_hw_chime(AFSK_CHIME_MSG, 2));          // 送信を始めると止まり、送信中は鳴らせない
    int guard = 0; while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); }
    g_stub.logDac = false;
    CHECK(!g_stub.ch[MON].enabled);
    stub_fire_timer(RXT, 8000); afsk_hw_poll(millis()); CHECK(!g_stub.ch[MON].enabled);       // 送信のあと鳴り直さない
    while (afsk_hw_pop_frame(&p)) {}
    afsk_hw_set_monitor_level(DEFAULT_MONITOR_LEVEL); }

  // ===== 受信状態 (画面表示用) =====
  { QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
    AfskHwRxStatus st; afsk_hw_rx_status(&st);
    CHECK(st.phase == AFSK_PHASE_IDLE && st.lastResult != AFSK_RESULT_NONE);        // これまでの受信の結果は残っている
    uint32_t flags0 = afsk_hw_stats().rxFlags, seq0 = st.resultSeq;
    // 無音 → 信号 → データ → 受信OK → 無音 の順に変わる
    g_src = adc; g_idx = 0; bool sawSignal = false, sawData = false, sawDataBeforeResult = false; int maxPeak = 0, maxMark = 0, maxSpace = 0, maxBytes = 0;
    AfskRxPhase prev = AFSK_PHASE_IDLE; bool orderOk = true;
    for (size_t done = 0; done < adc.size() + 3000; done += 100) {
      stub_fire_timer(RXT, 100); g_stub.nowMs += 8; afsk_hw_poll(millis());
      afsk_hw_rx_status(&st);
      if (st.phase == AFSK_PHASE_SIGNAL) sawSignal = true;
      if (st.phase == AFSK_PHASE_DATA) { sawData = true; if (st.resultSeq == seq0) sawDataBeforeResult = true; }
      if (prev == AFSK_PHASE_IDLE && st.phase == AFSK_PHASE_DATA) orderOk = false;   // いきなり「データ」にはならない (フラグ3個の間は「信号」)
      prev = st.phase;
      if (st.peak > maxPeak) maxPeak = st.peak;
      if (st.markAmp > maxMark) maxMark = st.markAmp;
      if (st.spaceAmp > maxSpace) maxSpace = st.spaceAmp;
      if (st.frameBytes > maxBytes) maxBytes = st.frameBytes;
    }
    CHECK(sawSignal && sawData && sawDataBeforeResult && orderOk);
    CHECK(maxPeak > 800 && maxMark > 800 && maxSpace > 400 && maxBytes >= 30);       // 振幅は MARK100/SPACE55 (×16) の程度、バイト数が増えていく
    CHECK(maxPeak < 2048 && maxSpace < maxMark);
    afsk_hw_rx_status(&st);
    CHECK(st.phase == AFSK_PHASE_IDLE && st.lastResult == AFSK_RESULT_OK && st.resultSeq == seq0 + 1);
    CHECK(st.lastResultMs > 0 && st.lastResultMs <= millis());
    CHECK(afsk_hw_stats().rxFlags - flags0 >= 62);
    while (afsk_hw_pop_frame(&p)) {}

    // FCSが合わないフレーム → FCS不良
    std::vector<uint8_t> bad = fr; bad[bad.size() - 1] ^= 0x55;
    bool okb; int a1, a2; std::vector<int> adcBad = transmit(bad, &okb, &a1, &a2); CHECK(okb);
    afsk_hw_rx_status(&st); CHECK(st.phase == AFSK_PHASE_IDLE && st.peak == 0);       // 送信が終わった直後は受信を始めたばかり (0から)
    uint32_t seq1 = st.resultSeq, fcs0 = afsk_hw_stats().rxFcsErrors;
    receive(adcBad);
    afsk_hw_rx_status(&st);
    CHECK(st.lastResult == AFSK_RESULT_FCS_ERROR && st.resultSeq == seq1 + 1 && afsk_hw_stats().rxFcsErrors == fcs0 + 1);
    CHECK(!afsk_hw_pop_frame(&p));
    receive(adc); afsk_hw_rx_status(&st); CHECK(st.lastResult == AFSK_RESULT_OK && st.resultSeq == seq1 + 2);   // 次のOKで上書き
    while (afsk_hw_pop_frame(&p)) {}

    // 送信中は「信号なし」(受信していない)
    CHECK(afsk_hw_send(fr.data(), fr.size()));
    stub_fire_timer(TXT, 500); afsk_hw_rx_status(&st);
    CHECK(st.phase == AFSK_PHASE_IDLE && st.peak == 0 && st.markAmp == 0 && st.lastResult == AFSK_RESULT_OK);
    { int guard = 0; while (afsk_hw_tx_busy() && guard++ < 1000) { stub_fire_timer(TXT, 1000); g_stub.nowMs += 38; afsk_hw_poll(millis()); } }

    // ノイズだけ: carrier が立っても「データ」にはならない
    { std::vector<int> noise; uint32_t rs = 777; double ph = 0;
      for (int i = 0; i < 13200 * 20; i++) { rs = rs * 1664525u + 1013904223u; double r = ((rs >> 8) & 0xFFFF) / 65536.0 - 0.5; noise.push_back(2048 + (int)lround(r * 3000)); (void)ph; }
      g_src = noise; g_idx = 0; bool data = false, signal = false;
      for (size_t done = 0; done < noise.size(); done += 100) { stub_fire_timer(RXT, 100); afsk_hw_poll(millis()); afsk_hw_rx_status(&st);
        if (st.phase == AFSK_PHASE_DATA) { data = true; } if (st.phase == AFSK_PHASE_SIGNAL) { signal = true; } }
      CHECK(signal && !data); } }


  // ===== PTT出力 =====
  { auto fr = make_frame("!3541.00N/13946.00E>PTT test");
    int pin = PTT_PIN;
    CHECK(pin == 5 && g_stub.gpioMode[pin] == OUTPUT && g_stub.gpioLevel[pin] == (PTT_ACTIVE_HIGH ? LOW : HIGH));   // 起動後はオフ
    CHECK(!afsk_hw_ptt_on());
    // 起動直後 (afsk_hw_begin の最初) にオフを書いている
    CHECK(!g_stub.gpioLog.empty() && g_stub.gpioLog.front().pin == pin && g_stub.gpioLog.front().level == (PTT_ACTIVE_HIGH ? LOW : HIGH));
    g_stub.gpioLog.clear();
    for (int i = 0; i < 20; i++) { stub_fire_timer(RXT, 1000); g_stub.nowMs += 10; afsk_hw_poll(millis()); }   // 受信中はPTTを触らない
    CHECK(g_stub.gpioLog.empty());

    uint32_t tx0 = afsk_hw_stats().txCount;
    unsigned long tOn = g_stub.nowMs;
    CHECK(afsk_hw_send(fr.data(), fr.size()));
    CHECK(afsk_hw_ptt_on() && afsk_hw_tx_busy() && afsk_hw_busy());
    CHECK(g_stub.gpioLog.size() == 1 && g_stub.gpioLog[0].level == (PTT_ACTIVE_HIGH ? HIGH : LOW));
    CHECK(g_stub.gpioLog[0].dacEn);                                  // DACを有効にしてからPTTを入れる
    CHECK(!g_stub.timers[TXT].enabled && !g_stub.timers[RXT].enabled);   // まだ音声は出さない・受信は止まっている
    CHECK(g_stub.ch[g_stub.mainCh].last == 128);                     // 待ちの間は中心値

    // 待ち時間のあいだは音声を出さない
    unsigned long tAudio = 0, tOff = 0, tDacOff = 0; bool audioStarted = false; int guard = 0;
    bool dacOffAfterPtt = false;
    while (afsk_hw_tx_busy() && guard++ < 5000) {
      if (g_stub.timers[TXT].enabled) stub_fire_timer(TXT, 264);     // 約10ms ぶんのサンプル
      g_stub.nowMs += 10; afsk_hw_poll(millis());
      if (!audioStarted && g_stub.timers[TXT].enabled) { audioStarted = true; tAudio = g_stub.nowMs; }
      if (!tOff && !afsk_hw_ptt_on()) { tOff = g_stub.nowMs; dacOffAfterPtt = g_stub.ch[g_stub.mainCh].enabled == false || true; }
    }
    CHECK(audioStarted && tAudio - tOn >= (unsigned long)PTT_ON_DELAY_MS && tAudio - tOn <= (unsigned long)PTT_ON_DELAY_MS + 20);
    CHECK(!afsk_hw_tx_busy() && !afsk_hw_ptt_on() && afsk_hw_stats().txCount == tx0 + 1);
    CHECK(g_stub.gpioLog.size() == 2 && g_stub.gpioLog[1].level == (PTT_ACTIVE_HIGH ? LOW : HIGH));
    CHECK(g_stub.gpioLog[1].dacEn);                                  // PTTを切る時点ではまだDACは有効 (切ってからDACを止める)
    CHECK(!g_stub.timers[TXT].enabled && g_stub.timers[RXT].enabled && !g_stub.ch[g_stub.mainCh].enabled);
    // 音声が終わってから PTT_OFF_DELAY_MS 以上あとにオフ。全体が最大時間以内
    unsigned long total = g_stub.gpioLog[1].ms - g_stub.gpioLog[0].ms;
    printf("PTT on time %lu ms (limit %d)\n", total, PTT_MAX_ON_MS);
    CHECK(total < (unsigned long)PTT_MAX_ON_MS && total > (unsigned long)(PTT_ON_DELAY_MS + PTT_OFF_DELAY_MS));
    CHECK(afsk_hw_stats().pttTimeouts == 0);
    (void)tOff; (void)tDacOff; (void)dacOffAfterPtt;

    // 音声の終わりからPTTオフまでの間隔: 終了を検出した poll から PTT_OFF_DELAY_MS 後
    { g_stub.gpioLog.clear();
      CHECK(afsk_hw_send(fr.data(), fr.size()));
      while (!g_stub.timers[TXT].enabled) { g_stub.nowMs += 10; afsk_hw_poll(millis()); }
      while (g_stub.timers[TXT].enabled && afsk_hw_ptt_on()) { stub_fire_timer(TXT, 264); g_stub.nowMs += 10; afsk_hw_poll(millis()); if (!g_stub.timers[TXT].enabled) break; }
      unsigned long tEnd = g_stub.nowMs;                             // 送出が終わって、タイマーを止めた時刻
      CHECK(afsk_hw_ptt_on() && afsk_hw_tx_busy());                  // 余韻のあいだはPTTオン
      int g2 = 0; while (afsk_hw_tx_busy() && g2++ < 100) { g_stub.nowMs += 10; afsk_hw_poll(millis()); }
      CHECK(!afsk_hw_ptt_on() && g_stub.gpioLog.size() == 2);
      CHECK(g_stub.gpioLog[1].ms - tEnd >= (unsigned long)PTT_OFF_DELAY_MS && g_stub.gpioLog[1].ms - tEnd <= (unsigned long)PTT_OFF_DELAY_MS + 20); }

    // ===== 最大時間: 待ちの途中・送出の途中 で止める =====
    { uint32_t to0 = afsk_hw_stats().pttTimeouts, c0 = afsk_hw_stats().txCount; g_stub.gpioLog.clear();
      // (a) 送出の途中で、時計が最大時間を超えた
      CHECK(afsk_hw_send(fr.data(), fr.size()));
      while (!g_stub.timers[TXT].enabled) { g_stub.nowMs += 10; afsk_hw_poll(millis()); }
      stub_fire_timer(TXT, 3000);
      g_stub.nowMs += PTT_MAX_ON_MS + 1; afsk_hw_poll(millis());
      CHECK(!afsk_hw_ptt_on() && !afsk_hw_tx_busy() && !g_stub.timers[TXT].enabled && g_stub.timers[RXT].enabled);
      CHECK(afsk_hw_stats().pttTimeouts == to0 + 1 && afsk_hw_stats().txCount == c0);   // 完了扱いにはしない
      CHECK(g_stub.gpioLog.size() == 2 && g_stub.gpioLog[1].level == (PTT_ACTIVE_HIGH ? LOW : HIGH) && g_stub.gpioLog[1].dacEn);
      CHECK(!g_stub.ch[g_stub.mainCh].enabled);
      // (b) 待ちの途中で超えた (時計が飛んだ場合など)
      CHECK(afsk_hw_send(fr.data(), fr.size()));
      g_stub.nowMs += PTT_MAX_ON_MS + 1; afsk_hw_poll(millis());
      CHECK(!afsk_hw_ptt_on() && !afsk_hw_tx_busy() && !g_stub.timers[TXT].enabled && afsk_hw_stats().pttTimeouts == to0 + 2);
      // (c) 境界: ちょうど最大時間では止めない
      CHECK(afsk_hw_send(fr.data(), fr.size()));
      g_stub.nowMs += PTT_MAX_ON_MS; afsk_hw_poll(millis()); CHECK(afsk_hw_ptt_on() && afsk_hw_tx_busy());
      g_stub.nowMs += 1; afsk_hw_poll(millis()); CHECK(!afsk_hw_ptt_on() && afsk_hw_stats().pttTimeouts == to0 + 3);
      // 中止のあとも、次の送信は普通にできる
      uint32_t c1 = afsk_hw_stats().txCount;
      bool ok2; int a, b; transmit(fr, &ok2, &a, &b);
      CHECK(ok2 && afsk_hw_stats().txCount == c1 + 1 && !afsk_hw_ptt_on() && afsk_hw_stats().pttTimeouts == to0 + 3); }

    // 送信が中止されても、受信は再開して復調できる
    { std::vector<int> adc; bool ok3; int a, b; adc = transmit(fr, &ok3, &a, &b);
      QueuedPacket p; while (afsk_hw_pop_frame(&p)) {}
      receive(adc); CHECK(afsk_hw_pop_frame(&p) && p.len == fr.size() - 2); } }


  // ===== 調整トーンと診断イベント =====
  { auto fr = make_frame("!3541.00N/13946.00E>EV test");
    const int ON = PTT_ACTIVE_HIGH ? HIGH : LOW, OFF = PTT_ACTIVE_HIGH ? LOW : HIGH;
    auto drain = []() { AfskHwEvent e; while (afsk_hw_pop_event(&e)) {} };
    // 通常の送信の出来事: PTT_ON → AUDIO_START → AUDIO_END → PTT_OFF (時間つき)
    drain();
    { bool ok2; int a, b; transmit(fr, &ok2, &a, &b); CHECK(ok2);
      AfskHwEvent e[8]; int n = 0; while (n < 8 && afsk_hw_pop_event(&e[n])) n++;
      CHECK(n == 4 && e[0].type == AFSK_EV_PTT_ON && e[1].type == AFSK_EV_AUDIO_START && e[2].type == AFSK_EV_AUDIO_END && e[3].type == AFSK_EV_PTT_OFF);
      CHECK(e[1].a >= (uint32_t)PTT_ON_DELAY_MS && e[1].a <= (uint32_t)PTT_ON_DELAY_MS + 60);
      CHECK(e[2].a > 500 && e[2].a < 3000);
      CHECK(e[3].a >= e[1].a + e[2].a && e[3].a <= e[1].a + e[2].a + 100);
      CHECK(e[0].ms <= e[1].ms && e[1].ms <= e[2].ms && e[2].ms <= e[3].ms); }

    // 調整トーン: 開始 → PTT → 待ち → トーン → 停止要求 → 余韻 → PTTオフ
    uint32_t c0 = afsk_hw_stats().txCount, to0 = afsk_hw_stats().pttTimeouts;
    CHECK(!afsk_hw_calib_start(0) && !afsk_hw_calib_start(4));
    g_stub.gpioLog.clear(); drain();
    CHECK(afsk_hw_calib_start(AFSK_TONE_MARK));
    CHECK(afsk_hw_calib_active() && afsk_hw_calib_mode() == AFSK_TONE_MARK && afsk_hw_tx_busy() && afsk_hw_ptt_on());
    CHECK(!afsk_hw_calib_start(AFSK_TONE_SPACE) && !afsk_hw_send(fr.data(), fr.size()));       // 送信中は受け付けない
    CHECK(!g_stub.timers[TXT].enabled && g_stub.gpioLog.size() == 1 && g_stub.gpioLog[0].level == ON && g_stub.gpioLog[0].dacEn);
    unsigned long t0 = g_stub.nowMs;
    while (!g_stub.timers[TXT].enabled) { g_stub.nowMs += 10; afsk_hw_poll(millis()); }
    CHECK(g_stub.nowMs - t0 >= (unsigned long)PTT_ON_DELAY_MS);                                  // 待ってから音が出る
    // 音が出ていて、1秒たっても終わらない。周波数は 1200Hz
    g_stub.ch[g_stub.mainCh].log.clear(); g_stub.logDac = true;
    { int cross = 0, prev = 128; for (int i = 0; i < 26400; i++) { stub_fire_timer(TXT, 1); if (i % 264 == 263) { g_stub.nowMs += 10; afsk_hw_poll(millis()); }
        int v = g_stub.dacLast; if (prev < 128 && v >= 128) cross++; prev = v; }
      CHECK(cross >= 1198 && cross <= 1202); }
    g_stub.logDac = false;
    CHECK(afsk_hw_calib_active() && afsk_hw_ptt_on());
    afsk_hw_calib_stop();
    afsk_hw_poll(millis());                                                                      // 音が止まり、余韻へ
    CHECK(!g_stub.timers[TXT].enabled && afsk_hw_ptt_on() && afsk_hw_calib_active());
    g_stub.nowMs += PTT_OFF_DELAY_MS - 10; afsk_hw_poll(millis()); CHECK(afsk_hw_ptt_on());
    g_stub.nowMs += 10; afsk_hw_poll(millis());
    CHECK(!afsk_hw_ptt_on() && !afsk_hw_tx_busy() && !afsk_hw_calib_active() && g_stub.timers[RXT].enabled && !g_stub.ch[g_stub.mainCh].enabled);
    CHECK(g_stub.gpioLog.size() == 2 && g_stub.gpioLog[1].level == OFF && g_stub.gpioLog[1].dacEn);
    CHECK(afsk_hw_stats().txCount == c0 && afsk_hw_stats().pttTimeouts == to0);                   // 送信回数にも、時間超過にも数えない
    { AfskHwEvent e[8]; int n = 0; while (n < 8 && afsk_hw_pop_event(&e[n])) n++;
      CHECK(n == 6 && e[0].type == AFSK_EV_PTT_ON && e[1].type == AFSK_EV_CALIB_START && e[1].a == AFSK_TONE_MARK && e[2].type == AFSK_EV_AUDIO_START &&
            e[3].type == AFSK_EV_AUDIO_END && e[4].type == AFSK_EV_PTT_OFF && e[5].type == AFSK_EV_CALIB_STOP); }
    afsk_hw_calib_stop(); afsk_hw_poll(millis()); CHECK(!afsk_hw_tx_busy());                      // 止まっているときの停止要求は無害

    // 自動停止: CALIB_MAX_ON_MS で止まる (時間超過の強制停止ではない)
    drain();
    CHECK(afsk_hw_calib_start(AFSK_TONE_ALT));
    { unsigned long ts = g_stub.nowMs; int guard = 0;
      while (afsk_hw_tx_busy() && guard++ < 100000) { if (g_stub.timers[TXT].enabled) stub_fire_timer(TXT, 264); g_stub.nowMs += 10; afsk_hw_poll(millis()); }
      CHECK(guard < 100000);
      unsigned long dur = g_stub.nowMs - ts; printf("calib auto stop after %lu ms\n", dur);
      CHECK(dur >= (unsigned long)CALIB_MAX_ON_MS && dur <= (unsigned long)CALIB_MAX_ON_MS + (unsigned long)PTT_OFF_DELAY_MS + 40); }
    CHECK(!afsk_hw_ptt_on() && afsk_hw_stats().pttTimeouts == to0 && afsk_hw_stats().txCount == c0);
    // 待ちの途中で止める
    CHECK(afsk_hw_calib_start(AFSK_TONE_SPACE)); afsk_hw_calib_stop(); afsk_hw_poll(millis());
    g_stub.nowMs += PTT_OFF_DELAY_MS; afsk_hw_poll(millis()); CHECK(!afsk_hw_tx_busy() && !afsk_hw_ptt_on() && !g_stub.timers[TXT].enabled);
    // そのあと通常の送信ができ、受信もできる
    { bool ok3; int a, b; auto adc = transmit(fr, &ok3, &a, &b); CHECK(ok3 && afsk_hw_stats().txCount == c0 + 1);
      QueuedPacket p; while (afsk_hw_pop_frame(&p)) {} receive(adc); CHECK(afsk_hw_pop_frame(&p) && p.len == fr.size() - 2); } }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
