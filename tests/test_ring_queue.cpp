// test_ring_queue.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "afsk_ring.h"
#include "packet_queue.h"
#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
  test_cfg_setup();
  // ===== リングバッファ =====
  { int16_t buf[8]; SampleRing r; ring_init(&r, buf, 8); int16_t v;
    CHECK(!ring_pop(&r, &v) && ring_count(&r) == 0);
    CHECK(ring_capacity(&r) == 7);
    for (int i = 0; i < 7; i++) CHECK(ring_push(&r, (int16_t)(i * 100 - 300)));
    CHECK(ring_count(&r) == 7);
    CHECK(!ring_push(&r, 999) && !ring_push(&r, 999) && r.overruns == 2);       // 満杯: 新しい方を捨てる
    for (int i = 0; i < 7; i++) { CHECK(ring_pop(&r, &v) && v == i * 100 - 300); }   // 古い順・負の値も保つ
    CHECK(!ring_pop(&r, &v));
    for (int i = 0; i < 20; i++) { ring_push(&r, (int16_t)i); CHECK(ring_pop(&r, &v) && v == i); }   // 何周もする
    ring_push(&r, 1); ring_push(&r, 2); ring_flush(&r); CHECK(ring_count(&r) == 0 && !ring_pop(&r, &v));
    ring_push(&r, 7); CHECK(ring_pop(&r, &v) && v == 7); }
  // 容量が2のべき乗でなくても、何周しても順序と個数が保たれる
  { int16_t buf[5]; SampleRing r; ring_init(&r, buf, 5); int16_t v; int pushed = 0, popped = 0;
    for (int i = 0; i < 100000; i++) {
      if (i % 3 != 2) { if (ring_push(&r, (int16_t)(i & 0x7FFF))) pushed++; }
      if (i % 2 == 0) { int16_t w; if (ring_pop(&r, &w)) popped++; }
    }
    while (ring_pop(&r, &v)) popped++;
    CHECK(pushed == popped); CHECK(ring_count(&r) == 0);
    int16_t exp = 0; bool order = true;
    for (int i = 0; i < 50000; i++) { ring_push(&r, exp); ring_push(&r, (int16_t)(exp + 1)); int16_t a1, a2; ring_pop(&r, &a1); ring_pop(&r, &a2); if (a1 != exp || a2 != exp + 1) order = false; exp = (int16_t)((exp + 2) & 0x3FFF); }
    CHECK(order); }
  // 2スレッド: 順序が保たれ、捨てた分と合わせて全数が合う
  { static int16_t buf[1321]; SampleRing r; ring_init(&r, buf, 1321);
    const int N = 2000000;
    std::atomic<bool> done(false);
    std::vector<int16_t> got; got.reserve(N);
    std::thread consumer([&]() {
      int16_t v;
      while (true) {
        if (ring_pop(&r, &v)) got.push_back(v);
        else if (done.load()) { if (!ring_pop(&r, &v)) break; got.push_back(v); }
      }
    });
    std::vector<int16_t> sent; sent.reserve(N);
    for (int i = 0; i < N; i++) { int16_t v = (int16_t)(i & 0x7FFF); if (ring_push(&r, v)) sent.push_back(v); if ((i & 1023) == 0) std::this_thread::yield(); }
    done = true; consumer.join();
    CHECK(got.size() == sent.size());
    CHECK(got == sent);
    CHECK(sent.size() + r.overruns == (size_t)N);
    printf("thread test: sent %zu, overruns %u\n", sent.size(), (unsigned)r.overruns); }

  // ===== 待ち行列 =====
  { static PacketQueue q; pq_init(&q, 4); QueuedPacket p; uint8_t d[32];
    CHECK(!pq_pop(&q, &p) && pq_count(&q) == 0);
    for (int i = 0; i < 4; i++) { memset(d, i + 1, sizeof(d)); CHECK(pq_push(&q, d, 10 + i, 1000 + i)); }
    memset(d, 99, sizeof(d));
    CHECK(!pq_push(&q, d, 10, 5000) && !pq_push(&q, d, 10, 5001) && q.dropped == 2 && pq_count(&q) == 4);   // 満杯: 新しい方を捨てて数える
    for (int i = 0; i < 4; i++) { CHECK(pq_pop(&q, &p) && p.len == 10 + i && p.rxMs == 1000u + i && p.data[0] == i + 1); }
    CHECK(!pq_pop(&q, &p));
    for (int round = 0; round < 50; round++) {                       // 何周もしても順序が保たれる
      for (int i = 0; i < 3; i++) { memset(d, round + i, sizeof(d)); pq_push(&q, d, 8, round * 10 + i); }
      for (int i = 0; i < 3; i++) { CHECK(pq_pop(&q, &p) && p.rxMs == (uint32_t)(round * 10 + i) && p.data[7] == (uint8_t)(round + i)); }
    }
    CHECK(q.dropped == 2);
    CHECK(!pq_push(&q, NULL, 5, 0) && !pq_push(&q, d, 0, 0) && !pq_push(&q, d, AFSK_RX_FRAME_MAX + 1, 0) && q.dropped == 2);
    uint8_t big[AFSK_RX_FRAME_MAX]; memset(big, 0xAB, sizeof(big));
    CHECK(pq_push(&q, big, AFSK_RX_FRAME_MAX, 1) && pq_pop(&q, &p) && p.len == AFSK_RX_FRAME_MAX && p.data[AFSK_RX_FRAME_MAX - 1] == 0xAB); }
  { static PacketQueue q; pq_init(&q, 0); CHECK(q.capacity == 1);
    pq_init(&q, 200); CHECK(q.capacity == PACKET_QUEUE_MAX); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
