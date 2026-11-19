// PCで ui_m5.cpp と .ino の文法を確かめるための、M5Unified / Arduino の最小の代用品 (動作はしない)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_stub.h"
#define SERIAL_8N1 0
enum textdatum_t { top_left = 0, top_center = 1 };
namespace fonts { struct IFont {}; extern IFont lgfxJapanGothic_16; extern IFont Font0; }
struct StubDisplay {
  void setRotation(int) {} void setBrightness(int) {} void setFont(const void*) {} void setTextSize(int) {} void setTextWrap(bool) {}
  void fillScreen(uint16_t) {} void setTextDatum(textdatum_t) {} void setTextPadding(int) {} void setTextColor(uint16_t, uint16_t) {}
  void drawString(const char*, int, int) {} void fillRect(int, int, int, int, uint16_t) {} void startWrite() {} void endWrite() {} void print(const char*) {} void println(const char*) {}
  void setTextColor(uint16_t) {} void drawRect(int, int, int, int, uint16_t) {} void drawLine(int, int, int, int, uint16_t) {} void drawPixel(int, int, uint16_t) {} void drawCircle(int, int, int, uint16_t) {} void setClipRect(int, int, int, int) {} void clearClipRect() {}
  void fillTriangle(int, int, int, int, int, int, uint16_t) {} void drawFastVLine(int, int, int, uint16_t) {} void drawFastHLine(int, int, int, uint16_t) {}
  void fillCircle(int, int, int, uint16_t) {} void drawArc(int32_t, int32_t, int32_t, int32_t, float, float, uint16_t) {} void drawRoundRect(int, int, int, int, int, uint16_t) {}
};
namespace m5 { struct Power_Class { enum is_charging_t { is_discharging = 0, is_charging, charge_unknown };
  int32_t getBatteryLevel() { return -1; } is_charging_t isCharging() { return charge_unknown; } }; }
struct StubBtn { bool isPressed() const { return false; } };
struct StubCfg { bool internal_spk = true, internal_mic = true; int serial_baudrate = 0; };
struct StubI2C { bool scanID(uint8_t, uint32_t = 100000) const { return false; } bool start(uint8_t, bool, uint32_t) const { return false; } bool read(uint8_t*, size_t, bool = false) const { return false; } bool stop() const { return true; } };
struct StubM5 { StubDisplay Display; m5::Power_Class Power; StubBtn BtnA, BtnB, BtnC; StubI2C In_I2C; static StubCfg config() { return StubCfg(); } void begin(const StubCfg&) {} void update() {} };
extern StubM5 M5;
struct StubSerial { void begin(unsigned long, int = 0, int = -1, int = -1) {} int available() { return 0; } int read() { return -1; } void printf(const char*, ...) {} void println(const char*) {} };
extern StubSerial Serial, Serial2;
struct StubEsp { unsigned long getFreeHeap() { return 123456; } const char* getSdkVersion() { return "v4.4-stub"; } unsigned long long getEfuseMac() { return 0x123456789ULL; } unsigned char getChipRevision() { return 3; } };
extern StubEsp ESP;
inline void delay(unsigned long) {}
