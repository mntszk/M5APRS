// afsk_tx_core.h
// AX.25フレーム → HDLC(フラグ、ビットスタッフィング、NRZI) → 1200bps AFSK(Bell 202)の
// DAC用サンプル列、を作る部分。M5Stack/Arduinoの部品に依存しない。
//
// 使い方(割り込みを使う側のイメージ):
//   afsk_tx_init(&tx, &cfg);                     // 1回だけ
//   afsk_tx_prepare(&tx, frame, frameLen);       // 送信前。ビット列を作る(割り込みの外で行う)
//   afsk_tx_start(&tx);                          // 再生を始める
//   タイマ割り込み(cfg.sampleRateHz)の中で:
//       uint8_t v; if (afsk_tx_next_sample(&tx, &v)) dacWrite(pin, v); else 送信終了の処理;
// 割り込みの中では afsk_tx_next_sample だけを呼ぶ(処理は数回の積和と表引きだけ)。

#ifndef AFSK_TX_CORE_H
#define AFSK_TX_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifndef IRAM_ATTR
#if defined(ARDUINO_ARCH_ESP32)
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif
#endif

// 作れるビット数の上限(プリアンブル + フレーム + スタッフィング + テール)
#define AFSK_TX_MAX_BITS  5000

struct AfskTxConfig {
  uint32_t sampleRateHz;   // DACの出力レート(Hz)
  uint16_t baud;           // 1200
  uint16_t markHz;         // 1200
  uint16_t spaceHz;        // 2200
  uint8_t  ampMark;        // MARKの振幅 0〜127 (DAC中心値128からの振れ幅)
  uint8_t  ampSpace;       // SPACEの振幅 0〜127
  uint8_t  preambleFlags;  // プリアンブルのフラグ数 (1以上)
  uint8_t  tailFlags;      // 後尾のフラグ数
};

struct AfskTx {
  AfskTxConfig cfg;
  int8_t       sine[256];                        // 正弦波テーブル (-127〜127)
  uint32_t     incMark, incSpace;                // 位相の増分 (32ビット位相)
  uint8_t      bits[(AFSK_TX_MAX_BITS + 7) / 8]; // NRZI符号化後のビット列 (1=MARK, 0=SPACE)
  uint16_t     nbits;
  // 再生状態
  volatile bool active;
  uint16_t     bitIndex;
  uint32_t     bitAcc;
  uint32_t     phase;
  // 連続トーン (調整用)。toneMode が 0 以外のとき、ビット列ではなく連続トーンを出す
  uint8_t      toneMode;                         // 0=なし 1=MARK 2=SPACE 3=交互
  bool         toneMark;                         // 交互のとき、現在のトーン
  uint32_t     toneLeft;                         // 交互のとき、切り替えまでの残りサンプル数
};

// 連続トーンのモード
#define AFSK_TONE_MARK   1    // 1200Hz
#define AFSK_TONE_SPACE  2    // 2200Hz
#define AFSK_TONE_ALT    3    // 1200Hzと2200Hzを0.5秒ごとに交互 (位相は連続)

// 初期化。cfgの値が不正(周波数やレートが0、振幅が127超、プリアンブルが0)なら false。
bool afsk_tx_init(AfskTx* t, const AfskTxConfig* cfg);

// フレーム(アドレス部〜FCSまで。packet_buildの出力)から、送出するビット列を作る。
// フレームが長すぎてビット列に収まらないときは false。再生中に呼んではいけない。
bool afsk_tx_prepare(AfskTx* t, const uint8_t* frame, size_t len);

// 再生を始める。prepare の後に呼ぶ。
void afsk_tx_start(AfskTx* t);

// 連続トーンを始める (調整用)。止めるまで終わらない。mode は AFSK_TONE_*。不正なら何もしない。
// 振幅は MARK=ampMark、SPACE=ampSpace (通常の送信と同じ)。
void afsk_tx_start_tone(AfskTx* t, uint8_t mode);

// 次のサンプル(0〜255、無音は128)を out に入れる。
//   戻り値 true : サンプルを出した(続けて呼ぶ)
//   戻り値 false: 送信が終わった(out は128)
bool IRAM_ATTR afsk_tx_next_sample(AfskTx* t, uint8_t* out);

// 再生中なら true
bool afsk_tx_is_active(const AfskTx* t);

// 送信を途中で打ち切る (再生を止め、次の afsk_tx_prepare ができる状態に戻す)。割り込みを止めてから呼ぶこと。
void afsk_tx_abort(AfskTx* t);

#endif // AFSK_TX_CORE_H
