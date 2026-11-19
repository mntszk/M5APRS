// diag.h
// シリアルモニタ (PC) への診断出力の文字列を作る。実際の出力先は out に渡す関数 (Serial.println など)。
// M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
//
// 出る内容 (115200bps。ASCIIのみ。時刻は起動からの秒):
//   [   12.345] PTT ON                       PTT・送信の経過 (afsk_hw のイベント)
//   [   12.646] TX audio start (+301ms after PTT)
//   [   13.751] TX audio end (1105ms)
//   [   13.802] PTT OFF (on 1457ms)
//   [   13.802] TX pkt: NOCALL-9>APM5AP,WIDE1-1:!....
//   [   20.100] RX signal  peak=1204 mark=380 space=402 dc=-12     受信の状態の変化 (ノイズで増えすぎないよう間引く)
//   [   20.300] RX data    flags=3
//   [   21.500] RX ok
//   [   21.501] RX pkt: JA...>APRS:...
//   [   30.000] STAT up=30s gps=FIX sat=9 src=GPS rx_ok=1 fcs=0 ...    一定間隔の状態 (受信中は見送る)
#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>
#include <stddef.h>
#include "afsk_hw.h"

#ifndef DIAG_SERIAL
#define DIAG_SERIAL           1        // 1: シリアルへ診断を出す。0: 出さない
#endif
#ifndef DIAG_STATUS_INTERVAL_MS
#define DIAG_STATUS_INTERVAL_MS  5000  // 状態の行を出す間隔 (ms)。0 なら出さない
#endif
#define DIAG_RX_MIN_INTERVAL_MS  100   // 受信の状態変化の行を出す最短間隔。これより速い変化は間引く
#define DIAG_LINE_MAX            320

typedef void (*DiagOut)(const char* line);

struct DiagState {
  DiagOut  out;
  bool     enabled;
  bool     phaseInit;
  AfskRxPhase lastPhase;
  uint32_t lastPhaseLineMs;
  uint32_t skipped;               // 間引いた行の数
  uint32_t lastResultSeq;
  uint32_t lastStatusMs;
  uint32_t lastStatusDue;         // 受信中で見送った回数
};

// 状態の行に出す内容
struct DiagStatus {
  uint32_t uptimeMs;
  int      gps;                   // GpsStatus の値 (0=NO_DATA 1=NO_FIX 2=STALE 3=FIX)
  uint8_t  satellites;
  uint8_t  posSource;             // POSITION_SOURCE_*
  bool     posValid;
  bool     sdOk;
  uint32_t sdPendingBytes;
  uint32_t freeHeap;
  AfskHwRxStatus rx;
  AfskHwStats    st;
};

void diag_init(DiagState* d, DiagOut out, bool enabled);
void diag_boot(DiagState* d, bool hwOk, uint32_t nowMs);                      // 起動時の設定の要約
void diag_hw_event(DiagState* d, const AfskHwEvent* e);                       // afsk_hw_pop_event で取り出したもの
void diag_rx_update(DiagState* d, const AfskHwRxStatus* rx, uint32_t nowMs);  // 受信の状態の変化 (毎回のloopで呼んでよい)
void diag_tx_packet(DiagState* d, const uint8_t* frameWithFcs, size_t len, uint32_t nowMs);
void diag_rx_packet(DiagState* d, const uint8_t* frame, size_t len, uint32_t nowMs);   // FCSなし
// 一定間隔で状態を出す。busy (送信中・受信中) のあいだは見送る (ただし2回分たまったら出す)。
// 1行の短い知らせ (自動送信など)。ASCIIで渡す
void diag_note(DiagState* d, uint32_t nowMs, const char* text);
void diag_status(DiagState* d, const DiagStatus* s, bool busy, uint32_t nowMs);

#endif
