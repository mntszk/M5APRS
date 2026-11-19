// msg_core.h
// APRSメッセージの送受信の管理 (仕様 4.8)。M5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
//
//   - 受信: 自分のコールサイン宛て (SSIDが違っても表示)、一斉通知 (BLN…) を保存する
//           SSIDまで一致し番号付きなら、Ackを返す待ち行列に入れる (送るかどうかは自動送信の設定で決まる)
//           同じ局・同じ番号の送り直しは1件にまとめ、Ackだけ返し直す
//   - 送信: 番号を付けて送り、Ackが来るまで MSG_RETRY_INTERVALS_S の間隔で送り直す。来なければ「応答なし」
//   - 送る時刻に他局が送信中なら、止むまで待つ。MSG_BUSY_WAIT_MS 待っても止まなければ、その回は見送る
//
// 使い方 (loop から):
//   msg_on_packet(&m, &pkt, myCall, now, &t);                      // 受信したパケットを渡す
//   msg_send(&m, "NOCALL-7", "Hello", now, &t);                    // 送るメッセージを登録
//   MsgTx tx; if (msg_poll(&m, now, busy, canTx, ackOn, &tx) == MSGP_SEND) { tx.info を送信 }

#ifndef MSG_CORE_H
#define MSG_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "app_config.h"
#include "packet_parse.h"
#include "logger.h"          // LogTime

#ifndef MSG_STORE_COUNT
#define MSG_STORE_COUNT        20          // 覚えておくメッセージの数 (受信・送信・一斉通知の合計)
#endif
#ifndef MSG_RETRY_INTERVALS_S
#define MSG_RETRY_INTERVALS_S  { 30, 60, 120, 240, 480 }   // 送り直しの間隔 (秒)。個数が送り直しの回数
#define MSG_RETRY_COUNT        5
#endif
#ifndef MSG_FINAL_WAIT_MS
#define MSG_FINAL_WAIT_MS      60000       // 最後に送ってから、これだけAckを待って「応答なし」にする
#endif
#ifndef MSG_BUSY_WAIT_MS
#define MSG_BUSY_WAIT_MS       30000       // 他局の送信が止むのを待つ最長時間
#endif

#define MSG_CALL_MAX   10
#define MSG_ACK_QUEUE  4

enum MsgDir   { MSG_DIR_IN = 0, MSG_DIR_OUT, MSG_DIR_BLN };
enum MsgState { MSG_ST_NONE = 0, MSG_ST_SENDING, MSG_ST_ACKED, MSG_ST_REJECTED, MSG_ST_FAILED };

struct MsgEntry {
  bool     used;
  uint8_t  dir;                       // MsgDir
  char     from[MSG_CALL_MAX];
  char     to[MSG_CALL_MAX];          // 宛先 (一斉通知は "BLN1" など)
  char     text[APRS_MSG_TEXT_MAX + 1];
  char     msgNo[6];
  char     time[9];                   // "HH:MM:SS" (UTC)。不明なら "--:--:--"
  uint32_t ms;                        // 受信/登録した時刻 (millis)。restored のときは使わない
  uint32_t utc;                       // 受信/登録した時刻 (UTC、1970年からの秒)。不明なら 0
  bool     restored;                  // 再起動の前に受信/登録した (SDから読み込んだ)
  uint32_t seq;                       // 登録順の番号 (並べ替えと選択に使う。0は使わない)
  bool     unread;                    // 自分宛ての未読
  bool     exact;                     // 受信: SSIDまで自分と一致
  uint8_t  state;                     // 送信: MsgState
  uint8_t  sends;                     // 送信: 送った回数 (見送りも数える)
  uint32_t nextMs;                    // 送信: 次に送る時刻 / 「応答なし」にする時刻
  uint8_t  repeats;                   // 受信: 送り直しを受けた回数
};

struct MsgAck { bool used; char to[MSG_CALL_MAX]; char no[6]; };

struct MsgCore {
  MsgEntry e[MSG_STORE_COUNT];
  MsgAck   acks[MSG_ACK_QUEUE];
  uint32_t seq;
  uint16_t nextNo;                    // 次のメッセージ番号 (1〜999)
  bool     waiting;  uint32_t waitSinceMs;
  uint32_t acksSent, acksDropped, sent, failed;
};

enum MsgRxResult {
  MSG_RX_NONE = 0,       // メッセージではない / 関係ない
  MSG_RX_NEW,            // 自分宛ての新しいメッセージ (未読)
  MSG_RX_REPEAT,         // 受信済みのメッセージの送り直し (Ackだけ返し直す)
  MSG_RX_BULLETIN,       // 新しい一斉通知
  MSG_RX_ACKED,          // 送ったメッセージにAckが来た
  MSG_RX_REJECTED        // 送ったメッセージがRejされた
};

enum MsgPoll {
  MSGP_IDLE = 0,
  MSGP_SEND,             // out を送る
  MSGP_SKIP_BUSY,        // 他局の送信が続いたので、この回は見送った
  MSGP_FAILED            // 送ったメッセージが「応答なし」になった (out->entry)
};

struct MsgTx {
  bool  isAck;
  int   entry;                     // メッセージのとき、その添字 (Ackのときは -1)
  char  to[MSG_CALL_MAX];
  char  info[APRS_MSG_TEXT_MAX + 24];   // 情報フィールド ":ADDRESSEE:text{NN" / ":ADDRESSEE:ackNN"
};

void msg_init(MsgCore* m, uint16_t firstNo);
// 送受信したメッセージをすべて消す (送り直し中のものもやめる)。メッセージ番号は続きから。返す予定の Ack は残す
void msg_clear(MsgCore* m);

// コールサインの比較 (大文字小文字を区別しない)。base_equal は SSID を無視する
bool msg_call_equal(const char* a, const char* b);
bool msg_call_base_equal(const char* a, const char* b);

// 受信したパケットを処理する。myCall は今の自局 (SSID付き。例 "NOCALL-9")
MsgRxResult msg_on_packet(MsgCore* m, const AprsPacket* p, const char* myCall, uint32_t nowMs, const LogTime* t);

// メッセージを送る準備をする (すぐには送らない。msg_poll が送る)。戻り値: 添字。宛先・本文が不正なら -1
// 宛先は大文字にし、本文は表示できるASCIIだけにする ('|' '~' '{' は使えないので '?' にする)
int msg_send(MsgCore* m, const char* to, const char* text, uint32_t nowMs, const LogTime* t);

// 送るものがあるか調べる。channelBusy=他局の信号あり、canTx=自局が送れる、ackEnabled=Ackを返す設定
MsgPoll msg_poll(MsgCore* m, uint32_t nowMs, bool channelBusy, bool canTx, bool ackEnabled, MsgTx* out);

// 新しい順に並べた添字。戻り値: 件数
int  msg_order(const MsgCore* m, uint8_t out[MSG_STORE_COUNT]);
int  msg_find_seq(const MsgCore* m, uint32_t seq);   // 見つからなければ -1
int  msg_unread(const MsgCore* m);
void msg_mark_read(MsgCore* m, int idx);
bool msg_pending(const MsgCore* m);                    // Ackの返送や送信中のメッセージがある
// SDから読み込んだあとの後始末: 送り直しの途中だったものは「応答なし」、millisの時刻は使わない印を付ける
void msg_after_restore(MsgCore* m, uint32_t nowMs);

// メッセージの情報フィールドを作る。戻り値: 長さ
size_t msg_build_info(char* out, size_t cap, const char* to, const char* text, const char* no);

#endif
