// app_config.cpp
#include "app_config.h"
#include "fixed_pos.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

AppConfig g_cfg = AppConfig();
CfgStatus g_cfgStatus = CfgStatus();

// ---- 初期値 ----
static const char* const DEF_DIGIPATH[DIGIPATH_CHOICE_COUNT] = { "", "WIDE1-1", "WIDE1-1,WIDE2-1" };
static const char* const DEF_MSG[] = { "QSL", "73", "TNX", "QRV", "QRT", "OK", "On my way", "Arrived" };
#define DEF_TX_AMP_MARK     100     // IF v1 基板で確認した値
#define DEF_TX_AMP_SPACE    55
#define DEF_PTT_ON_DELAY    300
#define DEF_PTT_OFF_DELAY   50
#define DEF_PTT_ACTIVE_HIGH 1
#define DEF_TX_PREAMBLE     60

void app_config_defaults(AppConfig* c) {
  memset(c, 0, sizeof(*c));
  snprintf(c->callsign, sizeof(c->callsign), "%s", "NOCALL");
  snprintf(c->comment, sizeof(c->comment), "%s", "M5Stack");
  for (int i = 0; i < FIXED_POSITION_COUNT; i++) snprintf(c->fixed[i].name, sizeof(c->fixed[i].name), i == 0 ? "Home" : "Spot%d", i + 1);
  for (int i = 0; i < DIGIPATH_CHOICE_COUNT; i++) snprintf(c->digipath[i], sizeof(c->digipath[i]), "%s", DEF_DIGIPATH[i]);
  int n = (int)(sizeof(DEF_MSG) / sizeof(DEF_MSG[0])); if (n > MSG_PRESET_MAX) n = MSG_PRESET_MAX;
  for (int i = 0; i < n; i++) snprintf(c->msgPresets[i], sizeof(c->msgPresets[i]), "%s", DEF_MSG[i]);
  c->msgPresetCount = (uint8_t)n;
  c->friendCount = 0;
  c->txAmpMark = DEF_TX_AMP_MARK; c->txAmpSpace = DEF_TX_AMP_SPACE;
  c->pttOnDelayMs = DEF_PTT_ON_DELAY; c->pttOffDelayMs = DEF_PTT_OFF_DELAY;
  c->pttActiveHigh = DEF_PTT_ACTIVE_HIGH; c->txPreambleFlags = DEF_TX_PREAMBLE;
}

// 起動したときから初期値にしておく (g_cfg の定義より後に置く)
static struct CfgInit { CfgInit() { app_config_defaults(&g_cfg); } } s_cfgInit;

bool app_config_callsign_ok(const AppConfig* c) {
  return c->callsign[0] && strcmp(c->callsign, "NOCALL") != 0;
}

bool app_config_flex_ok(const AppConfig* c) {
  return (c->flex.set & FLEX_KEYS_REQUIRED) == FLEX_KEYS_REQUIRED && c->flex.lowSpeed < c->flex.highSpeed;
}

// フレックスのキー: 名前、範囲、入れる先 (set の bit は表の順)
struct FlexKey { const char* key; long lo, hi; };
static const FlexKey FLEX_KEYS[] = {
  { "flex_low_speed", 2, 30 }, { "flex_high_speed", 2, 90 }, { "flex_slow_rate", 1, 100 }, { "flex_fast_rate", 10, 180 },
  { "flex_turn_angle", 5, 90 }, { "flex_turn_slope", 1, 255 }, { "flex_turn_time", 5, 180 }, { "flex_slow_dist", 0, 5000 },
};
static void flex_store(FlexConfig* f, int i, long v) {
  switch (i) {
    case 0: f->lowSpeed = (uint8_t)v; break;    case 1: f->highSpeed = (uint8_t)v; break;
    case 2: f->slowRateMin = (uint8_t)v; break; case 3: f->fastRateSec = (uint8_t)v; break;
    case 4: f->turnAngle = (uint8_t)v; break;   case 5: f->turnSlope = (uint8_t)v; break;
    case 6: f->turnTimeSec = (uint8_t)v; break; default: f->slowDistM = (uint16_t)v; break;
  }
  f->set |= (uint8_t)(1u << i);
}

bool fixed_position_set(int i) {
  if (i < 0 || i >= FIXED_POSITION_COUNT) return false;
  return pos_plausible(g_cfg.fixed[i].latDeg, g_cfg.fixed[i].lonDeg);
}
int fixed_position_count_set() {
  int n = 0;
  for (int i = 0; i < FIXED_POSITION_COUNT; i++) if (fixed_position_set(i)) n++;
  return n;
}

// ---- 値の確かめ ----
static void trim(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
  size_t i = 0; while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, n - i + 1);
}
static void upper(char* s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static bool printable_ascii(const char* s) { for (; *s; s++) if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false; return true; }

// コールサイン (英数字1〜6文字、数字を含む)。allowSsid なら "-0"〜"-15" を付けてよい
static bool valid_call(const char* s, bool allowSsid) {
  int n = 0; bool digit = false, alpha = false;
  while (s[n] && s[n] != '-') {
    if (isdigit((unsigned char)s[n])) digit = true; else if (isupper((unsigned char)s[n])) alpha = true; else return false;
    n++;
  }
  if (n < 1 || n > 6 || !digit || !alpha) return false;
  if (!s[n]) return true;
  if (!allowSsid) return false;
  const char* q = s + n + 1;
  if (!*q || strlen(q) > 2) return false;
  for (const char* p = q; *p; p++) if (!isdigit((unsigned char)*p)) return false;
  if (q[0] == '0' && q[1]) return false;               // "-05" は不可
  return atoi(q) <= 15;
}
// デジパスの1つ (WIDE1-1 など)。数字を含まない名前 (RELAY 等) も通す
static bool valid_path_item(const char* s) {
  int n = 0;
  while (s[n] && s[n] != '-') { if (!isalnum((unsigned char)s[n]) || islower((unsigned char)s[n])) return false; n++; }
  if (n < 1 || n > 6) return false;
  if (!s[n]) return true;
  const char* q = s + n + 1;
  if (!*q || strlen(q) > 2) return false;
  for (const char* p = q; *p; p++) if (!isdigit((unsigned char)*p)) return false;
  return atoi(q) <= 15;
}
static bool valid_digipath(const char* s) {
  if (!*s) return true;
  char buf[CFG_DIGIPATH_MAX + 1]; snprintf(buf, sizeof(buf), "%s", s);
  int items = 0;
  for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    trim(tok);
    if (!valid_path_item(tok)) return false;
    if (++items > 8) return false;
  }
  return items > 0 && s[strlen(s) - 1] != ',' && s[0] != ',' && !strstr(s, ",,");
}
static bool parse_int(const char* s, long lo, long hi, long* out) {
  if (!*s) return false;
  char* e; long v = strtol(s, &e, 10);
  if (*e || v < lo || v > hi) return false;
  *out = v; return true;
}
static bool parse_double(const char* s, double* out) {
  while (*s == ' ') s++;
  if (!*s) return false;
  char* e; double v = strtod(s, &e);
  while (*e == ' ') e++;
  if (*e) return false;
  *out = v; return true;
}
// "name, lat, lon[, alt]"。空なら未設定に戻す
static uint8_t parse_fixed(const char* v, FixedPosition* f, int index) {
  FixedPosition r; memset(&r, 0, sizeof(r));
  if (!*v) { snprintf(r.name, sizeof(r.name), index == 0 ? "Home" : "Spot%d", index + 1); *f = r; return 0; }
  char buf[128]; snprintf(buf, sizeof(buf), "%s", v);
  char* part[5]; int n = 0;
  char* p = buf; part[n++] = p;
  for (; *p; p++) if (*p == ',') { *p = 0; if (n >= 5) return CFG_E_BAD_VALUE; part[n++] = p + 1; }
  if (n < 3 || n > 4) return CFG_E_BAD_VALUE;
  for (int i = 0; i < n; i++) trim(part[i]);
  if (!part[0][0] || !printable_ascii(part[0])) return CFG_E_BAD_VALUE;
  if (strlen(part[0]) > CFG_NAME_MAX) return CFG_E_TOO_LONG;
  if (!parse_double(part[1], &r.latDeg) || !parse_double(part[2], &r.lonDeg) || !pos_plausible(r.latDeg, r.lonDeg)) return CFG_E_BAD_VALUE;
  if (n == 4) { if (!parse_double(part[3], &r.altM) || r.altM < -500 || r.altM > 99999) return CFG_E_BAD_VALUE; r.hasAlt = true; }
  snprintf(r.name, sizeof(r.name), "%s", part[0]);
  *f = r; return 0;
}
// メッセージ本文として送れる文字 ('|' '~' '{' は使えない)
static bool valid_msg_text(const char* s) {
  if (!*s || !printable_ascii(s)) return false;
  return !strpbrk(s, "|~{");
}

// "msg3" → 3。prefix が合わなければ 0
static int key_index(const char* key, const char* prefix, int max) {
  size_t n = strlen(prefix);
  if (strncmp(key, prefix, n) != 0) return 0;
  long v;
  if (!parse_int(key + n, 1, max, &v) || key[n] == '0') return 0;
  return (int)v;
}

static void add_error(CfgStatus* st, int line, uint8_t code, const char* key) {
  if (!st) return;
  if (st->errorCount < CFG_ERROR_MAX) {
    CfgError* e = &st->errors[st->errorCount];
    e->line = (uint16_t)line; e->code = code; snprintf(e->key, sizeof(e->key), "%s", key ? key : "");
  }
  if (st->errorCount < 255) st->errorCount++;
}

// 1つのキーと値を c に入れる。戻り値: 0 = 成功、それ以外は CfgErrorCode
static uint8_t apply(AppConfig* c, const char* key, char* v, bool* msgSeen, bool* friendSeen,
                     char msgs[MSG_PRESET_MAX][CFG_MSG_TEXT_MAX + 1], char friends[MSG_FRIEND_MAX][CFG_CALL_MAX]) {
  long n; int i;
  if (!strcmp(key, "callsign")) {
    upper(v);
    if (strlen(v) > 6 && !strchr(v, '-')) return CFG_E_TOO_LONG;
    if (!valid_call(v, false) && strcmp(v, "NOCALL") != 0) return CFG_E_BAD_VALUE;   // SSIDは書かない (設定メニューで選ぶ)
    snprintf(c->callsign, sizeof(c->callsign), "%s", v); return 0;
  }
  if (!strcmp(key, "comment")) {
    if (!printable_ascii(v) || strpbrk(v, "|~")) return CFG_E_BAD_VALUE;
    if (strlen(v) > CFG_COMMENT_MAX) return CFG_E_TOO_LONG;
    snprintf(c->comment, sizeof(c->comment), "%s", v); return 0;
  }
  if ((i = key_index(key, "fixed", FIXED_POSITION_COUNT)) > 0) return parse_fixed(v, &c->fixed[i - 1], i - 1);
  if ((i = key_index(key, "digipath", DIGIPATH_CHOICE_COUNT)) > 0) {
    upper(v);
    if (strlen(v) > CFG_DIGIPATH_MAX) return CFG_E_TOO_LONG;
    char t[CFG_DIGIPATH_MAX + 1]; size_t k = 0;            // 空白を取り除く ("WIDE1-1, WIDE2-1" も可)
    for (const char* p = v; *p && k < sizeof(t) - 1; p++) if (*p != ' ' && *p != '\t') t[k++] = *p;
    t[k] = 0;
    if (!valid_digipath(t)) return CFG_E_BAD_VALUE;
    snprintf(c->digipath[i - 1], sizeof(c->digipath[i - 1]), "%s", t); return 0;
  }
  if ((i = key_index(key, "msg", MSG_PRESET_MAX)) > 0) {
    if (*v && strlen(v) > CFG_MSG_TEXT_MAX) return CFG_E_TOO_LONG;
    if (*v && !valid_msg_text(v)) return CFG_E_BAD_VALUE;
    *msgSeen = true; snprintf(msgs[i - 1], CFG_MSG_TEXT_MAX + 1, "%s", v); return 0;
  }
  if ((i = key_index(key, "friend", MSG_FRIEND_MAX)) > 0) {
    upper(v);
    if (*v && strlen(v) >= CFG_CALL_MAX) return CFG_E_TOO_LONG;
    if (*v && !valid_call(v, true)) return CFG_E_BAD_VALUE;
    *friendSeen = true; snprintf(friends[i - 1], CFG_CALL_MAX, "%s", v); return 0;
  }
  if (!strcmp(key, "tx_amp_mark"))     { if (!parse_int(v, 1, 127, &n)) return CFG_E_BAD_VALUE; c->txAmpMark = (uint8_t)n; return 0; }
  if (!strcmp(key, "tx_amp_space"))    { if (!parse_int(v, 1, 127, &n)) return CFG_E_BAD_VALUE; c->txAmpSpace = (uint8_t)n; return 0; }
  if (!strcmp(key, "ptt_on_delay"))    { if (!parse_int(v, 0, 2000, &n)) return CFG_E_BAD_VALUE; c->pttOnDelayMs = (uint16_t)n; return 0; }
  if (!strcmp(key, "ptt_off_delay"))   { if (!parse_int(v, 0, 1000, &n)) return CFG_E_BAD_VALUE; c->pttOffDelayMs = (uint16_t)n; return 0; }
  if (!strcmp(key, "ptt_active_high")) { if (!parse_int(v, 0, 1, &n)) return CFG_E_BAD_VALUE; c->pttActiveHigh = (uint8_t)n; return 0; }
  if (!strcmp(key, "tx_preamble"))     { if (!parse_int(v, 1, 255, &n)) return CFG_E_BAD_VALUE; c->txPreambleFlags = (uint8_t)n; return 0; }
  for (int k = 0; k < (int)(sizeof(FLEX_KEYS) / sizeof(FLEX_KEYS[0])); k++)
    if (!strcmp(key, FLEX_KEYS[k].key)) { if (!parse_int(v, FLEX_KEYS[k].lo, FLEX_KEYS[k].hi, &n)) return CFG_E_BAD_VALUE; flex_store(&c->flex, k, n); return 0; }
  return CFG_E_UNKNOWN_KEY;
}

int app_config_parse(AppConfig* c, const char* text, size_t len, CfgStatus* st) {
  app_config_defaults(c);
  if (st) { st->errorCount = 0; memset(st->errors, 0, sizeof(st->errors)); }
  int errs = 0;
  bool msgSeen = false, friendSeen = false;
  char msgs[MSG_PRESET_MAX][CFG_MSG_TEXT_MAX + 1]; memset(msgs, 0, sizeof(msgs));
  char friends[MSG_FRIEND_MAX][CFG_CALL_MAX];      memset(friends, 0, sizeof(friends));
  size_t i = 0;
  if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) i = 3;   // UTF-8 の BOM
  int line = 0;
  while (i < len) {
    size_t s = i;
    while (i < len && text[i] != '\n') i++;
    size_t n = i - s;
    if (i < len) i++;                                    // '\n' を飛ばす
    line++;
    char buf[160];
    if (n >= sizeof(buf)) { add_error(st, line, CFG_E_LINE_TOO_LONG, ""); errs++; continue; }
    memcpy(buf, text + s, n); buf[n] = 0;
    trim(buf);
    if (!buf[0] || buf[0] == '#') continue;
    char* eq = strchr(buf, '=');
    if (!eq) { add_error(st, line, CFG_E_NO_EQUAL, ""); errs++; continue; }
    *eq = 0;
    trim(buf);
    char key[24];
    if (strlen(buf) >= sizeof(key)) { add_error(st, line, CFG_E_UNKNOWN_KEY, ""); errs++; continue; }
    memcpy(key, buf, strlen(buf) + 1);
    for (char* p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
    char* v = eq + 1; trim(v);
    uint8_t e = apply(c, key, v, &msgSeen, &friendSeen, msgs, friends);
    if (e) { add_error(st, line, e, key); errs++; }
  }
  // 定型文・宛先の候補: ファイルに1つでも書いてあれば、書いたものだけにする (空の欄は詰める)
  if (msgSeen) {
    c->msgPresetCount = 0;
    for (int k = 0; k < MSG_PRESET_MAX; k++) if (msgs[k][0]) snprintf(c->msgPresets[c->msgPresetCount++], CFG_MSG_TEXT_MAX + 1, "%s", msgs[k]);
  }
  if (friendSeen) {
    c->friendCount = 0;
    for (int k = 0; k < MSG_FRIEND_MAX; k++) if (friends[k][0]) snprintf(c->friends[c->friendCount++], CFG_CALL_MAX, "%s", friends[k]);
  }
  return errs;
}

const char* app_config_error_text(uint8_t code) {
  switch (code) {
    case CFG_E_NO_EQUAL:      return "「=」がない";
    case CFG_E_UNKNOWN_KEY:   return "不明な項目";
    case CFG_E_BAD_VALUE:     return "値が不正";
    case CFG_E_TOO_LONG:      return "値が長すぎる";
    case CFG_E_LINE_TOO_LONG: return "行が長すぎる";
    default:                  return "書き間違い";
  }
}
const char* app_config_error_text_en(uint8_t code) {
  switch (code) {
    case CFG_E_NO_EQUAL:      return "missing '='";
    case CFG_E_UNKNOWN_KEY:   return "unknown key";
    case CFG_E_BAD_VALUE:     return "bad value";
    case CFG_E_TOO_LONG:      return "value too long";
    case CFG_E_LINE_TOO_LONG: return "line too long";
    default:                  return "error";
  }
}

// ---- 見本のファイル ----
size_t app_config_template(char* out, size_t cap) {
  AppConfig d; app_config_defaults(&d);
  size_t n = 0;
  bool ok = true;
#define PUT(...) do { if (ok) { int w = snprintf(out + n, cap > n ? cap - n : 0, __VA_ARGS__); if (w < 0 || n + (size_t)w >= cap) ok = false; else n += (size_t)w; } } while (0)
  PUT("# M5APRS config.txt\n");
  PUT("# 1行に1つ「キー = 値」で書きます。行の先頭が # の行はコメントです (値の途中の # は文字として扱います)。\n");
  PUT("# 書き換えたら、本体を再起動すると反映されます。書き間違いのある行は無視されます (画面に行番号が出ます)。\n");
  PUT("# One setting per line: key = value. Lines starting with # are comments. Restart after editing.\n\n");
  PUT("# 自局のコールサイン (SSIDは書かない。SSIDは本体の設定メニューで選ぶ)。NOCALL のままでは送信しません\n");
  PUT("callsign = %s\n\n", d.callsign);
  PUT("# 位置に付けるコメント (半角英数字、43文字まで)\n");
  PUT("comment = %s\n\n", d.comment);
  PUT("# 固定位置 (最大8件): 名前, 緯度, 経度[, 高度m]  (名前は半角9文字まで。北緯・東経が正)\n");
  PUT("# 例: fixed1 = Home, 35.68123, 139.76712, 12\n");
  for (int i = 0; i < FIXED_POSITION_COUNT; i++) PUT("fixed%d =\n", i + 1);
  PUT("\n# デジパスの候補 (3つ。設定メニューで選ぶ。空 = なし)\n");
  for (int i = 0; i < DIGIPATH_CHOICE_COUNT; i++) PUT("digipath%d = %s\n", i + 1, d.digipath[i]);
  PUT("\n# メッセージの定型文 (最大8件。半角英数字67文字まで。| ~ { は使えない)\n");
  for (int i = 0; i < MSG_PRESET_MAX; i++) PUT("msg%d = %s\n", i + 1, i < d.msgPresetCount ? d.msgPresets[i] : "");
  PUT("\n# よく交信する局 (メッセージの宛先の候補に出す。最大8局。SSID付きでもよい)\n");
  for (int i = 0; i < MSG_FRIEND_MAX; i++) PUT("friend%d =\n", i + 1);
  PUT("\n# ---- 無線機・インターフェース基板に合わせて調整する値 (IF v1 基板で確かめた値) ----\n");
  PUT("# 送信トーンの振幅 (1〜127)。2200Hz はマイク経路で強く変調されるので小さめにする\n");
  PUT("tx_amp_mark = %u\n", (unsigned)d.txAmpMark);
  PUT("tx_amp_space = %u\n", (unsigned)d.txAmpSpace);
  PUT("# PTTを入れてから音を出すまで / 音が終わってからPTTを切るまで (ms)\n");
  PUT("ptt_on_delay = %u\n", (unsigned)d.pttOnDelayMs);
  PUT("ptt_off_delay = %u\n", (unsigned)d.pttOffDelayMs);
  PUT("# 1: HIGHでPTTオン (IF v1 基板)。0: LOWでオン\n");
  PUT("ptt_active_high = %u\n", (unsigned)d.pttActiveHigh);
  PUT("# パケットの先頭に付けるフラグの数 (60 で約400ms)\n");
  PUT("tx_preamble = %u\n", (unsigned)d.txPreambleFlags);
  PUT("\n# ---- フレックス (ビーコン自動送信の選択肢) ----\n");
  PUT("# 速度と曲がり方で送信の間隔を変える (八重洲・ケンウッドのスマートビーコニングと同じ方式)\n");
  PUT("# 値は両社の初期値。flex_slow_dist 以外の7つがそろっていないと選べない\n");
  PUT("# 遅いとき (km/h、2〜30) / 速いとき (km/h、2〜90。low より大きく)\n");
  PUT("flex_low_speed = 5\nflex_high_speed = 70\n");
  PUT("# 遅いときの間隔 (分、1〜100) / 速いときの間隔 (秒、10〜180)。その間は速度に反比例\n");
  PUT("flex_slow_rate = 30\nflex_fast_rate = 120\n");
  PUT("# 曲がったら送る: 進行方向が turn_angle + turn_slope×10÷速度 (度) より大きく変わったとき\n");
  PUT("# turn_angle (度、5〜90) / turn_slope (1〜255) / 送信の最短間隔 turn_time (秒、5〜180)\n");
  PUT("flex_turn_angle = 28\nflex_turn_slope = 26\nflex_turn_time = 30\n");
  PUT("# 独自の追加: 遅いときでも、前回送った位置からこの距離 (m、0〜5000) 動いたら送る。0 = 使わない\n");
  PUT("flex_slow_dist = 0\n");
#undef PUT
  return ok ? n : 0;
}
