// afsk_tx_core.cpp
#include "afsk_tx_core.h"

#include <math.h>
#include <string.h>

bool afsk_tx_init(AfskTx* t, const AfskTxConfig* cfg) {
  if (!t || !cfg) return false;
  if (cfg->sampleRateHz == 0 || cfg->baud == 0 || cfg->markHz == 0 || cfg->spaceHz == 0) return false;
  if (cfg->ampMark > 127 || cfg->ampSpace > 127) return false;
  if (cfg->preambleFlags < 1) return false;
  // 1ビットに最低でも数サンプル必要(ナイキスト条件: 2200Hzの2倍より十分高いこと)
  if (cfg->sampleRateHz < 2u * (uint32_t)cfg->spaceHz + 1000u) return false;

  memset(t, 0, sizeof(*t));
  t->cfg = *cfg;
  for (int i = 0; i < 256; i++) {
    t->sine[i] = (int8_t)lround(127.0 * sin(2.0 * 3.14159265358979323846 * i / 256.0));
  }
  t->incMark  = (uint32_t)(((uint64_t)cfg->markHz  << 32) / cfg->sampleRateHz);
  t->incSpace = (uint32_t)(((uint64_t)cfg->spaceHz << 32) / cfg->sampleRateHz);
  t->active = false;
  return true;
}

// ---- ビット列の作成 -------------------------------------------------

struct BitWriter {
  AfskTx* t;
  uint8_t level;   // NRZIの現在のトーン (1=MARK, 0=SPACE)
  uint8_t ones;    // 連続する1の数(ビットスタッフィング用)
  bool    ok;
};

// NRZI: 0 のときトーンを反転、1 のときは保つ。結果のトーンを格納する。
static void put_raw(BitWriter* w, uint8_t bit) {
  if (!w->ok) return;
  if (w->t->nbits >= AFSK_TX_MAX_BITS) { w->ok = false; return; }
  if (bit == 0) w->level ^= 1;
  uint16_t i = w->t->nbits++;
  if (w->level) w->t->bits[i >> 3] |=  (uint8_t)(1u << (i & 7));
  else          w->t->bits[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

// フラグ 0x7E は、スタッフィングなしで下位ビットから送る
static void put_flag(BitWriter* w) {
  for (int b = 0; b < 8; b++) put_raw(w, (uint8_t)((0x7E >> b) & 1));
  w->ones = 0;
}

// データのビット: 1が5つ続いたら、0を1つ挿入する
static void put_data_bit(BitWriter* w, uint8_t bit) {
  put_raw(w, bit);
  if (bit) {
    if (++w->ones == 5) { put_raw(w, 0); w->ones = 0; }
  } else {
    w->ones = 0;
  }
}

bool afsk_tx_prepare(AfskTx* t, const uint8_t* frame, size_t len) {
  if (!t || !frame || len == 0 || t->active) return false;
  t->nbits = 0;
  memset(t->bits, 0, sizeof(t->bits));

  BitWriter w;
  w.t = t; w.level = 1; w.ones = 0; w.ok = true;     // 最初のトーンはMARK

  for (uint16_t i = 0; i < t->cfg.preambleFlags; i++) put_flag(&w);
  for (size_t i = 0; i < len; i++) {
    for (int b = 0; b < 8; b++) put_data_bit(&w, (uint8_t)((frame[i] >> b) & 1));   // 下位ビットから
  }
  put_flag(&w);                                       // 終わりのフラグ
  for (uint16_t i = 0; i < t->cfg.tailFlags; i++) put_flag(&w);

  if (!w.ok) { t->nbits = 0; return false; }
  return true;
}

// ---- 再生 ------------------------------------------------------------

void afsk_tx_start(AfskTx* t) {
  if (!t || t->nbits == 0) return;
  t->bitIndex = 0;
  t->bitAcc = 0;
  t->phase = 0;
  t->toneMode = 0;
  t->active = true;      // 状態をそろえてから、最後にオンにする
}

void afsk_tx_start_tone(AfskTx* t, uint8_t mode) {
  if (!t || t->active || mode < AFSK_TONE_MARK || mode > AFSK_TONE_ALT) return;
  t->nbits = 0;
  t->bitIndex = 0; t->bitAcc = 0; t->phase = 0;
  t->toneMark = true;
  t->toneLeft = t->cfg.sampleRateHz / 2;
  t->toneMode = mode;
  t->active = true;
}

bool IRAM_ATTR afsk_tx_next_sample(AfskTx* t, uint8_t* out) {
  if (!t->active) { *out = 128; return false; }

  if (t->toneMode) {                                   // 連続トーン: 割り算を使わず、カウントダウンで切り替える
    const bool mk = (t->toneMode == AFSK_TONE_MARK) || (t->toneMode == AFSK_TONE_ALT && t->toneMark);
    const int a = mk ? t->cfg.ampMark : t->cfg.ampSpace;
    *out = (uint8_t)(128 + (((int)t->sine[t->phase >> 24] * a) >> 7));
    t->phase += mk ? t->incMark : t->incSpace;
    if (t->toneMode == AFSK_TONE_ALT && t->toneLeft && --t->toneLeft == 0) {
      t->toneMark = !t->toneMark;
      t->toneLeft = t->cfg.sampleRateHz / 2;
    }
    return true;
  }

  const uint16_t i = t->bitIndex;
  const bool mark = (t->bits[i >> 3] >> (i & 7)) & 1;
  const uint32_t inc = mark ? t->incMark : t->incSpace;
  const int amp = mark ? t->cfg.ampMark : t->cfg.ampSpace;

  const int s = ((int)t->sine[t->phase >> 24] * amp) >> 7;
  *out = (uint8_t)(128 + s);

  t->phase += inc;                      // 位相はトーンが変わっても連続する
  t->bitAcc += t->cfg.baud;
  if (t->bitAcc >= t->cfg.sampleRateHz) {
    t->bitAcc -= t->cfg.sampleRateHz;   // サンプリングレートがボーレートの整数倍でなくても、ずれが溜まらない
    if (++t->bitIndex >= t->nbits) t->active = false;
  }
  return true;
}

bool afsk_tx_is_active(const AfskTx* t) { return t && t->active; }

void afsk_tx_abort(AfskTx* t) { if (t) { t->active = false; t->nbits = 0; t->toneMode = 0; } }
