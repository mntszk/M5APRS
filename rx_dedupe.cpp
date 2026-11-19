// rx_dedupe.cpp
#include "rx_dedupe.h"
#include <string.h>

void rx_dedupe_init(RxDedupe* d) { memset(d, 0, sizeof(*d)); }

static uint32_t fnv(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }

bool rx_dedupe_check(RxDedupe* d, const uint8_t* f, size_t len, uint32_t nowMs) {
  if (!d || !f || len < 16) return false;
  // アドレス部の長さ (最後のアドレスの最下位ビットが1)
  size_t n = 0; bool last = false;
  while (!last) {
    if (n >= 10 || (n + 1) * 7 > len) return false;
    last = (f[n * 7 + 6] & 1) != 0;
    n++;
  }
  if (n < 2 || len < n * 7 + 2) return false;
  // 宛先と送信元: コールサイン6文字とSSIDだけ (経路で変わるビットは見ない)。続けて制御・PID・情報欄
  uint32_t h = 2166136261u;
  for (int a = 0; a < 2; a++) {
    for (int i = 0; i < 6; i++) h = fnv(h, f[a * 7 + i]);
    h = fnv(h, (uint8_t)((f[a * 7 + 6] >> 1) & 0x0F));
  }
  size_t start = n * 7;
  for (size_t i = start; i < len; i++) h = fnv(h, f[i]);
  uint16_t l = (uint16_t)(len - start);

  for (int i = 0; i < RX_DUPE_SLOTS; i++) {
    RxDupeEntry* e = &d->e[i];
    if (e->used && e->hash == h && e->len == l && (uint32_t)(nowMs - e->ms) < (uint32_t)RX_DUPE_MS) { d->dupes++; return true; }
  }
  RxDupeEntry* e = &d->e[d->next];
  e->used = true; e->hash = h; e->len = l; e->ms = nowMs;
  d->next = (uint8_t)((d->next + 1) % RX_DUPE_SLOTS);
  return false;
}
