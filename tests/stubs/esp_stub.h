// PCでafsk_hw.cppをコンパイル・動作確認するための、ESP32 Arduinoコア 2.x / ESP-IDF 4.4 の代用品。
// 実機のタイマー割り込みは、テストが stub_fire_timer() で手動で呼ぶ。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>

#define IRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0

struct hw_timer_t {
  int num = 0; uint16_t divider = 0; bool countUp = false;
  void (*fn)(void) = 0; uint64_t alarm = 0; bool autoreload = false; bool enabled = false;
  int enableCalls = 0; int disableCalls = 0;
};

typedef enum { DAC_CHANNEL_1 = 0, DAC_CHANNEL_2 = 1 } dac_channel_t;
typedef enum { ADC1_CHANNEL_0 = 0, ADC1_CHANNEL_1, ADC1_CHANNEL_2, ADC1_CHANNEL_3, ADC1_CHANNEL_4, ADC1_CHANNEL_5, ADC1_CHANNEL_6, ADC1_CHANNEL_7 } adc1_channel_t;
typedef enum { ADC_ATTEN_DB_0 = 0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6, ADC_ATTEN_DB_11 } adc_atten_t;
typedef enum { ADC_WIDTH_BIT_9 = 0, ADC_WIDTH_BIT_10, ADC_WIDTH_BIT_11, ADC_WIDTH_BIT_12 } adc_bits_width_t;

struct StubHw {
  hw_timer_t timers[4];
  // 主チャネル (AFSK_TX_DAC_PIN) は従来の名前、チャネルごとの値は ch[] に記録する
  bool dacEnabled = false; uint8_t dacLast = 0; int dacEnableCalls = 0, dacDisableCalls = 0;
  std::vector<uint8_t> dacLog; bool logDac = false;
  dac_channel_t mainCh = DAC_CHANNEL_2;
  struct Ch { bool enabled = false; uint8_t last = 0; int enableCalls = 0, disableCalls = 0; std::vector<uint8_t> log; };
  Ch ch[2];
  int (*adcSource)(void) = 0; int adcReads = 0;
  int adcWidth = -1, adcAtten = -1; adc1_channel_t adcCh = ADC1_CHANNEL_0; dac_channel_t dacCh = DAC_CHANNEL_1;
  unsigned long nowMs = 0;
  // GPIO (PTT用)。書き込みの履歴を残す
  int gpioMode[40]; int gpioLevel[40];
  struct GpioEv { int pin; int level; unsigned long ms; bool dacEn; bool txTimerEn; };
  std::vector<GpioEv> gpioLog;
  StubHw() { for (int i = 0; i < 40; i++) { gpioMode[i] = -1; gpioLevel[i] = -1; } }
};
extern StubHw g_stub;

// n回、タイマー割り込みを手動で起こす (有効なときだけ)
inline int stub_fire_timer(int num, int n) {
  int fired = 0;
  for (int i = 0; i < n; i++) { hw_timer_t& t = g_stub.timers[num]; if (!t.enabled || !t.fn) break; t.fn(); fired++; }
  return fired;
}

inline unsigned long millis() { return g_stub.nowMs; }
inline unsigned long micros() { return g_stub.nowMs * 1000UL; }
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
inline void pinMode(int pin, int mode) { if (pin >= 0 && pin < 40) g_stub.gpioMode[pin] = mode; }
inline void digitalWrite(int pin, int level) {
  if (pin < 0 || pin >= 40) return;
  g_stub.gpioLevel[pin] = level;
  g_stub.gpioLog.push_back({pin, level, g_stub.nowMs, g_stub.ch[g_stub.mainCh].enabled, g_stub.timers[3].enabled});
}
inline hw_timer_t* timerBegin(uint8_t num, uint16_t divider, bool countUp) { hw_timer_t& t = g_stub.timers[num]; t = hw_timer_t(); t.num = num; t.divider = divider; t.countUp = countUp; return &t; }
inline void timerAttachInterrupt(hw_timer_t* t, void (*fn)(void), bool) { t->fn = fn; }
inline void timerAlarmWrite(hw_timer_t* t, uint64_t v, bool ar) { t->alarm = v; t->autoreload = ar; }
inline void timerAlarmEnable(hw_timer_t* t) { t->enabled = true; t->enableCalls++; }
inline void timerAlarmDisable(hw_timer_t* t) { t->enabled = false; t->disableCalls++; }
inline void timerWrite(hw_timer_t*, uint64_t) {}
