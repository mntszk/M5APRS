// settings_nvs.cpp
#include "settings_nvs.h"

#include <Preferences.h>

static Preferences g_prefs;
static bool        g_open = false;

static bool nvs_get(void*, const char* key, uint8_t* value) {
  if (!g_open || !g_prefs.isKey(key)) return false;
  *value = g_prefs.getUChar(key, 0);
  return true;
}

static bool nvs_put(void*, const char* key, uint8_t value) {
  if (!g_open) return false;
  return g_prefs.putUChar(key, value) == 1;     // 書けたバイト数
}

const SettingsBackend* settings_nvs_backend() {
  static SettingsBackend be = { 0, nvs_get, nvs_put };
  if (!g_open) g_open = g_prefs.begin(NVS_NAMESPACE, false);
  return &be;
}
