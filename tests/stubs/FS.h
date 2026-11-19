#pragma once
#include "esp_stub.h"
#include <string>
#include <map>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
// 試験用のメモリ上のSDカード
struct StubCard {
  bool present = true; bool mounted = false; bool failOpen = false; bool failWrite = false; bool failRename = false;
  std::map<std::string, std::string> files; std::map<std::string, bool> dirs;
  int beginCalls = 0, endCalls = 0;
};
extern StubCard g_card;
class File {
public:
  std::string path; bool ok = false;
  explicit operator bool() const { return ok; }
  size_t size() const { return ok ? g_card.files[path].size() : 0; }
  size_t write(const uint8_t* d, size_t n) { if (!ok || g_card.failWrite) return 0; g_card.files[path].append((const char*)d, n); return n; }
  size_t pos = 0;
  size_t read(uint8_t* d, size_t n) { if (!ok) return 0; const std::string& s = g_card.files[path]; size_t k = 0; while (k < n && pos < s.size()) d[k++] = (uint8_t)s[pos++]; return k; }
  void close() { ok = false; }
};
