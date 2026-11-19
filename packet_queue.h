// packet_queue.h
// 受信したフレームの待ち行列。復調側(loop)が入れて、画面・ログ側(loop)が取り出す。
// 満杯のときは新しいフレームを破棄して件数を記録する (仕様: RX_PACKET_QUEUE_COUNT)。
// M5Stack/Arduinoの部品に依存しない。

#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include "afsk_rx_core.h"      // AFSK_RX_FRAME_MAX

#ifndef PACKET_QUEUE_MAX
#define PACKET_QUEUE_MAX 16    // 配列の大きさの上限。実際の件数は pq_init の引数
#endif

struct QueuedPacket {
  uint16_t len;
  uint32_t rxMs;                    // 受信完了時刻 (millis)
  uint8_t  data[AFSK_RX_FRAME_MAX]; // FCSなしのフレーム
};

struct PacketQueue {
  QueuedPacket slot[PACKET_QUEUE_MAX];
  uint8_t  capacity;
  uint8_t  head, count;
  uint32_t dropped;                 // 満杯で破棄した件数
};

void pq_init(PacketQueue* q, uint8_t capacity);
// 追加。満杯なら破棄して false。長さが 0 または大きすぎるときも false (破棄の件数には入れない)
bool pq_push(PacketQueue* q, const uint8_t* data, size_t len, uint32_t rxMs);
// 最も古いものを取り出す。out の中身をコピー。なければ false
bool pq_pop(PacketQueue* q, QueuedPacket* out);
uint8_t pq_count(const PacketQueue* q);

#endif // PACKET_QUEUE_H
