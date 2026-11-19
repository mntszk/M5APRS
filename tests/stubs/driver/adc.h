#pragma once
#include "../esp_stub.h"
inline esp_err_t adc1_config_width(adc_bits_width_t w) { g_stub.adcWidth = (int)w; return ESP_OK; }
inline esp_err_t adc1_config_channel_atten(adc1_channel_t ch, adc_atten_t a) { g_stub.adcCh = ch; g_stub.adcAtten = (int)a; return ESP_OK; }
inline int adc1_get_raw(adc1_channel_t) { g_stub.adcReads++; return g_stub.adcSource ? g_stub.adcSource() : 2048; }
