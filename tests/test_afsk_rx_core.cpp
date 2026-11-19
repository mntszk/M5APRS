// test_afsk_rx_core.cpp  (パソコン上のテスト用。スケッチには入れない)
// 送信側(afsk_tx_core)が作った波形に、直流分・ノイズ・トーン強度の差・クロックのずれなどを加えて、
// 受信側(afsk_rx_core)で元のフレームが戻ることを確かめる。
#include "test_cfg.h"
#include "afsk_rx_core.h"
#include "afsk_tx_core.h"
#include "packet_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// ---- 乱数 (再現できるように固定) ----
static uint32_t rng_state = 12345;
static uint32_t rnd() { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }
static double urand() { return (rnd() & 0xFFFFFF) / 16777216.0; }
static double grand() { double s = 0; for (int i = 0; i < 12; i++) s += urand(); return s - 6.0; }   // 標準偏差≈1

// ---- フレーム作成 ----
static std::vector<uint8_t> make_frame(const char* src, int ssid, const char* info) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { src, (uint8_t)ssid, "APRS", 0, "WIDE1-1" };
  size_t n = ax25_build_ui_frame(p, (const uint8_t*)info, strlen(info), f, sizeof(f));
  return std::vector<uint8_t>(f, f + n);
}

// ---- 送信波形 (DAC値) を作り、ADC風の符号付き整数にする ----
struct Synth {
  uint32_t txRate = 26400;       // 送信側のサンプルレート
  int decim = 2;                 // 何個に1個を使うか (txRate/decim が受信のサンプルレート相当)
  uint8_t ampMark = 100, ampSpace = 55;
  double markGain = 1.0, spaceGain = 1.0;   // 無線機経路でのトーンごとの強さの違い
  double noise = 0;              // ノイズの標準偏差(ADC値の単位)
  int dc = 0;                    // 直流オフセット
  int leadSamples = 3000, tailSamples = 3000;
  int preamble = 60;
};

static std::vector<int16_t> synth(const std::vector<uint8_t>& frame, const Synth& s) {
  AfskTxConfig c; memset(&c, 0, sizeof(c));
  c.sampleRateHz = s.txRate; c.baud = 1200; c.markHz = 1200; c.spaceHz = 2200;
  c.ampMark = s.ampMark; c.ampSpace = s.ampSpace; c.preambleFlags = (uint8_t)s.preamble; c.tailFlags = 4;
  static AfskTx tx;
  afsk_tx_init(&tx, &c);
  afsk_tx_prepare(&tx, frame.data(), frame.size());
  afsk_tx_start(&tx);
  std::vector<int16_t> out;
  for (int i = 0; i < s.leadSamples; i++) out.push_back((int16_t)(s.dc + grand() * s.noise));
  uint8_t v; int k = 0;
  // 振幅の違いは、MARK/SPACEを区別せずに掛けるとトーンごとの差にならないので、
  // 瞬時の周波数ではなく「どちらのトーンか」で掛けられるよう、TX側の振幅設定で再現する(下のampで指定)
  while (afsk_tx_next_sample(&tx, &v)) {
    if ((k++ % s.decim) == 0) {
      double x = ((int)v - 128) * 16.0;       // 8ビット→12ビット相当
      out.push_back((int16_t)lround(s.dc + x + grand() * s.noise));
    }
  }
  for (int i = 0; i < s.tailSamples; i++) out.push_back((int16_t)(s.dc + grand() * s.noise));
  return out;
}

static AfskRxConfig rxcfg(uint32_t rate = 13200) {
  AfskRxConfig c; memset(&c, 0, sizeof(c));
  c.sampleRateHz = rate; c.baud = 1200; c.markHz = 1200; c.spaceHz = 2200; c.toneLevelInit = 1000;
  return c;
}

static std::vector<std::vector<uint8_t> > run_rx(AfskRx& rx, const std::vector<int16_t>& s) {
  std::vector<std::vector<uint8_t> > got;
  for (size_t i = 0; i < s.size(); i++) {
    if (afsk_rx_process(&rx, s[i])) {
      size_t n; const uint8_t* p = afsk_rx_frame(&rx, &n);
      got.push_back(std::vector<uint8_t>(p, p + n));
    }
  }
  return got;
}

// frame(FCS込み)と受信結果(FCSなし)が一致するか
static bool same(const std::vector<uint8_t>& withFcs, const std::vector<uint8_t>& rxd) {
  if (withFcs.size() < 2 || rxd.size() != withFcs.size() - 2) return false;
  return memcmp(withFcs.data(), rxd.data(), rxd.size()) == 0;
}

static AfskRx rx;   // 大きいのでstatic

int main() {
  test_cfg_setup();
  const char* INFO = "!3539.52N/13944.72E>090/012/A=000200M5Stack";
  std::vector<uint8_t> fr = make_frame("NOCALL", 9, INFO);
  CHECK(fr.size() > 20);

  // ---- 初期化の検査 ----
  { AfskRxConfig c = rxcfg(); CHECK(afsk_rx_init(&rx, &c));
    c = rxcfg(); c.sampleRateHz = 0;     CHECK(!afsk_rx_init(&rx, &c));
    c = rxcfg(); c.baud = 0;             CHECK(!afsk_rx_init(&rx, &c));
    c = rxcfg(); c.spaceHz = 7000;       CHECK(!afsk_rx_init(&rx, &c));         // ナイキスト以上
    c = rxcfg(); c.sampleRateHz = 4000;  CHECK(!afsk_rx_init(&rx, &c));         // 1ビット4サンプル未満
    c = rxcfg(1200u * 70u);              CHECK(!afsk_rx_init(&rx, &c));         // 窓が大きすぎる
    CHECK(!afsk_rx_init(NULL, &c)); }

  // ---- 基本: そのまま往復 (TX 26400Hz → 間引き → RX 13200Hz) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; auto g = run_rx(rx, synth(fr, s));
    CHECK(g.size() == 1); CHECK(g.size() == 1 && same(fr, g[0]));
    CHECK(rx.framesOk == 1 && rx.fcsErrors == 0); }

  // ---- TXを13200Hzで直接 ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; s.txRate = 13200; s.decim = 1; auto g = run_rx(rx, synth(fr, s));
    CHECK(g.size() == 1 && same(fr, g[0])); }

  // ---- 実機の振幅設定 (MARK100/SPACE55) と逆の関係 (MARK小・SPACE大) と、同じ強さ ----
  { const uint8_t am[3] = {100, 55, 45}, as[3] = {55, 100, 100};
    for (int k = 0; k < 3; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.ampMark = am[k]; s.ampSpace = as[k];
      auto g = run_rx(rx, synth(fr, s));
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }
  // ---- 強さの比が大きい (約8〜9.5dB差。実測した対応範囲は MARK/SPACE = 約0.37〜3.5倍) ----
  { const uint8_t am[4] = {120, 120, 40, 50}, as[4] = {40, 60, 100, 100};
    for (int k = 0; k < 4; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.ampMark = am[k]; s.ampSpace = as[k];
      auto g = run_rx(rx, synth(fr, s));
      if (g.size() != 1) printf("imbalance case %d: got %zu\n", k, g.size());
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }

  // ---- 直流オフセット (ADC生値のように 2048 付近) ----
  { const int dcs[4] = {2048, -1500, 1000, 300};
    for (int k = 0; k < 4; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.dc = dcs[k];
      auto g = run_rx(rx, synth(fr, s));
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }

  // ---- 小さな信号 (振幅±10〜±20) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; s.ampMark = 2; s.ampSpace = 1; s.dc = 2048;
    s.ampMark = 3; s.ampSpace = 3;
    auto g = run_rx(rx, synth(fr, s));
    if (g.size() != 1) printf("small signal: got %zu\n", g.size());
    CHECK(g.size() == 1 && same(fr, g[0])); }

  // ---- 開始位置(ビットの位相)をずらす ----
  { int ok = 0, n = 0;
    for (int lead = 3000; lead < 3000 + 22; lead++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.leadSamples = lead; auto g = run_rx(rx, synth(fr, s));
      n++; if (g.size() == 1 && same(fr, g[0])) ok++;
    }
    CHECK(ok == n); }

  // ---- 短いプリアンブル (フラグ8個、4個) でも拾えるか ----
  { const int pre[2] = {8, 4};
    for (int k = 0; k < 2; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.preamble = pre[k]; auto g = run_rx(rx, synth(fr, s));
      if (g.size() != 1) printf("preamble %d: got %zu\n", pre[k], (size_t)g.size());
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }

  // ---- クロックのずれ (送信と受信のサンプルレートが違う) ----
  { const uint32_t rates[6] = {26400 * 99 / 100, 26400 * 995 / 1000, 26400 * 1005 / 1000, 26400 * 101 / 100,
                               26400 * 98 / 100, 26400 * 102 / 100};
    for (int k = 0; k < 6; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
      Synth s; s.txRate = rates[k]; auto g = run_rx(rx, synth(fr, s));
      if (g.size() != 1) printf("clock %u: got %zu\n", rates[k], (size_t)g.size());
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }

  // ---- 連続した複数フレーム (間に無音)、内容が異なる ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    std::vector<std::vector<uint8_t> > frames;
    std::vector<int16_t> all;
    for (int i = 0; i < 5; i++) {
      char info[80]; snprintf(info, sizeof(info), "!3500.%02dN/13900.%02dE>pkt%d", i, i, i);
      frames.push_back(make_frame("NOCALL", i, info));
      Synth s; s.leadSamples = 500 + 700 * i; s.tailSamples = 200; s.preamble = 10 + 5 * i;
      auto w = synth(frames.back(), s);
      all.insert(all.end(), w.begin(), w.end());
    }
    auto g = run_rx(rx, all);
    CHECK(g.size() == 5);
    for (size_t i = 0; i < g.size() && i < 5; i++) CHECK(same(frames[i], g[i])); }

  // ---- 最短と最長のフレーム ----
  { uint8_t f[AX25_FRAME_MAX]; Ax25Params p = { "NOCALL", 0, "APRS", 0, "" };
    size_t n = ax25_build_ui_frame(p, (const uint8_t*)"", 0, f, sizeof(f));
    std::vector<uint8_t> shortF(f, f + n);
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; auto g = run_rx(rx, synth(shortF, s));
    CHECK(shortF.size() == 18); CHECK(g.size() == 1 && same(shortF, g[0]));

    std::string big(200, 'x');
    for (size_t i = 0; i < big.size(); i++) big[i] = (char)('!' + (i * 7) % 90);
    std::vector<uint8_t> longF;
    { Ax25Params p2 = { "NOCALL", 7, "APRS", 0, "WIDE1-1,WIDE2-1" };
      n = ax25_build_ui_frame(p2, (const uint8_t*)big.data(), big.size(), f, sizeof(f));
      longF.assign(f, f + n); }
    afsk_rx_init(&rx, &c);
    g = run_rx(rx, synth(longF, s));
    CHECK(longF.size() > 230); CHECK(g.size() == 1 && same(longF, g[0])); }

  // ---- ビットスタッフィングが多いデータ (0xFF, 0x7E ばかり) ----
  { std::vector<uint8_t> f2;
    uint8_t f[AX25_FRAME_MAX]; Ax25Params p = { "NOCALL", 0, "APRS", 0, "" };
    uint8_t info[60]; for (int i = 0; i < 60; i++) info[i] = (i % 3 == 0) ? 0xFF : (i % 3 == 1) ? 0x7E : 0x3F;
    size_t n = ax25_build_ui_frame(p, info, sizeof(info), f, sizeof(f));
    f2.assign(f, f + n);
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; auto g = run_rx(rx, synth(f2, s));
    CHECK(g.size() == 1 && same(f2, g[0])); }

  // ---- FCSが壊れているフレームは捨てる ----
  { std::vector<uint8_t> bad = fr; bad[20] ^= 0x04;
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; auto g = run_rx(rx, synth(bad, s));
    CHECK(g.size() == 0); CHECK(rx.fcsErrors == 1 && rx.framesOk == 0);
    std::vector<uint8_t> bad2 = fr; bad2[bad2.size() - 1] ^= 0x80;
    afsk_rx_init(&rx, &c);
    g = run_rx(rx, synth(bad2, s));
    CHECK(g.size() == 0 && rx.fcsErrors == 1); }

  // ---- ノイズ: 信号の振幅は約±1600 (MARK) / ±880 (SPACE)。ノイズの標準偏差を変えて成功率を見る ----
  { const double sig[5] = {50, 150, 300, 450, 700};
    const int minOk[5]  = {50, 50, 50, 40, 0};   // 50回中の最低成功数 (最後は参考値)
    for (int k = 0; k < 5; k++) {
      int ok = 0;
      for (int t = 0; t < 50; t++) {
        AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
        Synth s; s.noise = sig[k]; s.leadSamples = 2000 + (int)(rnd() % 40);
        auto g = run_rx(rx, synth(fr, s));
        if (g.size() == 1 && same(fr, g[0])) ok++;
        CHECK(g.size() <= 1);                     // 壊れたフレームを正しいものとして出さない
        if (g.size() == 1) CHECK(same(fr, g[0]));
      }
      printf("noise sd=%4.0f : %d/50 decoded\n", sig[k], ok);
      CHECK(ok >= minOk[k]);
    } }

  // ---- ノイズだけ・トーンだけ・無音から、フレームが出ない ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    std::vector<int16_t> n;
    for (int i = 0; i < 300000; i++) n.push_back((int16_t)lround(2048 + grand() * 400));
    auto g = run_rx(rx, n); CHECK(g.size() == 0);
    n.clear(); for (int i = 0; i < 100000; i++) n.push_back(2048);
    g = run_rx(rx, n); CHECK(g.size() == 0);
    n.clear(); for (int i = 0; i < 100000; i++) n.push_back((int16_t)lround(1500 * sin(2 * M_PI * 1200.0 * i / 13200.0)));
    g = run_rx(rx, n); CHECK(g.size() == 0);
    n.clear(); for (int i = 0; i < 100000; i++) n.push_back((int16_t)lround(1500 * sin(2 * M_PI * 1700.0 * i / 13200.0)));
    g = run_rx(rx, n); CHECK(g.size() == 0);
    n.clear(); for (int i = 0; i < 100000; i++) n.push_back((i & 1) ? 4095 : -4095);      // 極端な入力
    g = run_rx(rx, n); CHECK(g.size() == 0);
    n.clear(); for (int i = 0; i < 1000; i++) n.push_back((int16_t)(i % 2 ? 32767 : -32768));   // 範囲外の入力もクランプされる
    g = run_rx(rx, n); CHECK(g.size() == 0); }

  // ---- 雑音の後でも続けてフレームを受けられる (状態が壊れない) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    std::vector<int16_t> all;
    for (int i = 0; i < 50000; i++) all.push_back((int16_t)lround(grand() * 700));
    Synth s; auto w = synth(fr, s); all.insert(all.end(), w.begin(), w.end());
    auto g = run_rx(rx, all);
    CHECK(g.size() == 1 && same(fr, g[0])); }


  // ---- 信号検出 (carrier) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    CHECK(!afsk_rx_carrier(&rx));
    for (int i = 0; i < 20000; i++) afsk_rx_process(&rx, (int16_t)lround(2048 + grand() * 6));      // 小さな雑音
    CHECK(!afsk_rx_carrier(&rx));
    Synth s; s.dc = 2048; s.leadSamples = 0; s.tailSamples = 0;
    auto w = synth(fr, s);
    bool seen = false; size_t lastOn = 0;
    for (size_t i = 0; i < w.size(); i++) { afsk_rx_process(&rx, w[i]); if (afsk_rx_carrier(&rx)) { seen = true; lastOn = i; } }
    CHECK(seen); CHECK(afsk_rx_carrier(&rx));                                // 信号の終わった直後はまだ「あり」
    int off = -1;
    for (int i = 0; i < 20000 && off < 0; i++) { afsk_rx_process(&rx, 2048); if (!afsk_rx_carrier(&rx)) off = i; }
    CHECK(off >= 600 && off <= 700);                                          // 13200Hz × 50ms = 660サンプル後に消える
    (void)lastOn;
    // 信号の途中では、トーンが入れ替わっても「あり」が続く
    afsk_rx_init(&rx, &c);
    int drops = 0; bool started = false;
    for (size_t i = 0; i < w.size(); i++) { afsk_rx_process(&rx, w[i]); if (afsk_rx_carrier(&rx)) started = true; else if (started && i < w.size() - 20) drops++; }
    CHECK(started && drops == 0); }

  // ---- 別のサンプルレート設定 (受信 11025Hz / 9600Hz / 22050Hz) ----
  { const uint32_t rr[3] = {11025, 9600, 22050};
    for (int k = 0; k < 3; k++) {
      AfskRxConfig c = rxcfg(rr[k]); CHECK(afsk_rx_init(&rx, &c));
      Synth s; s.txRate = rr[k]; s.decim = 1;
      auto g = run_rx(rx, synth(fr, s));
      if (g.size() != 1) printf("rx rate %u: got %zu\n", rr[k], (size_t)g.size());
      CHECK(g.size() == 1 && same(fr, g[0]));
    } }

  // ---- 監視用の値 (入力レベル、トーン強度、フラグ同期) ----
  { // 純音: 振幅がほぼそのまま読める
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    AfskRxMonitor m;
    for (int n = 0; n < 6000; n++) afsk_rx_process(&rx, (int16_t)lround(500.0 * sin(2 * M_PI * 1200.0 * n / 13200.0)));
    afsk_rx_monitor(&rx, &m);
    CHECK(m.peak >= 460 && m.peak <= 510);                                   // 入力振幅
    CHECK(m.markAmp >= 440 && m.markAmp <= 540);                             // MARK
    printf("leak mark->space: %u of 500\n", m.spaceAmp);
    CHECK(m.spaceAmp < 130);                                                 // SPACEへの漏れ(窓が1ビット幅なので約19%)は小さい
    CHECK(!m.clipped && m.carrier && m.syncFlags == 0);
    for (int n = 0; n < 6000; n++) afsk_rx_process(&rx, (int16_t)lround(300.0 * sin(2 * M_PI * 2200.0 * n / 13200.0)));
    afsk_rx_monitor(&rx, &m);
    printf("leak space->mark: %u of 300\n", m.markAmp);
    CHECK(m.spaceAmp >= 270 && m.spaceAmp <= 330 && m.markAmp < 90);         // 切り替えたら、MARKは落ち、漏れだけが残る
    // 止めたら、約100〜300msで落ちる
    for (int n = 0; n < 4000; n++) afsk_rx_process(&rx, 0);
    afsk_rx_monitor(&rx, &m);
    CHECK(m.peak < 30 && m.markAmp < 20 && m.spaceAmp < 20 && !m.carrier);
    // 振り切れ
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 3000; n++) afsk_rx_process(&rx, (int16_t)lround(2047.0 * sin(2 * M_PI * 1200.0 * n / 13200.0)));
    afsk_rx_monitor(&rx, &m); CHECK(m.clipped && m.peak > 1900);
    for (int n = 0; n < 3000; n++) afsk_rx_process(&rx, 0);                  // 100ms (1320サンプル) 以上おとなしければ消える
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped);
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 3000; n++) afsk_rx_process(&rx, (int16_t)lround(1500.0 * sin(2 * M_PI * 1200.0 * n / 13200.0)));
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped && m.peak > 1400); }

  { // 入力が開放・バイアス不良で端に張り付いている (振れていない) だけなら、振り切れとはしない。直流のずれとして分かる
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c); AfskRxMonitor m;
    for (int n = 0; n < 5000; n++) afsk_rx_process(&rx, -2048);               // 0V に張り付き
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped && m.peak < 50 && m.dcOffset < -1900 && !m.carrier);
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 5000; n++) afsk_rx_process(&rx, 2047);                // 3.3V 側に張り付き
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped && m.dcOffset > 1900);
    // 張り付いたまま小さなノイズが乗っても (端を超えない範囲)、振り切れとはしない
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 5000; n++) afsk_rx_process(&rx, (int16_t)(-2048 + (n % 3)));
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped);
    // バイアスがずれていて (+1000)、信号が端に届くほど大きければ、振り切れ
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 5000; n++) { int v = 1000 + (int)lround(1200.0 * sin(2 * M_PI * 1200.0 * n / 13200.0)); if (v > 2047) v = 2047; afsk_rx_process(&rx, (int16_t)v); }
    afsk_rx_monitor(&rx, &m); CHECK(m.clipped && m.dcOffset > 800 && m.dcOffset < 1100);
    // バイアスが正しい入力 (中心付近) では dcOffset は 0 付近
    afsk_rx_init(&rx, &c);
    for (int n = 0; n < 5000; n++) afsk_rx_process(&rx, (int16_t)lround(500.0 * sin(2 * M_PI * 1200.0 * n / 13200.0)));
    afsk_rx_monitor(&rx, &m); CHECK(!m.clipped && m.dcOffset > -50 && m.dcOffset < 50); }

  { // パケット: プリアンブルの途中から連続フラグが数えられ、組み立て中のバイト数が分かる。終われば戻る
    AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    Synth s; s.dc = 2048 - 2048; auto w = synth(fr, s);
    AfskRxMonitor m; int maxSync = 0, maxBytes = 0, firstSyncAt = -1, frames = 0; bool dataWhileSync = false;
    uint32_t flags0 = rx.flagsSeen;
    for (size_t i = 0; i < w.size(); i++) {
      if (afsk_rx_process(&rx, w[i])) frames++;
      afsk_rx_monitor(&rx, &m);
      if (m.syncFlags > maxSync) maxSync = m.syncFlags;
      if (m.frameBytes > maxBytes) maxBytes = m.frameBytes;
      if (firstSyncAt < 0 && m.syncFlags >= 3) firstSyncAt = (int)i;
      if (m.syncFlags >= 3 && m.carrier && m.inFrame) dataWhileSync = true;
    }
    CHECK(frames == 1 && maxSync >= 40 && dataWhileSync);                    // 前置フラグ60個: 少なくとも40個は連続で数えられる
    CHECK(firstSyncAt > 0 && firstSyncAt < 3000 + 13200 / 10);                // 信号の頭から約100ms以内に「データ」と判断できる
    CHECK(maxBytes >= (int)fr.size() - 4 || maxBytes >= 30);                  // 組み立て中のバイト数が増えていく
    CHECK(rx.flagsSeen - flags0 >= 60 + 2);                                   // 前置+閉じのフラグ
    for (int i = 0; i < 2000; i++) afsk_rx_process(&rx, 0);
    afsk_rx_monitor(&rx, &m); CHECK(!m.carrier && m.syncFlags == 0); }

  { // ノイズだけでは「データ」(連続フラグ3個以上) にならない。強いノイズで carrier は立ってよい
    const double sd[5] = { 30, 100, 300, 700, 1200 };
    for (int k = 0; k < 5; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c); AfskRxMonitor m; int maxSync = 0; bool carrierSeen = false;
      for (int n = 0; n < 13200 * 120; n++) {                                 // 2分ぶん
        afsk_rx_process(&rx, (int16_t)lround(grand() * sd[k]));
        if ((n & 15) == 0) { afsk_rx_monitor(&rx, &m); if (m.syncFlags > maxSync) maxSync = m.syncFlags; if (m.carrier) carrierSeen = true; }
      }
      if (maxSync >= 3) printf("noise sd=%.0f: maxSync=%d\n", sd[k], maxSync);
      CHECK(maxSync < 3);
      if (k >= 2) CHECK(carrierSeen);                                         // 強いノイズでも carrier は立つ (これだけでは「データ」と区別できない、という確認)
    } }

  // ---- 信号がないときは解読しない: 小さな雑音・無音を長く入れても、フラグもエラーも数えない (0.13.3) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    for (int n = 0; n < 13200 * 120; n++) afsk_rx_process(&rx, (int16_t)lround(2048 + grand() * 6));   // 2分ぶん (入力をGNDに落とした程度)
    for (int n = 0; n < 13200 * 10; n++) afsk_rx_process(&rx, 2048);
    CHECK(rx.flagsSeen == 0 && rx.fcsErrors == 0 && rx.fragments == 0 && rx.framesOk == 0 && !rx.inFrame);
    // そのあとでも普通に受けられる
    Synth s; s.dc = 2048; auto g = run_rx(rx, synth(fr, s)); CHECK(g.size() == 1 && same(fr, g[0])); }

  // ---- 短すぎるかけらは「雑音」、長さが正しくて CRC が合わないものは「CRC不良」として別に数える ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c);
    std::vector<uint8_t> shortFr(fr.begin(), fr.begin() + 6);                 // 17バイト未満
    Synth s; auto g = run_rx(rx, synth(shortFr, s));
    CHECK(g.size() == 0 && rx.fragments >= 1 && rx.fcsErrors == 0); }

  // ---- 調整トーン (1200Hz と 2200Hz を交互) の強さの比が正しく出る (0.13.2 までは 2200Hz への漏れで、比が大きいほど小さく出ていた) ----
  { const double R[5] = { 0.5, 1.0, 2.0, 3.0, 6.0 };
    for (int k = 0; k < 5; k++) {
      AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c); AfskRxMonitor m;
      for (int t = 0; t < 4; t++) for (int i = 0; i < 6600; i++) {
        bool sp = t & 1; double a = sp ? 1000.0 / R[k] : 1000.0, f = sp ? 2200.0 : 1200.0;
        afsk_rx_process(&rx, (int16_t)lround(a * sin(2 * M_PI * f * i / 13200.0)));
      }
      afsk_rx_monitor(&rx, &m);
      double got = (double)m.markAvgAmp / (double)m.spaceAvgAmp;
      if (fabs(got / R[k] - 1.0) > 0.05) printf("tone ratio %.1f: got %.2f\n", R[k], got);
      CHECK(fabs(got / R[k] - 1.0) <= 0.05);
    } }

  // ---- 最後に正しく受けたフレームの MARK/SPACE の振幅 (受信状態画面の「直近 … 比」) ----
  { AfskRxConfig c = rxcfg(); afsk_rx_init(&rx, &c); AfskRxMonitor m;
    afsk_rx_monitor(&rx, &m); CHECK(m.lastOkMarkAmp == 0 && m.lastOkSpaceAmp == 0);   // まだない
    Synth s; s.ampMark = 100; s.ampSpace = 50;
    auto g = run_rx(rx, synth(fr, s)); CHECK(g.size() == 1);
    for (int i = 0; i < 20000; i++) afsk_rx_process(&rx, 0);                   // 信号が終わっても残る
    afsk_rx_monitor(&rx, &m);
    double r = m.lastOkSpaceAmp ? (double)m.lastOkMarkAmp / m.lastOkSpaceAmp : 0;
    if (r < 1.85 || r > 2.15 || m.lastOkMarkAmp <= 300) printf("last ratio %.2f (%u/%u)\n", r, (unsigned)m.lastOkMarkAmp, (unsigned)m.lastOkSpaceAmp);
    CHECK(r >= 1.85 && r <= 2.15 && m.lastOkMarkAmp > 300); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
