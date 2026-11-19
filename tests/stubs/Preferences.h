// PCでのコンパイル確認用の代用品 (ESP32 Arduinoコア 2.x の Preferences と同じ呼び出し方)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
class Preferences {
public:
  static std::map<std::string, std::map<std::string, uint8_t> > store;
  static std::map<std::string, std::map<std::string, std::vector<uint8_t> > > blobs;
  static bool failBegin, failPut; static int putCalls;
  std::string ns; bool opened = false;
  bool begin(const char* name, bool readOnly = false) { (void)readOnly; if (failBegin) return false; ns = name; opened = true; return true; }
  void end() { opened = false; }
  bool isKey(const char* key) { return opened && (store[ns].count(key) > 0 || blobs[ns].count(key) > 0); }
  uint8_t getUChar(const char* key, uint8_t def = 0) { return isKey(key) ? store[ns][key] : def; }
  size_t getBytesLength(const char* key) { return (opened && blobs[ns].count(key)) ? blobs[ns][key].size() : 0; }
  size_t getBytes(const char* key, void* buf, size_t maxLen) {
    if (!opened || !blobs[ns].count(key)) { return 0; }
    std::vector<uint8_t>& v = blobs[ns][key];
    size_t n = v.size() < maxLen ? v.size() : maxLen; memcpy(buf, v.data(), n); return n; }
  size_t putBytes(const char* key, const void* buf, size_t len) {
    putCalls++; if (!opened || failPut) return 0; blobs[ns][key] = std::vector<uint8_t>((const uint8_t*)buf, (const uint8_t*)buf + len); return len; }
  size_t putUChar(const char* key, uint8_t v) { putCalls++; if (!opened || failPut) return 0; store[ns][key] = v; return 1; }
};
