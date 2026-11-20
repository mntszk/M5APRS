// board_check.h
// ボードパッケージ (ESP32 / M5Stack) の版を確かめる。M5APRS は 2.x (動作確認 2.1.4) 用。
// 3.x はタイマー・ADC・DAC の使い方が変わったので、分かりやすいメッセージを出してビルドを止める。
#ifndef BOARD_CHECK_H
#define BOARD_CHECK_H
#include <Arduino.h>
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#error "M5APRS: board package (ESP32 / M5Stack) 3.x is not supported. In Tools > Board > Boards Manager, install version 2.x (2.1.4) of M5Stack. / ボードパッケージ 3.x は未対応です。ボードマネージャで M5Stack の 2.x (2.1.4) を入れてください"
#include "M5APRS_needs_board_package_2x.h"   // ここで止める (存在しないファイル。ほかの分かりにくいエラーを出さないため)
#endif
#endif
