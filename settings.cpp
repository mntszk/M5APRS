// settings.cpp
#include "settings.h"

#include <string.h>
#include "fixed_pos.h"     // fixed_position_set

enum { K_AMB = 0, K_DIGI, K_SYM, K_CS, K_ALT, K_MON, K_PSRC, K_FIX, K_AUTO, K_BRI, K_LANG, K_SSID, K_RDR_R, K_RDR_T, K_CHIME };
static const char* const KEYS[SETTINGS_KEY_COUNT] = { "amb", "digi", "sym", "cs", "alt", "mon", "psrc", "fix", "auto", "bri", "lang", "ssid", "rdrr", "rdrt", "chime" };

// 0.7.0 までのプリセット (キー "preset" の値 0〜7) → シンボルとSSID
struct OldPreset { char table, code; uint8_t ssid; };
static const OldPreset OLD_PRESETS[8] = {
  { '/', '>', 9 }, { '/', '[', 7 }, { '/', '-', 0 }, { '/', 'b', 7 }, { '/', 'l', 12 }, { '/', ';', 12 }, { '/', 's', 8 }, { '/', 'k', 14 } };

static uint8_t get_key(const Settings* s, int k) {
  switch (k) {
    case K_AMB:    return s->ambiguity;
    case K_DIGI:   return s->digipathIndex;
    case K_SYM:    return s->symbolIndex;
    case K_SSID:   return s->ssid;
    case K_RDR_R:  return s->radarRange;
    case K_RDR_T:  return s->radarTime;
    case K_CS:     return s->sendCourseSpeed ? 1 : 0;
    case K_ALT:    return s->sendAltitude ? 1 : 0;
    case K_MON:    return s->monitorLevel;
    case K_CHIME:  return s->chimeLevel;
    case K_PSRC:   return s->positionSource;
    case K_AUTO:   return s->autoTx;
    case K_BRI:    return s->brightness;
    case K_LANG:   return s->language;
    default:       return s->fixedIndex;
  }
}

static void set_key(Settings* s, int k, uint8_t v) {
  switch (k) {
    case K_AMB:    s->ambiguity = v; break;
    case K_DIGI:   s->digipathIndex = v; break;
    case K_SYM:    s->symbolIndex = v; break;
    case K_SSID:   s->ssid = v; break;
    case K_RDR_R:  s->radarRange = v; break;
    case K_RDR_T:  s->radarTime = v; break;
    case K_CS:     s->sendCourseSpeed = (v != 0); break;
    case K_ALT:    s->sendAltitude = (v != 0); break;
    case K_MON:    s->monitorLevel = v; break;
    case K_CHIME:  s->chimeLevel = v; break;
    case K_PSRC:   s->positionSource = v; break;
    case K_AUTO:   s->autoTx = v; break;
    case K_BRI:    s->brightness = v; break;
    case K_LANG:   s->language = v; break;
    default:       s->fixedIndex = v; break;
  }
}

static bool valid_key(int k, uint8_t v) {
  switch (k) {
    case K_AMB:    return v <= MAX_AMBIGUITY_LEVEL;
    case K_DIGI:   return v < DIGIPATH_CHOICE_COUNT;
    case K_SYM:    return v < SYMBOL_CHOICE_COUNT;
    case K_SSID:   return v <= 15;
    case K_RDR_R:  return v < RADAR_RANGE_COUNT;
    case K_RDR_T:  return v < RADAR_TIME_COUNT;
    case K_MON:    return v <= MONITOR_LEVEL_MAX;
    case K_CHIME:  return v <= MONITOR_LEVEL_MAX;
    case K_PSRC:   return v <= POSITION_SOURCE_FIXED;
    case K_FIX:    return v < FIXED_POSITION_COUNT;
    case K_AUTO:   return v < AUTO_TX_CHOICES;
    case K_BRI:    return v < BRIGHTNESS_LEVEL_COUNT;
    case K_LANG:   return v < LANGUAGE_COUNT;
    default:       return v <= 1;
  }
}

void settings_defaults(Settings* s) {
  s->ambiguity       = DEFAULT_AMBIGUITY_LEVEL;
  s->digipathIndex   = DEFAULT_DIGIPATH_INDEX;
  { const char* d = DEFAULT_SYMBOL; int i = symbol_find(d[0], d[0] ? d[1] : 0); s->symbolIndex = (uint8_t)(i < 0 ? 0 : i); }
  s->ssid            = (DEFAULT_SSID <= 15) ? DEFAULT_SSID : 9;
  s->sendCourseSpeed = DEFAULT_SEND_COURSE_SPEED;
  s->sendAltitude    = DEFAULT_SEND_ALTITUDE;
  s->monitorLevel    = DEFAULT_MONITOR_LEVEL;
  s->chimeLevel      = (DEFAULT_CHIME_LEVEL <= MONITOR_LEVEL_MAX) ? DEFAULT_CHIME_LEVEL : MONITOR_LEVEL_MAX;
  s->positionSource  = DEFAULT_POSITION_SOURCE;
  s->fixedIndex      = DEFAULT_FIXED_INDEX;
  s->autoTx          = (DEFAULT_AUTO_TX < AUTO_TX_CHOICES) ? DEFAULT_AUTO_TX : AUTO_TX_OFF;
  s->brightness      = (DEFAULT_BRIGHTNESS_LEVEL < BRIGHTNESS_LEVEL_COUNT) ? DEFAULT_BRIGHTNESS_LEVEL : BRIGHTNESS_LEVEL_COUNT - 1;
  s->language        = (DEFAULT_LANGUAGE < LANGUAGE_COUNT) ? DEFAULT_LANGUAGE : 0;
  s->radarRange      = (DEFAULT_RADAR_RANGE < RADAR_RANGE_COUNT) ? DEFAULT_RADAR_RANGE : RADAR_RANGE_AUTO;
  s->radarTime       = (DEFAULT_RADAR_TIME < RADAR_TIME_COUNT) ? DEFAULT_RADAR_TIME : 0;
}

bool settings_sanitize(Settings* s) {
  Settings d; settings_defaults(&d);
  bool changed = false;
  for (int k = 0; k < SETTINGS_KEY_COUNT; k++) {
    if (!valid_key(k, get_key(s, k))) { set_key(s, k, get_key(&d, k)); changed = true; }
  }
  return changed;
}

void settings_init(SettingsMgr* m, const SettingsBackend* be) {
  memset(m, 0, sizeof(*m));
  settings_defaults(&m->cur);
  settings_defaults(&m->saved);
  if (be) m->be = *be;
  if (!m->be.getU8) return;
  for (int k = 0; k < SETTINGS_KEY_COUNT; k++) {
    uint8_t v;
    if (m->be.getU8(m->be.ctx, KEYS[k], &v) && valid_key(k, v)) {
      set_key(&m->cur, k, v);
      set_key(&m->saved, k, v);
    }
    // 保存値が無い、または不正なら初期値のまま。saved も初期値なので、初期値から変えたときだけ書く
  }
  // 0.7.0 までの保存値: プリセットだけがあれば、シンボルとSSIDに直す (saved は初期値のままなので、次の commit で書く)
  uint8_t v, p;
  bool haveNew = m->be.getU8(m->be.ctx, KEYS[K_SYM], &v) || m->be.getU8(m->be.ctx, KEYS[K_SSID], &v);
  if (!haveNew && m->be.getU8(m->be.ctx, "preset", &p) && p < 8) {
    int i = symbol_find(OLD_PRESETS[p].table, OLD_PRESETS[p].code);
    if (i >= 0) m->cur.symbolIndex = (uint8_t)i;
    m->cur.ssid = OLD_PRESETS[p].ssid;
  }
}

static bool set_value(SettingsMgr* m, int k, uint8_t v) {
  if (!valid_key(k, v)) return false;
  if (get_key(&m->cur, k) == v) return false;
  set_key(&m->cur, k, v);
  return true;
}

bool settings_set_ambiguity(SettingsMgr* m, uint8_t level) {
  if (level > MAX_AMBIGUITY_LEVEL) level = MAX_AMBIGUITY_LEVEL;
  return set_value(m, K_AMB, level);
}
bool settings_set_digipath(SettingsMgr* m, uint8_t index)  { return set_value(m, K_DIGI, index); }
bool settings_set_symbol(SettingsMgr* m, uint8_t index)    { return set_value(m, K_SYM, index); }
bool settings_set_ssid(SettingsMgr* m, uint8_t ssid)       { return set_value(m, K_SSID, ssid); }
bool settings_set_course_speed(SettingsMgr* m, bool on)    { return set_value(m, K_CS, on ? 1 : 0); }
bool settings_set_altitude(SettingsMgr* m, bool on)        { return set_value(m, K_ALT, on ? 1 : 0); }
bool settings_set_monitor(SettingsMgr* m, uint8_t level) {
  if (level > MONITOR_LEVEL_MAX) level = MONITOR_LEVEL_MAX;
  return set_value(m, K_MON, level);
}

void settings_next_ambiguity(SettingsMgr* m) {
  settings_set_ambiguity(m, (uint8_t)((m->cur.ambiguity + 1) % (MAX_AMBIGUITY_LEVEL + 1)));
}
void settings_next_digipath(SettingsMgr* m) {
  settings_set_digipath(m, (uint8_t)((m->cur.digipathIndex + 1) % DIGIPATH_CHOICE_COUNT));
}
void settings_next_symbol(SettingsMgr* m) {
  settings_set_symbol(m, (uint8_t)((m->cur.symbolIndex + 1) % SYMBOL_CHOICE_COUNT));
}
void settings_next_ssid(SettingsMgr* m) { settings_set_ssid(m, (uint8_t)((m->cur.ssid + 1) % 16)); }
void settings_toggle_course_speed(SettingsMgr* m) { settings_set_course_speed(m, !m->cur.sendCourseSpeed); }
void settings_toggle_altitude(SettingsMgr* m)     { settings_set_altitude(m, !m->cur.sendAltitude); }
bool settings_set_position_source(SettingsMgr* m, uint8_t source) { return set_value(m, K_PSRC, source); }
bool settings_set_fixed_index(SettingsMgr* m, uint8_t index)      { return set_value(m, K_FIX, index); }
bool settings_set_auto_tx(SettingsMgr* m, uint8_t mode)           { return set_value(m, K_AUTO, mode); }
void settings_next_auto_tx(SettingsMgr* m) { settings_set_auto_tx(m, (uint8_t)((m->cur.autoTx + 1) % AUTO_TX_CHOICES)); }
bool settings_set_brightness(SettingsMgr* m, uint8_t level) { return set_value(m, K_BRI, level); }
bool settings_set_language(SettingsMgr* m, uint8_t lang) { return set_value(m, K_LANG, lang); }
bool settings_set_radar_range(SettingsMgr* m, uint8_t index) { return set_value(m, K_RDR_R, index); }
bool settings_set_radar_time(SettingsMgr* m, uint8_t index)  { return set_value(m, K_RDR_T, index); }
void settings_next_radar_range(SettingsMgr* m) { settings_set_radar_range(m, (uint8_t)((m->cur.radarRange + 1) % RADAR_RANGE_COUNT)); }
void settings_next_radar_time(SettingsMgr* m)  { settings_set_radar_time(m, (uint8_t)((m->cur.radarTime + 1) % RADAR_TIME_COUNT)); }
uint16_t settings_radar_range_km(const SettingsMgr* m) {
  static const uint16_t T[] = RADAR_RANGE_KM;
  return m->cur.radarRange < RADAR_RANGE_AUTO ? T[m->cur.radarRange] : 0;
}
uint16_t settings_radar_time_min(const SettingsMgr* m) {
  static const uint16_t T[RADAR_TIME_COUNT] = RADAR_TIME_MIN;
  return T[m->cur.radarTime < RADAR_TIME_COUNT ? m->cur.radarTime : 0];
}
void settings_next_language(SettingsMgr* m) { settings_set_language(m, (uint8_t)((m->cur.language + 1) % LANGUAGE_COUNT)); }
void settings_next_brightness(SettingsMgr* m) { settings_set_brightness(m, (uint8_t)((m->cur.brightness + 1) % BRIGHTNESS_LEVEL_COUNT)); }
uint8_t settings_backlight(const SettingsMgr* m) {
  static const uint8_t T[BRIGHTNESS_LEVEL_COUNT] = BRIGHTNESS_TABLE;
  uint8_t i = m->cur.brightness < BRIGHTNESS_LEVEL_COUNT ? m->cur.brightness : (uint8_t)(BRIGHTNESS_LEVEL_COUNT - 1);
  return T[i] ? T[i] : 1;                                // 0 (消灯) にはしない
}

void settings_next_position_source(SettingsMgr* m) {
  uint8_t n = (uint8_t)((m->cur.positionSource + 1) % (POSITION_SOURCE_FIXED + 1));
  if (n == POSITION_SOURCE_FIXED && fixed_position_count_set() == 0) n = POSITION_SOURCE_GPS;   // 固定位置が1件もなければ飛ばす
  settings_set_position_source(m, n);
}

void settings_next_fixed(SettingsMgr* m) {
  for (int k = 1; k <= FIXED_POSITION_COUNT; k++) {
    int i = (m->cur.fixedIndex + k) % FIXED_POSITION_COUNT;
    if (fixed_position_set(i)) { settings_set_fixed_index(m, (uint8_t)i); return; }
  }
}

void settings_next_monitor(SettingsMgr* m) {
  settings_set_monitor(m, (uint8_t)((m->cur.monitorLevel + 1) % (MONITOR_LEVEL_MAX + 1)));
}
bool settings_set_chime(SettingsMgr* m, uint8_t level) {
  if (level > MONITOR_LEVEL_MAX) level = MONITOR_LEVEL_MAX;
  return set_value(m, K_CHIME, level);
}
void settings_next_chime(SettingsMgr* m) {
  settings_set_chime(m, (uint8_t)((m->cur.chimeLevel + 1) % (MONITOR_LEVEL_MAX + 1)));
}

const SymbolChoice* settings_symbol(const SettingsMgr* m) {
  return &SYMBOL_CHOICES[m->cur.symbolIndex < SYMBOL_CHOICE_COUNT ? m->cur.symbolIndex : 0];
}
uint8_t settings_ssid(const SettingsMgr* m) { return m->cur.ssid & 15; }

const char* settings_digipath(const SettingsMgr* m) {
  uint8_t i = m->cur.digipathIndex < DIGIPATH_CHOICE_COUNT ? m->cur.digipathIndex : DEFAULT_DIGIPATH_INDEX;
  return g_cfg.digipath[i];
}

bool settings_dirty(const SettingsMgr* m) {
  for (int k = 0; k < SETTINGS_KEY_COUNT; k++) if (get_key(&m->cur, k) != get_key(&m->saved, k)) return true;
  return false;
}

int settings_commit(SettingsMgr* m, bool busy) {
  if (busy || !m->be.putU8) return 0;
  int written = 0;
  for (int k = 0; k < SETTINGS_KEY_COUNT; k++) {
    uint8_t v = get_key(&m->cur, k);
    if (v == get_key(&m->saved, k)) continue;            // 保存済みと同じ値は書かない
    if (m->be.putU8(m->be.ctx, KEYS[k], v)) {
      set_key(&m->saved, k, v);
      m->writeCount++;
      written++;
    } else {
      m->writeErrors++;                                  // 書けなかった: saved は変えない → 次回また試す
    }
  }
  return written;
}
