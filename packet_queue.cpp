// packet_queue.cpp
#include "packet_queue.h"

#include <string.h>

void pq_init(PacketQueue* q, uint8_t capacity) {
  memset(q, 0, sizeof(*q));
  if (capacity < 1) capacity = 1;
  if (capacity > PACKET_QUEUE_MAX) capacity = PACKET_QUEUE_MAX;
  q->capacity = capacity;
}

bool pq_push(PacketQueue* q, const uint8_t* data, size_t len, uint32_t rxMs) {
  if (!data || len == 0 || len > AFSK_RX_FRAME_MAX) return false;
  if (q->count >= q->capacity) { q->dropped++; return false; }
  QueuedPacket* s = &q->slot[(q->head + q->count) % q->capacity];
  s->len = (uint16_t)len; s->rxMs = rxMs;
  memcpy(s->data, data, len);
  q->count++;
  return true;
}

bool pq_pop(PacketQueue* q, QueuedPacket* out) {
  if (q->count == 0) return false;
  if (out) *out = q->slot[q->head];
  q->head = (uint8_t)((q->head + 1) % q->capacity);
  q->count--;
  return true;
}

uint8_t pq_count(const PacketQueue* q) { return q->count; }
