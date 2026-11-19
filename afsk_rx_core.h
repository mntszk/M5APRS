// afsk_rx_core.h
// ADCのサンプル列 → 1200bps AFSK(Bell 202)復調 → HDLC → AX.25フレーム(FCS検査済み)、を行う部分。
// M5Stack/Arduinoの部品に依存しない(パソコン上でテストできる)。
//
// 方式:
//   1. 直流分を除く
//   2. MARK/SPACE それぞれについて、sin/cos との相関を「1ビット分の窓」で滑らせて求める (直交相関検波)
//      → 各トーンの強さ(エネルギー)
//   3. トーンごとの強さの基準値(ピークホールド)で割って補正する (プリエンファシス/ディエンファシスによる
//      MARK/SPACEの強さの違いを吸収)
//   4. MARK/SPACEの判定の切り替わりでビット同期用のデジタルPLLを補正し、1ビットごとに判定
//      信号あり (どちらかのトーンの強さが carrierMinEnergy を超えた。ハングを含む) のときだけ、ビットをHDLCに渡す。
//      信号がないときは、組み立て中のフレームを数えずに捨てる (雑音から偽のフラグ・エラーを作らないため。0.13.3)
//   5. NRZI復号 → フラグ検出 → ビットのデスタッフィング → バイト化 → FCS検査
//
// 使い方:
//   afsk_rx_init(&rx, &cfg);                                  // 1回だけ
//   (ADCのサンプルは割り込みでリングバッファに入れるだけ。割り込みの外で以下を呼ぶ)
//   if (afsk_rx_process(&rx, sample)) { auto f = afsk_rx_frame(&rx, &len); ... }
// afsk_rx_process は割り込みの外(loopやタスク)から呼ぶこと。

#ifndef AFSK_RX_CORE_H
#define AFSK_RX_CORE_H

#include <stdint.h>
#include <stddef.h>

#define AFSK_RX_FRAME_MAX   332   // 受け取るフレームの最大長(FCS込み)。超えたら破棄
#define AFSK_RX_MAX_WINDOW  64    // 相関の窓(= サンプルレート/ボーレート)の上限

struct AfskRxConfig {
  uint32_t sampleRateHz;    // ADCのサンプルレート(Hz)
  uint16_t baud;            // 1200
  uint16_t markHz;          // 1200
  uint16_t spaceHz;         // 2200
  uint32_t toneLevelInit;   // トーンの強さの基準値の初期値 (エネルギーの単位。fw_config.h の RX_TONE_LEVEL_INIT)
  uint32_t carrierMinEnergy;// 「信号あり」と見なすトーンの強さ。0なら既定値 (50000。振幅が約40以上のトーン)
  uint16_t carrierHangMs;   // 信号が途切れてからも「信号あり」を保つ時間 (ms)。0なら既定値 (50)
};

struct AfskRx {
  AfskRxConfig cfg;
  // 相関
  int16_t   sinTab[256];
  uint32_t  incMark, incSpace, phMark, phSpace;
  uint16_t  win, wpos;
  int32_t   mI[AFSK_RX_MAX_WINDOW], mQ[AFSK_RX_MAX_WINDOW];
  int32_t   sI[AFSK_RX_MAX_WINDOW], sQ[AFSK_RX_MAX_WINDOW];
  int32_t   sumMI, sumMQ, sumSI, sumSQ;
  // 直流分
  int32_t   dcAcc;
  bool      dcInit;
  // 補正用のトーンの強さの基準値
  uint32_t  levelMark, levelSpace;
  // ビット同期(DPLL)
  uint32_t  pll;            // 0〜sampleRate。baud ずつ増え、sampleRate に達したらビットを判定
  bool      curTone;        // 現在の判定 (true=MARK)
  bool      prevBitTone;    // 1つ前のビットのトーン (NRZI用)
  // HDLC
  uint8_t   ones;           // 連続して来た1の数
  bool      inFrame;
  uint16_t  nbits;
  uint8_t   work[AFSK_RX_FRAME_MAX];
  // 出力
  uint8_t   out[AFSK_RX_FRAME_MAX];
  size_t    outLen;
  // 信号検出 (設定の書き込みなど、受信を邪魔してはいけない処理を避けるために使う)
  uint32_t  carrierMin, hangSamples, hang;
  // 監視用 (画面表示。復調の判定には使わない)
  int32_t   peak;                // 入力振幅(直流分を除いた絶対値)のピークホールド
  uint32_t  clipHang;            // 入力が振り切れたあと、表示を保つサンプル数
  uint32_t  peakMarkE, peakSpaceE; // MARK/SPACEのトーンの強さ(エネルギー)のピークホールド (バー用)
  uint32_t  avgMarkE, avgSpaceE;   // そのトーンが優勢なときの強さの平均 (比の計算用)
  uint8_t   syncFlags;           // 連続して見つかったフラグの数 (信号が途切れたら0)
  // 統計
  uint32_t  framesOk, fcsErrors, aborts, overflows;   // fcsErrors: 長さは正しいのに CRC が合わない (本物の受信失敗)
  uint32_t  fragments;           // フラグの間の半端なかけら (バイト単位でない・短すぎる。主に信号の端の雑音)
  uint32_t  lastOkMarkE, lastOkSpaceE;   // 最後に正しく受けたフレームの終わりの MARK/SPACE の強さ (ピークホールド)
  uint32_t  flagsSeen;           // 見つかったフラグの累計
};

// 画面表示用の値
struct AfskRxMonitor {
  uint16_t peak;                 // 入力振幅 (ADC値の単位。振り切れは約2048)
  bool     clipped;              // 最近、信号が振れてADCの端に当たった (直流が端に張り付いているだけでは立たない)
  int16_t  dcOffset;             // 直流分の推定 (ADC値で、中心からのずれ。バイアスが正しければ 0 付近)
  uint16_t markAmp, spaceAmp;    // MARK(1200Hz)/SPACE(2200Hz)トーンの振幅 (ADC値の単位。ピークホールド。バー用)
  uint16_t markAvgAmp, spaceAvgAmp;   // 同じく、そのトーンが優勢なときの平均 (比の計算用。信号がなくなっても残る)
  bool     carrier;              // afsk_rx_carrier と同じ
  uint8_t  syncFlags;            // 連続フラグ数。3以上なら、AFSKのパケットが来ているとみなせる
  bool     inFrame;              // フラグを見つけて、フレームを組み立てている
  uint16_t frameBytes;           // 組み立て中のバイト数
  uint32_t flagsSeen;            // フラグ検出の累計
  uint16_t lastOkMarkAmp, lastOkSpaceAmp;   // 最後に正しく受けたフレームの MARK/SPACE の振幅 (0 = まだない)
};

// 初期化。cfgが不正(0、窓が上限超え、周波数がナイキスト以上)なら false。
bool afsk_rx_init(AfskRx* r, const AfskRxConfig* cfg);

// 状態を最初に戻す(統計は残す)。
void afsk_rx_reset(AfskRx* r);

// ADCの値1サンプル(中心付近の符号付き。12ビットADCなら 0〜4095 をそのまま、または中心を引いた値)を処理する。
// FCSが正しいフレームが1つ完成したときだけ true を返す。
bool afsk_rx_process(AfskRx* r, int16_t sample);

// 途中まで復調していたフレームを捨て、フラグの検出からやり直す (サンプルの欠落があったときなど)。
// 基準値や直流分の推定は保つ。
void afsk_rx_discard_frame(AfskRx* r);

// 信号(MARKまたはSPACEのトーン)を受けている最中、または途切れて hang 時間以内なら true。
// この間は、フラッシュへの書き込み(Preferences)など、割り込みを止める処理をしないこと。
bool afsk_rx_carrier(const AfskRx* r);

// 画面表示用の値を取り出す (割り込みの外から。afsk_rx_process と同じ場所で呼ぶ)
void afsk_rx_monitor(const AfskRx* r, AfskRxMonitor* out);

// 直前に true を返したときのフレーム(アドレス部〜情報フィールド。FCSは除く)。
// 次に afsk_rx_process を呼ぶまで有効。
const uint8_t* afsk_rx_frame(const AfskRx* r, size_t* len);

#endif // AFSK_RX_CORE_H
