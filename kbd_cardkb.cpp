// kbd_cardkb.cpp
#include "kbd_cardkb.h"
#include <M5Unified.h>

// M5Stack Basic では、ポートAは本体内部のI2C (GPIO21/22) と同じ線。M5Unified が初期化した M5.In_I2C を使う
static uint32_t s_lastPoll = 0;

void kbd_begin() { s_lastPoll = 0; }

bool kbd_poll(uint32_t nowMs, char* c) {
  if ((uint32_t)(nowMs - s_lastPoll) < (uint32_t)CARDKB_POLL_MS) return false;
  s_lastPoll = nowMs;
  uint8_t v = 0;
  bool ok = M5.In_I2C.start((uint8_t)CARDKB_I2C_ADDR, true, CARDKB_I2C_FREQ);
  if (ok) ok = M5.In_I2C.read(&v, 1, true);
  M5.In_I2C.stop();
  if (!ok || v == 0) return false;               // 読めない (つながっていない・一時的な失敗) / キーなし: 次の回にまた読む
  *c = (char)v;
  return true;
}
