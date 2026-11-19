// last_pos_nvs.cpp
#include "last_pos_nvs.h"

#include <Preferences.h>
#include <string.h>

// 保存形式 (26バイト): [0]=版(1) [1]=フラグ(bit0=高度あり) [2..9]=緯度 [10..17]=経度 [18..25]=高度。double は実行環境のまま
#define LP_VER   1
#define LP_BYTES 26
#define LP_KEY   "lastpos"

static Preferences g_prefs;
static bool        g_open = false;

static bool nvs_load(void*, LastPos* out) {
  if (!g_open || !g_prefs.isKey(LP_KEY)) return false;
  if (g_prefs.getBytesLength(LP_KEY) != LP_BYTES) return false;
  uint8_t b[LP_BYTES];
  if (g_prefs.getBytes(LP_KEY, b, LP_BYTES) != LP_BYTES) return false;
  if (b[0] != LP_VER) return false;
  memset(out, 0, sizeof(*out));
  out->hasAlt = (b[1] & 1) != 0;
  memcpy(&out->latDeg, b + 2, 8); memcpy(&out->lonDeg, b + 10, 8); memcpy(&out->altM, b + 18, 8);
  out->valid = true;
  return true;
}

static bool nvs_save(void*, const LastPos* p) {
  if (!g_open) return false;
  uint8_t b[LP_BYTES];
  b[0] = LP_VER; b[1] = p->hasAlt ? 1 : 0;
  memcpy(b + 2, &p->latDeg, 8); memcpy(b + 10, &p->lonDeg, 8); memcpy(b + 18, &p->altM, 8);
  return g_prefs.putBytes(LP_KEY, b, LP_BYTES) == LP_BYTES;
}

const LastPosBackend* last_pos_nvs_backend() {
  static LastPosBackend be = { 0, nvs_load, nvs_save };
  if (!g_open) g_open = g_prefs.begin(NVS_NAMESPACE, false);
  return &be;
}
