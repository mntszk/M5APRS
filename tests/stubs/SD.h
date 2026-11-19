#pragma once
#include "FS.h"
#include "SPI.h"
class SDClass {
public:
  bool begin(uint8_t, SPIClass&, uint32_t) { g_card.beginCalls++; if (!g_card.present) return false; g_card.mounted = true; return true; }
  void end() { g_card.endCalls++; g_card.mounted = false; }
  bool exists(const char* p) { return g_card.mounted && (g_card.dirs.count(p) || g_card.files.count(p)); }
  bool mkdir(const char* p) { if (!g_card.mounted) return false; g_card.dirs[p] = true; return true; }
  File open(const char* p, const char* mode) {
    File f; if (!g_card.mounted || g_card.failOpen) return f;
    std::string m = mode ? mode : "r";
    if (m == "r" && !g_card.files.count(p)) return f;
    f.path = p; f.ok = true;
    if (m == "w") g_card.files[p].clear(); else g_card.files[p];
    return f; }
  bool remove(const char* p) { if (!g_card.mounted) return false; return g_card.files.erase(p) > 0; }
  bool rename(const char* a, const char* b) {
    if (!g_card.mounted || g_card.failRename || !g_card.files.count(a)) return false;
    g_card.files[b] = g_card.files[a]; g_card.files.erase(a); return true; }
};
extern SDClass SD;
