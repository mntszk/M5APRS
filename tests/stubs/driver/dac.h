#pragma once
#include "../esp_stub.h"
// チャネルごとに記録する。主チャネル (g_stub.mainCh) は従来のフィールドにも写す。
inline esp_err_t dac_output_enable(dac_channel_t ch) {
  StubHw::Ch& c = g_stub.ch[ch]; c.enabled = true; c.enableCalls++;
  if (ch == g_stub.mainCh) { g_stub.dacEnabled = true; g_stub.dacEnableCalls++; g_stub.dacCh = ch; }
  return ESP_OK; }
inline esp_err_t dac_output_disable(dac_channel_t ch) {
  StubHw::Ch& c = g_stub.ch[ch]; c.enabled = false; c.disableCalls++;
  if (ch == g_stub.mainCh) { g_stub.dacEnabled = false; g_stub.dacDisableCalls++; }
  return ESP_OK; }
inline esp_err_t dac_output_voltage(dac_channel_t ch, uint8_t v) {
  StubHw::Ch& c = g_stub.ch[ch]; c.last = v; if (g_stub.logDac && c.enabled) c.log.push_back(v);
  if (ch == g_stub.mainCh) { g_stub.dacLast = v; if (g_stub.logDac && g_stub.dacEnabled) g_stub.dacLog.push_back(v); }
  return ESP_OK; }
