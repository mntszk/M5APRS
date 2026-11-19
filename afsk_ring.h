// afsk_ring.h
// ADCのサンプルを、割り込み(書く側1つ)とloop(読む側1つ)の間で受け渡すリングバッファ。
// ロックなしで安全に動く(書く側と読む側がそれぞれ1つの場合に限る)。
// 割り込みから呼ぶ ring_push は、短い処理だけ。満杯のときは新しいサンプルを捨てて、数える。
// M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。

#ifndef AFSK_RING_H
#define AFSK_RING_H

#include <stdint.h>
#include <stddef.h>

#ifndef IRAM_ATTR
#if defined(ARDUINO_ARCH_ESP32)
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif
#endif

struct SampleRing {
  int16_t*          buf;        // 呼び出し側が用意した配列
  uint32_t          cap;        // 配列の要素数 (入れられるのは cap-1 個。満杯と空を見分けるため1つ空ける)
  volatile uint32_t head;       // 次に書く位置 (0〜cap-1。書く側だけが進める)
  volatile uint32_t tail;       // 次に読む位置 (0〜cap-1。読む側だけが進める)
  volatile uint32_t overruns;   // 満杯で捨てたサンプル数
};

static inline void ring_init(SampleRing* r, int16_t* buf, uint32_t cap) {
  r->buf = buf; r->cap = cap; r->head = 0; r->tail = 0; r->overruns = 0;
}

// 入れられる最大数
static inline uint32_t ring_capacity(const SampleRing* r) { return r->cap - 1; }

// 割り込み側。入ったら true、満杯で捨てたら false
static inline bool IRAM_ATTR ring_push(SampleRing* r, int16_t v) {
  uint32_t h = r->head;
  uint32_t next = h + 1; if (next >= r->cap) next = 0;
  if (next == __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE)) { r->overruns++; return false; }
  r->buf[h] = v;
  __atomic_store_n(&r->head, next, __ATOMIC_RELEASE);
  return true;
}

// loop側。取れたら true
static inline bool ring_pop(SampleRing* r, int16_t* v) {
  uint32_t t = r->tail;
  if (t == __atomic_load_n(&r->head, __ATOMIC_ACQUIRE)) return false;
  *v = r->buf[t];
  uint32_t next = t + 1; if (next >= r->cap) next = 0;
  __atomic_store_n(&r->tail, next, __ATOMIC_RELEASE);
  return true;
}

static inline uint32_t ring_count(const SampleRing* r) {
  uint32_t h = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE), t = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
  return h >= t ? h - t : h + r->cap - t;
}

// loop側から、溜まっているサンプルを読み捨てる (送信から受信に戻るときなど)
static inline void ring_flush(SampleRing* r) {
  __atomic_store_n(&r->tail, __atomic_load_n(&r->head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

#endif // AFSK_RING_H
