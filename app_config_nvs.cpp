// app_config_nvs.cpp
#include "app_config_nvs.h"
#include "app_config.h"
#include <Preferences.h>
#include <string.h>

#define CFG_NVS_KEY "cfgtxt"

static Preferences g_prefs;
static bool        g_open = false;
static bool open_prefs() { if (!g_open) g_open = g_prefs.begin(NVS_NAMESPACE, false); return g_open; }

size_t app_config_nvs_load(char* buf, size_t cap) {
  if (!open_prefs() || !g_prefs.isKey(CFG_NVS_KEY)) return 0;
  size_t n = g_prefs.getBytesLength(CFG_NVS_KEY);
  if (n == 0 || n > cap) return 0;
  return g_prefs.getBytes(CFG_NVS_KEY, buf, n) == n ? n : 0;
}

bool app_config_nvs_save(const char* text, size_t len) {
  if (!open_prefs() || len == 0 || len > CFG_FILE_MAX) return false;
  static char old[CFG_FILE_MAX];
  size_t on = app_config_nvs_load(old, sizeof(old));
  if (on == len && memcmp(old, text, len) == 0) return false;   // 同じ中身は書かない (フラッシュの書き込み回数を減らす)
  return g_prefs.putBytes(CFG_NVS_KEY, text, len) == len;
}
