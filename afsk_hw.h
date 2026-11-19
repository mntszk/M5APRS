// afsk_hw.h
// M5Stack Basic (ESP32, Arduinoコア 2.1.4) のタイマー割り込みで、
//   送信: afsk_tx_core のサンプルを DAC (AFSK_TX_DAC_PIN。既定はGPIO26。PTT出力とモニター出力もここで扱う) へ出力
//   受信: ADC (GPIO36) のサンプルをリングバッファへ入れ、loopで afsk_rx_core により復調
// を行う。送信中は受信を止め、終わったら受信に戻る (半二重)。
// ESP32専用 (パソコンでは stubs/ の代用品でコンパイル・動作確認だけ行う)。
//
// 使い方:
//   setup(): afsk_hw_begin();
//   loop():  afsk_hw_poll(millis());                       // 毎回呼ぶ (受信の復調と、送信終了の処理)
//            QueuedPacket p; while (afsk_hw_pop_frame(&p)) {...}   // 受信したフレーム (FCSなし)
//   送信:    afsk_hw_send(frame, len);                      // packet_build の出力 (FCS込み)。すぐ戻る
//            while (afsk_hw_tx_busy()) afsk_hw_poll(millis());      // または、画面に「送信中」を出しつつloopを回す
//   設定の保存: settings_commit(&settings, afsk_hw_busy());
//
// 注意:
//   - M5Stackライブラリの内蔵スピーカー(M5.Speaker など)は使わないこと。DAC(GPIO25/26)を取り合う。
//     M5.begin() でスピーカーを初期化する設定なら、afsk_hw_begin() の後に M5.Speaker.end() などで解放する。
//   - 割り込みの中では、DAC/ADCの読み書きとリングバッファの操作だけを行う。
//   - Preferences/SD/画面描画などでフラッシュやSPIを長く占有すると、割り込みが遅れて受信を取りこぼす。
//     afsk_hw_busy() が true の間は、設定の書き込みなどを避ける。

#ifndef AFSK_HW_H
#define AFSK_HW_H

#include <stdint.h>
#include <stddef.h>
#include "packet_queue.h"

#ifndef CALIB_MAX_ON_MS
#define CALIB_MAX_ON_MS     30000      // 調整トーンを出し続ける最長時間 (ms)。超えたら自動で止める
#endif

struct AfskHwStats {
  uint32_t rxSamples;       // 復調に回したサンプル数
  uint32_t rxOverruns;      // リングバッファが満杯で捨てたサンプル数 (loopが遅れた)
  uint32_t rxFramesOk;      // FCSが正しかったフレーム数
  uint32_t rxFcsErrors;     // FCS不一致 (長さは正しいフレーム。本物の受信失敗)
  uint32_t rxFragments;     // フラグの間の半端なかけら (主に信号の端の雑音。受信失敗ではない)
  uint32_t rxDiscards;      // サンプルの欠落で、途中のフレームを捨てた回数
  uint32_t queueDropped;    // 待ち行列が満杯で破棄した受信フレーム数
  uint32_t txCount;         // 送信したフレーム数
  uint32_t rxFlags;         // 見つかったフラグ(0x7E)の累計
  uint32_t pttTimeouts;     // PTT最大時間を超えて、送信を中止した回数
};

// 初期化して受信を始める。ピンやタイマーの設定に失敗したら false。
// 受信の状態 (画面表示用)
enum AfskRxPhase {
  AFSK_PHASE_IDLE = 0,      // 信号なし (送信中もここ)
  AFSK_PHASE_SIGNAL,        // 信号あり。ただしAFSKのパケットとは確認できていない (ノイズ、音声、他の変調など)
  AFSK_PHASE_DATA           // AFSKのパケットが来ている (フラグが RX_DATA_SYNC_FLAGS 個以上連続した)
};
enum AfskRxResult { AFSK_RESULT_NONE = 0, AFSK_RESULT_OK, AFSK_RESULT_FCS_ERROR };

struct AfskHwRxStatus {
  AfskRxPhase  phase;
  uint16_t     peak;                // 入力振幅 (ADC値。振り切れは約2048)
  bool         clipped;             // 入力が振り切れた (信号が振れて端に当たった)
  int16_t      dcOffset;            // 直流分のずれ (ADC値。中心 2048 からの差。大きいときはバイアス回路を確認)
  uint16_t     markAmp, spaceAmp;   // MARK/SPACEのトーンの振幅 (ADC値。ピークホールド。バー用)
  uint16_t     markAvgAmp, spaceAvgAmp;   // そのトーンが優勢なときの平均の振幅 (比の表示用)
  uint8_t      syncFlags;           // 連続フラグ数
  uint16_t     frameBytes;          // 組み立て中のバイト数
  AfskRxResult lastResult;          // 直近に完成したフレームの結果
  uint32_t     lastResultMs;        // その時刻 (millis)
  uint32_t     resultSeq;           // 結果が出るたびに増える
  uint16_t     lastOkMarkAmp, lastOkSpaceAmp;   // 最後に正しく受けたフレームの MARK/SPACE の振幅 (0 = まだない)
};

bool afsk_hw_begin();

// loopから毎回呼ぶ。nowMs は millis()。
void afsk_hw_poll(uint32_t nowMs);

// 送信を始める (すぐ戻る)。PTTオン → 待ち → 送出 → 待ち → PTTオフ は afsk_hw_poll が進める (loopを回し続けること)。frame は AX.25 フレーム (FCS込み)。
// 送信中、または frame が不正なら false。受信は止まり、終わると自動で再開する。
bool afsk_hw_send(const uint8_t* frame, size_t len);

// 送信音のモニター音量 (0=ミュート〜MONITOR_LEVEL_MAX)。次の送信から反映される (送信中は変えない)。
// モニター出力ピンが無い/使えないときは何も起きない。afsk_hw_begin の前に呼んでもよい。
void    afsk_hw_set_monitor_level(uint8_t level);
uint8_t afsk_hw_monitor_level();

// 調整用の連続トーン (減衰回路・送信レベルの調整用)。PTTを入れて、止めるまで(最長 CALIB_MAX_ON_MS)トーンを出す。
// mode は AFSK_TONE_MARK / SPACE / ALT (afsk_tx_core.h)。送信中なら false。無線機はダミーロードにつなぐこと。
// 止まったあとは、PTTを切って受信に戻る (通常の送信と同じ手順)。送信回数には数えない。
bool    afsk_hw_calib_start(uint8_t mode);
// 受信音 (お知らせ音) をモニターのスピーカーに出す。level は受信音の音量 0〜MONITOR_LEVEL_MAX (0 なら鳴らさない。
// モニター音量とは別)。送信中・モニター出力なしなら false。鳴っている途中なら新しい音に替える。送信を始めると止まる。
//   PACKET 「ポッ」     825Hz 40ms                          パケットを受信
//   ECHO   「ポピッ」   943Hz 60ms → 1650Hz 80ms            自局のパケットがデジピーターで折り返してきた
//   ACK    「ピコーン」 1320Hz 80ms → 1650Hz 200ms          送ったメッセージに Ack が来た
//   MSG    「ピピピッ」 1100Hz 100ms ×3 (間 60ms)           メッセージ・一斉通知を受信
enum AfskChime { AFSK_CHIME_PACKET = 0, AFSK_CHIME_ECHO, AFSK_CHIME_ACK, AFSK_CHIME_MSG, AFSK_CHIME_COUNT };
bool    afsk_hw_chime(uint8_t kind, uint8_t level);
void    afsk_hw_calib_stop();             // 止める要求 (afsk_hw_poll が処理する)
bool    afsk_hw_calib_active();           // 調整トーンの送信中 (PTT待ち・終了処理を含む)
uint8_t afsk_hw_calib_mode();

// 送信の経過を知らせる出来事 (診断表示用)。loopから取り出す。たまったものを古い順に返す。
enum AfskHwEventType {
  AFSK_EV_PTT_ON = 1,        // a = 0
  AFSK_EV_AUDIO_START,       // a = PTTオンから音声を出し始めるまで (ms)
  AFSK_EV_AUDIO_END,         // a = 音声を出していた時間 (ms)
  AFSK_EV_PTT_OFF,           // a = PTTオンだった時間 (ms)
  AFSK_EV_PTT_TIMEOUT,       // a = PTTオンだった時間 (ms)。最大時間超過で中止
  AFSK_EV_CALIB_START,       // a = モード (AFSK_TONE_*)
  AFSK_EV_CALIB_STOP         // a = 0
};
struct AfskHwEvent { uint32_t ms; uint8_t type; uint32_t a; };
bool afsk_hw_pop_event(AfskHwEvent* out);

bool afsk_hw_tx_busy();   // 送信中 (PTTオン〜PTTオフまで)
bool afsk_hw_ptt_on();    // PTT出力がオンの間 true (PTT_PIN が無ければ常に false)
bool afsk_hw_busy();      // 送信中、または信号を受信中 (設定の書き込みなどを避ける目安)

// 受信したフレームを1件取り出す (古い順)。なければ false。
bool afsk_hw_pop_frame(QueuedPacket* out);

AfskHwStats afsk_hw_stats();

// 受信の状態を取り出す (loopから。割り込みの中では呼ばない)
void afsk_hw_rx_status(AfskHwRxStatus* out);

#endif // AFSK_HW_H
