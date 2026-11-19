// test_afsk_tx_core.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "afsk_tx_core.h"
#include "packet_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static AfskTxConfig defcfg() {
  AfskTxConfig c; memset(&c, 0, sizeof(c));
  c.sampleRateHz = 26400; c.baud = 1200; c.markHz = 1200; c.spaceHz = 2200;
  c.ampMark = 100; c.ampSpace = 55; c.preambleFlags = 60; c.tailFlags = 4;
  return c;
}

static int bit_at(const AfskTx& t, int i) { return (t.bits[i >> 3] >> (i & 7)) & 1; }

// 参照用のHDLC復号: NRZI復号 → フラグ検出 → ビットデスタッフィング → バイト化。
// 最初の「フラグの連続」の後ろから、次のフラグまでのフレームを取り出す。
static bool ref_decode(const AfskTx& t, std::vector<uint8_t>* frame, bool* stuffingOk) {
  std::vector<int> d;
  int prev = 1;
  for (int i = 0; i < t.nbits; i++) {
    int cur = bit_at(t, i);
    d.push_back(cur == prev ? 1 : 0);
    prev = cur;
  }
  *stuffingOk = true;
  // フラグ 01111110 (送出順)を探す
  auto isFlagAt = [&](size_t p) {
    static const int f[8] = {0,1,1,1,1,1,1,0};
    if (p + 8 > d.size()) return false;
    for (int k = 0; k < 8; k++) if (d[p + k] != f[k]) return false;
    return true;
  };
  size_t p = 0;
  while (p + 8 <= d.size() && isFlagAt(p)) p += 8;     // プリアンブルを読み飛ばす
  std::vector<int> bits;
  int ones = 0;
  while (p < d.size()) {
    if (isFlagAt(p)) break;
    int b = d[p++];
    if (ones == 5) {                                    // 1が5つ続いた後の0は、挿入されたもの
      if (b != 0) { *stuffingOk = false; return false; }
      ones = 0; continue;
    }
    if (b) ones++; else ones = 0;
    bits.push_back(b);
  }
  if (bits.size() % 8 != 0) return false;
  frame->clear();
  for (size_t i = 0; i < bits.size(); i += 8) {
    uint8_t v = 0;
    for (int k = 0; k < 8; k++) v |= (uint8_t)(bits[i + k] << k);
    frame->push_back(v);
  }
  return true;
}

// Goertzel: 窓の中の、指定周波数の成分の強さ
static double goertzel(const std::vector<uint8_t>& s, size_t from, size_t n, double f, double fs) {
  double w = 2.0 * M_PI * f / fs, c = 2.0 * cos(w), q1 = 0, q2 = 0;
  for (size_t i = 0; i < n; i++) {
    double q0 = (s[from + i] - 128.0) + c * q1 - q2;
    q2 = q1; q1 = q0;
  }
  return q1 * q1 + q2 * q2 - c * q1 * q2;
}

static std::vector<uint8_t> render(AfskTx& t) {
  std::vector<uint8_t> out; uint8_t v;
  afsk_tx_start(&t);
  while (afsk_tx_next_sample(&t, &v)) out.push_back(v);
  return out;
}

int main() {
  test_cfg_setup();
  // --- 実際のフレームを作る ---
  uint8_t info[APRS_INFO_MAX];
  PositionReport r; memset(&r, 0, sizeof(r));
  r.latDeg = 35.6586; r.lonDeg = 139.7454; r.symTable = '/'; r.symCode = '>'; r.comment = "M5Stack test";
  size_t infoLen = aprs_build_position_info(r, '!', info, sizeof(info));
  uint8_t frame[AX25_FRAME_MAX];
  Ax25Params p = { "NOCALL", 9, "APRS", 0, "WIDE1-1,WIDE2-1" };
  size_t frameLen = ax25_build_ui_frame(p, info, infoLen, frame, sizeof(frame));
  CHECK(frameLen > 0);

  AfskTxConfig cfg = defcfg();
  static AfskTx tx;
  CHECK(afsk_tx_init(&tx, &cfg));
  CHECK(afsk_tx_prepare(&tx, frame, frameLen));

  // --- ビット列: 参照復号で元のフレームに戻る ---
  { std::vector<uint8_t> dec; bool sok;
    CHECK(ref_decode(tx, &dec, &sok));
    CHECK(sok);
    CHECK(dec.size() == frameLen && memcmp(dec.data(), frame, frameLen) == 0);
    CHECK(ax25_fcs(dec.data(), dec.size()) == 0x0F47);       // FCSが正しい
    // ビット数 >= (プリアンブル + 終わり1 + テール) × 8 + フレーム
    CHECK(tx.nbits >= (60 + 1 + 4) * 8 + frameLen * 8); }

  // --- 波形: サンプル数、振幅、各ビットのトーン ---
  { std::vector<uint8_t> s = render(tx);
    CHECK((int)s.size() == tx.nbits * 22);                      // 26400/1200 = 22サンプル/ビット
    CHECK(!afsk_tx_is_active(&tx));
    uint8_t v = 0; CHECK(!afsk_tx_next_sample(&tx, &v) && v == 128);   // 終了後は無音
    int maxMark = 0, maxSpace = 0, wrong = 0;
    for (int i = 0; i < tx.nbits; i++) {
      int mk = bit_at(tx, i);
      double pm = goertzel(s, (size_t)i * 22, 22, 1200, 26400);
      double ps = goertzel(s, (size_t)i * 22, 22, 2200, 26400);
      if ((pm > ps) != (mk == 1)) wrong++;
      for (int k = 0; k < 22; k++) {
        int a = abs((int)s[(size_t)i * 22 + k] - 128);
        if (mk) { if (a > maxMark) maxMark = a; } else { if (a > maxSpace) maxSpace = a; }
      }
    }
    CHECK(wrong == 0);
    CHECK(maxMark <= 100 && maxMark >= 95);
    CHECK(maxSpace <= 55 && maxSpace >= 50);
    for (size_t i = 0; i < s.size(); i++) CHECK(s[i] >= 1); }

  // --- 位相の連続(振幅を同じにして、トーン切り替え時に波形が飛ばないこと) ---
  { AfskTxConfig c2 = defcfg(); c2.ampMark = 100; c2.ampSpace = 100;
    static AfskTx t2; CHECK(afsk_tx_init(&t2, &c2)); CHECK(afsk_tx_prepare(&t2, frame, frameLen));
    std::vector<uint8_t> s = render(t2);
    int maxStep = 0;
    for (size_t i = 1; i < s.size(); i++) { int d = abs((int)s[i] - (int)s[i - 1]); if (d > maxStep) maxStep = d; }
    // 2200Hzの1サンプルあたり最大変化 = 100 * 2*sin(pi*2200/26400) ≒ 51
    CHECK(maxStep <= 56); }

  // --- ビットスタッフィング: 0xFFが続くフレーム ---
  { uint8_t fr[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0x7E, 0x7E };
    static AfskTx t3; CHECK(afsk_tx_init(&t3, &cfg)); CHECK(afsk_tx_prepare(&t3, fr, sizeof(fr)));
    std::vector<uint8_t> dec; bool sok;
    CHECK(ref_decode(t3, &dec, &sok));
    CHECK(sok && dec.size() == sizeof(fr) && memcmp(dec.data(), fr, sizeof(fr)) == 0);
    // フラグ以外の部分に、1が6個以上連続しないこと
    std::vector<int> d; int prev = 1;
    for (int i = 0; i < t3.nbits; i++) { int cur = bit_at(t3, i); d.push_back(cur == prev ? 1 : 0); prev = cur; }
    size_t start = (size_t)cfg.preambleFlags * 8, end = d.size() - ((size_t)cfg.tailFlags + 1) * 8;
    int run = 0, maxRun = 0;
    for (size_t i = start; i < end; i++) { if (d[i]) { run++; if (run > maxRun) maxRun = run; } else run = 0; }
    CHECK(maxRun <= 5); }

  // --- サンプリングレートが整数倍でない場合 ---
  { AfskTxConfig c4 = defcfg(); c4.sampleRateHz = 11025;
    static AfskTx t4; CHECK(afsk_tx_init(&t4, &c4)); CHECK(afsk_tx_prepare(&t4, frame, frameLen));
    std::vector<uint8_t> s = render(t4);
    double expect = (double)t4.nbits * 11025.0 / 1200.0;
    CHECK(fabs((double)s.size() - expect) <= 1.0); }
  { AfskTxConfig c5 = defcfg(); c5.sampleRateHz = 9600;
    static AfskTx t5; CHECK(afsk_tx_init(&t5, &c5)); CHECK(afsk_tx_prepare(&t5, frame, frameLen));
    std::vector<uint8_t> s = render(t5);
    CHECK((int)s.size() == t5.nbits * 8); }

  // --- 不正な設定・入力 ---
  { AfskTxConfig b = defcfg(); static AfskTx tb;
    b = defcfg(); b.sampleRateHz = 0;     CHECK(!afsk_tx_init(&tb, &b));
    b = defcfg(); b.sampleRateHz = 4000;  CHECK(!afsk_tx_init(&tb, &b));     // 低すぎる
    b = defcfg(); b.ampMark = 128;        CHECK(!afsk_tx_init(&tb, &b));
    b = defcfg(); b.preambleFlags = 0;    CHECK(!afsk_tx_init(&tb, &b));
    CHECK(afsk_tx_init(&tb, &cfg));
    CHECK(!afsk_tx_prepare(&tb, frame, 0));
    uint8_t big[800]; memset(big, 0x55, sizeof(big));
    CHECK(!afsk_tx_prepare(&tb, big, sizeof(big)));                          // ビット列に収まらない
    CHECK(tb.nbits == 0);
    CHECK(afsk_tx_prepare(&tb, frame, frameLen));
    afsk_tx_start(&tb);
    CHECK(afsk_tx_is_active(&tb));
    CHECK(!afsk_tx_prepare(&tb, frame, frameLen)); }                         // 再生中は作り直せない

  // 途中で打ち切り: 再生が止まり、次の準備ができる
  { AfskTxConfig c = defcfg(); static AfskTx ta; CHECK(afsk_tx_init(&ta, &c));
    CHECK(afsk_tx_prepare(&ta, frame, frameLen)); afsk_tx_start(&ta);
    uint8_t v; for (int i = 0; i < 500; i++) afsk_tx_next_sample(&ta, &v);
    CHECK(afsk_tx_is_active(&ta));
    afsk_tx_abort(&ta);
    CHECK(!afsk_tx_is_active(&ta) && !afsk_tx_next_sample(&ta, &v) && v == 128);
    CHECK(afsk_tx_prepare(&ta, frame, frameLen)); afsk_tx_start(&ta); CHECK(afsk_tx_is_active(&ta));
    afsk_tx_abort(&ta); afsk_tx_abort(&ta); afsk_tx_abort(NULL); CHECK(!afsk_tx_is_active(&ta)); }

  // ===== 連続トーン (調整用) =====
  { AfskTxConfig c = defcfg(); static AfskTx tt;
    // 立ち上がりのゼロ交差を数える (1秒ぶん)
    auto crossings = [&](AfskTx* x, int n, int* mn, int* mx) { int prev = 128, cnt = 0; *mn = 255; *mx = 0;
      for (int i = 0; i < n; i++) {
        uint8_t v; bool ok = afsk_tx_next_sample(x, &v);
        CHECK(ok);
        if (prev < 128 && v >= 128) { cnt++; }
        prev = v;
        if (v < *mn) { *mn = v; }
        if (v > *mx) { *mx = v; }
      }
      return cnt; };
    int mn, mx;
    CHECK(afsk_tx_init(&tt, &c));
    afsk_tx_start_tone(&tt, AFSK_TONE_MARK); CHECK(afsk_tx_is_active(&tt));
    int n1 = crossings(&tt, c.sampleRateHz, &mn, &mx);
    CHECK(n1 >= 1199 && n1 <= 1201 && mn >= 128 - c.ampMark && mx <= 128 + c.ampMark && mx - 128 > c.ampMark - 3);
    CHECK(afsk_tx_is_active(&tt));                                  // 自分では終わらない
    for (int i = 0; i < 200000; i++) { uint8_t v; CHECK(afsk_tx_next_sample(&tt, &v)); }
    afsk_tx_abort(&tt); CHECK(!afsk_tx_is_active(&tt));
    { uint8_t v; CHECK(!afsk_tx_next_sample(&tt, &v) && v == 128); }
    afsk_tx_start_tone(&tt, AFSK_TONE_SPACE);
    int n2 = crossings(&tt, c.sampleRateHz, &mn, &mx);
    CHECK(n2 >= 2199 && n2 <= 2201 && mx <= 128 + c.ampSpace && mx - 128 > c.ampSpace - 3);
    afsk_tx_start_tone(&tt, AFSK_TONE_MARK); CHECK(tt.toneMode == AFSK_TONE_SPACE);   // 再生中は始め直せない
    afsk_tx_abort(&tt);
    afsk_tx_start_tone(&tt, 0); CHECK(!afsk_tx_is_active(&tt));       // 不正なモードは何もしない
    afsk_tx_start_tone(&tt, 4); CHECK(!afsk_tx_is_active(&tt));
    afsk_tx_start_tone(&tt, 255); CHECK(!afsk_tx_is_active(&tt));
    // 交互: 0.5秒ごとに MARK ⇔ SPACE。最初はMARK
    afsk_tx_start_tone(&tt, AFSK_TONE_ALT);
    int half = (int)(c.sampleRateHz / 2);
    int a1 = crossings(&tt, half, &mn, &mx), a2 = crossings(&tt, half, &mn, &mx), a3 = crossings(&tt, half, &mn, &mx), a4 = crossings(&tt, half, &mn, &mx);
    CHECK(a1 >= 599 && a1 <= 601 && a2 >= 1099 && a2 <= 1101 && a3 >= 599 && a3 <= 601 && a4 >= 1099 && a4 <= 1101);
    afsk_tx_abort(&tt);
    // 中止のあと、通常の送信ができる (トーンの状態が残らない)
    CHECK(afsk_tx_prepare(&tt, frame, frameLen)); afsk_tx_start(&tt); CHECK(tt.toneMode == 0);
    { int guard = 0; uint8_t v; while (afsk_tx_next_sample(&tt, &v) && guard++ < 2000000) {} CHECK(guard < 2000000 && !afsk_tx_is_active(&tt)); } }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
