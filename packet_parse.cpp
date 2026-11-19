// packet_parse.cpp
#include "packet_parse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------
// 小さな部品
// ---------------------------------------------------------------

// 文字列の末尾に追加(あふれたら切り捨て)
static void append(char* dst, size_t cap, const char* s) {
  size_t n = strlen(dst);
  while (*s && n + 1 < cap) dst[n++] = *s++;
  dst[n] = '\0';
}

static bool is_digit(int c) { return c >= '0' && c <= '9'; }

// 曖昧化の範囲(分)。レベル 0〜4。範囲の中央を位置とする。
static double ambiguity_cell_min(int level) {
  static const double cell[5] = {0.0, 0.1, 1.0, 10.0, 60.0};
  return cell[level];
}

// 分(0以上60未満、曖昧化前の値)を、曖昧化の範囲の中央にそろえる
static double apply_ambiguity(double minutes, int level) {
  if (level <= 0) return minutes;
  double cell = ambiguity_cell_min(level);
  return floor(minutes / cell + 1e-9) * cell + cell / 2.0;
}

// 表示できるASCIIだけにして dst に入れる(それ以外は '?')
static void copy_text(char* dst, size_t cap, const uint8_t* s, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 1 < cap; i++) {
    uint8_t c = s[i];
    dst[o++] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
  }
  dst[o] = '\0';
}

// アドレス7バイトをコールサイン文字列にする。戻り値: そのアドレスが最後なら true
static bool decode_address(const uint8_t* a, char* out, bool* hbit) {
  size_t n = 0;
  for (int i = 0; i < 6; i++) {
    char c = (char)(a[i] >> 1);
    if (c < 0x20 || c > 0x7E) c = '?';
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  int ssid = (a[6] >> 1) & 0x0F;
  if (ssid) n += (size_t)snprintf(out + n, PARSE_CALL_MAX - n, "-%d", ssid);
  out[n] = '\0';
  if (hbit) *hbit = (a[6] & 0x80) != 0;
  return (a[6] & 1) != 0;
}

// ---------------------------------------------------------------
// 高度・コメントの共通処理
// ---------------------------------------------------------------

// コメントから "/A=aaaaaa" (フィート) を探して取り出す。見つけたら取り除く。
static void extract_altitude_feet(char* comment, AprsPacket* p) {
  size_t n = strlen(comment);
  for (size_t i = 0; i + 9 <= n; i++) {
    if (comment[i] == '/' && comment[i + 1] == 'A' && comment[i + 2] == '=') {
      const char* d = comment + i + 3;
      bool neg = (d[0] == '-');
      size_t k = neg ? 1 : 0;
      bool ok = true;
      for (size_t j = k; j < 6; j++) if (!is_digit(d[j])) { ok = false; break; }
      if (!ok) continue;
      long v = 0;
      for (size_t j = k; j < 6; j++) v = v * 10 + (d[j] - '0');
      p->hasAltitude = true;
      p->altitudeM = (neg ? -v : v) * 0.3048;
      memmove(comment + i, comment + i + 9, n - (i + 9) + 1);
      return;
    }
  }
}

// ---------------------------------------------------------------
// 非圧縮位置
// ---------------------------------------------------------------

// 緯度 "DDMM.hhN" (8文字) → 度。曖昧化レベルも返す。不正なら false
static bool parse_lat(const uint8_t* s, double* deg, int* amb) {
  char c[9]; memcpy(c, s, 8); c[8] = '\0';
  if (c[4] != '.' || (c[7] != 'N' && c[7] != 'S')) return false;
  static const int pos[4][4] = { {6, -1, -1, -1}, {5, 6, -1, -1}, {3, 5, 6, -1}, {2, 3, 5, 6} };
  int level = 0;
  for (int i = 0; i < 8; i++) if (c[i] == ' ') level++;
  if (level > 4) return false;
  // 空白は、レベルごとに決まった位置にだけ許す。それ以外は数字
  for (int i = 0; i < 8; i++) {
    if (i == 4 || i == 7) continue;
    bool shouldBlank = false;
    for (int k = 0; k < level; k++) if (pos[level - 1][k] == i) shouldBlank = true;
    if (shouldBlank ? (c[i] != ' ') : !is_digit(c[i])) return false;
  }
  for (int i = 0; i < 8; i++) if (c[i] == ' ') c[i] = '0';
  int d = (c[0] - '0') * 10 + (c[1] - '0');
  int m = (c[2] - '0') * 10 + (c[3] - '0');
  int h = (c[5] - '0') * 10 + (c[6] - '0');
  if (d > 90 || m > 59) return false;
  double mm = apply_ambiguity(m + h / 100.0, level);
  double v = d + mm / 60.0;
  if (v > 90.0) v = 90.0;
  *deg = (c[7] == 'S') ? -v : v;
  *amb = level;
  return true;
}

// 経度 "DDDMM.hhE" (9文字) → 度。曖昧化レベルは緯度に合わせて適用するので、ここでは確認だけ
static bool parse_lon(const uint8_t* s, int level, double* deg) {
  char c[10]; memcpy(c, s, 9); c[9] = '\0';
  if (c[5] != '.' || (c[8] != 'E' && c[8] != 'W')) return false;
  // 空白は、緯度の曖昧化レベルで空白にしてよい位置にだけ許す(緯度が空白なのに経度が数字なのは許す)
  static const int blankPos[4][4] = { {7, -1, -1, -1}, {6, 7, -1, -1}, {4, 6, 7, -1}, {3, 4, 6, 7} };
  for (int i = 0; i < 9; i++) {
    if (i == 5 || i == 8) continue;
    if (c[i] == ' ') {
      bool allowed = false;
      for (int k = 0; k < level; k++) if (blankPos[level - 1][k] == i) allowed = true;
      if (!allowed) return false;
      c[i] = '0';
    } else if (!is_digit(c[i])) return false;
  }
  int d = (c[0] - '0') * 100 + (c[1] - '0') * 10 + (c[2] - '0');
  int m = (c[3] - '0') * 10 + (c[4] - '0');
  int h = (c[6] - '0') * 10 + (c[7] - '0');
  if (d > 180 || m > 59) return false;
  double mm = apply_ambiguity(m + h / 100.0, level);
  double v = d + mm / 60.0;
  if (v > 180.0) v = 180.0;
  *deg = (c[8] == 'W') ? -v : v;
  return true;
}

// ---------------------------------------------------------------
// 気象データ
// ---------------------------------------------------------------
// n桁の数値を読む。'.' や ' ' だけなら「不明」(戻り値 false、*ok=true)。負号は allowNeg のときだけ先頭に1つ
static bool wx_num(const uint8_t* s, size_t n, bool allowNeg, long* v, bool* ok) {
  *ok = false;
  bool blank = true;
  for (size_t i = 0; i < n; i++) if (s[i] != '.' && s[i] != ' ') blank = false;
  if (blank) { *ok = true; return false; }
  size_t i = 0; bool neg = false;
  if (allowNeg && s[0] == '-') { neg = true; i = 1; }
  if (i >= n) return false;
  long x = 0;
  for (; i < n; i++) { if (!is_digit(s[i])) return false; x = x * 10 + (s[i] - '0'); }
  *v = neg ? -x : x; *ok = true;
  return true;
}

// 気象の項目 (1文字 + 固定桁) を順に読む。読めない文字が来たらそこで終わり、その位置を返す
// leadingWind: 先頭が "ddd/sss" (位置付きの気象レポートの風向/風速)
static size_t parse_weather_fields(const uint8_t* s, size_t len, bool leadingWind, AprsWeather* w) {
  size_t i = 0; long v; bool ok;
  if (leadingWind && len >= 7 && s[3] == '/') {
    if (wx_num(s, 3, false, &v, &ok)) { w->hasWindDir = true; w->windDirDeg = (uint16_t)(v % 360); }
    else if (!ok) return 0;
    if (wx_num(s + 4, 3, false, &v, &ok)) { w->hasWindSpeed = true; w->windSpeedMs = v * 0.44704; }
    else if (!ok) return 0;
    i = 7;
  }
  while (i < len) {
    char c = (char)s[i];
    size_t n; bool neg = false;
    switch (c) {
      case 'c': case 's': case 'g': case 'r': case 'p': case 'P': case 'L': case 'l': n = 3; break;
      case 't': n = 3; neg = true; break;
      case 'h': n = 2; break;
      case 'b': n = 5; break;
      default: return i;
    }
    if (i + 1 + n > len) return i;
    bool have = wx_num(s + i + 1, n, neg, &v, &ok);
    if (!ok) return i;
    if (have) {
      switch (c) {
        case 'c': w->hasWindDir = true; w->windDirDeg = (uint16_t)(v % 360); break;
        case 's': w->hasWindSpeed = true; w->windSpeedMs = v * 0.44704; break;
        case 'g': w->hasGust = true; w->gustMs = v * 0.44704; break;
        case 't': w->hasTemp = true; w->tempC = (v - 32) * 5.0 / 9.0; break;
        case 'r': w->hasRain1h = true; w->rain1hMm = v * 0.254; break;
        case 'p': w->hasRain24h = true; w->rain24hMm = v * 0.254; break;
        case 'P': w->hasRainMid = true; w->rainMidMm = v * 0.254; break;
        case 'h': w->hasHumidity = true; w->humidityPct = (uint8_t)(v == 0 ? 100 : (v > 100 ? 100 : v)); break;
        case 'b': w->hasPressure = true; w->pressureHpa = v / 10.0; break;
        case 'L': w->hasLuminosity = true; w->luminosityWm2 = (uint16_t)v; break;
        case 'l': w->hasLuminosity = true; w->luminosityWm2 = (uint16_t)(v + 1000); break;
      }
    }
    i += 1 + n;
  }
  return i;
}

// ---------------------------------------------------------------
// メッセージ ":ADDRESSEE:text{NNNNN"
// ---------------------------------------------------------------
static bool is_alnum(int c) { return is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static void copy_no(char* dst, const uint8_t* s, size_t n) {   // 番号: 英数字のみ、最大5文字
  size_t o = 0;
  for (size_t i = 0; i < n && o < 5; i++) { if (!is_alnum(s[i])) break; dst[o++] = (char)s[i]; }
  dst[o] = 0;
}

static bool parse_message(const uint8_t* info, size_t len, AprsPacket* p) {
  if (len < 11 || info[10] != ':') return false;
  AprsMessage* m = &p->msg;
  size_t n = 9;
  while (n > 0 && info[n] == ' ') n--;                // 宛先の後ろの空白を除く
  copy_text(m->addressee, sizeof(m->addressee), info + 1, n);
  if (!m->addressee[0]) return false;
  const uint8_t* t = info + 11;
  size_t tl = len - 11;
  while (tl > 0 && (t[tl - 1] == '\r' || t[tl - 1] == '\n')) tl--;
  // ack / rej: "ackNNNNN" (番号は英数字1〜5文字)。返信Ack形式 "ackMM}AA" (AAも5文字まで) も受ける。
  // それ以外の文字 ('{' など) が続くもの、番号が長すぎるものは普通の本文
  if (tl >= 4 && (!memcmp(t, "ack", 3) || !memcmp(t, "rej", 3))) {
    size_t i = 3, a = 0, b = 0;
    while (i < tl && is_alnum(t[i])) { i++; a++; }
    bool form = (a >= 1 && a <= 5);
    if (form && i < tl) {
      if (t[i] != '}') form = false;
      else { i++; while (i < tl && is_alnum(t[i])) { i++; b++; } if (i != tl || b > 5) form = false; }
    }
    if (form) {
      m->isAck = (t[0] == 'a'); m->isRej = !m->isAck;
      copy_no(m->ackNo, t + 3, tl - 3);
      m->present = true; p->kind = APRS_KIND_MESSAGE;
      copy_text(p->comment, sizeof(p->comment), info, len);
      return true;
    }
  }
  // 本文と番号 "{NNNNN" (返信Ack形式 "{MM}AA" にも対応)
  size_t body = tl;
  for (size_t i = 0; i < tl; i++) {
    if (t[i] == '{') {
      body = i;
      copy_no(m->msgNo, t + i + 1, tl - i - 1);
      for (size_t k = i + 1; k < tl; k++) if (t[k] == '}') { copy_no(m->ackNo, t + k + 1, tl - k - 1); break; }
      break;
    }
  }
  if (body > APRS_MSG_TEXT_MAX) body = APRS_MSG_TEXT_MAX;
  copy_text(m->text, sizeof(m->text), t, body);
  m->present = true; p->kind = APRS_KIND_MESSAGE;
  copy_text(p->comment, sizeof(p->comment), info, len);   // 受信一覧・ログ用には情報フィールドをそのまま
  return true;
}

// 位置なしの気象レポート "_MMDDHHMM" + 項目
static bool parse_weather_positionless(const uint8_t* info, size_t len, AprsPacket* p) {
  if (len < 9) return false;
  for (int k = 1; k <= 8; k++) if (!is_digit(info[k])) return false;
  size_t used = parse_weather_fields(info + 9, len - 9, false, &p->weather);
  p->kind = APRS_KIND_WEATHER;
  p->weather.present = true;
  copy_text(p->comment, sizeof(p->comment), info + 9 + used, len - 9 - used);
  return true;
}

// ---- 圧縮形式の位置 (APRS101 第9章) ----
// "/YYYYXXXX$csT": 記号の表、緯度4文字・経度4文字 (91進数)、記号、cs (進行方向・速度 / 高度 / 電波の範囲)、種別
static bool base91(const uint8_t* s, int n, uint32_t* out) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < 33 || s[i] > 123) return false;
    v = v * 91u + (uint32_t)(s[i] - 33);
  }
  *out = v; return true;
}
static bool parse_compressed(const uint8_t* info, size_t len, size_t i, AprsPacket* p) {
  if (len < i + 13) return false;
  char table = (char)info[i];
  if (table >= 'a' && table <= 'j') table = (char)('0' + (table - 'a'));      // オーバーレイの数字 0〜9 は a〜j で送られる
  if (!(table == '/' || table == '\\' || (table >= 'A' && table <= 'Z') || (table >= '0' && table <= '9'))) return false;
  uint32_t y, x;
  if (!base91(info + i + 1, 4, &y) || !base91(info + i + 5, 4, &x)) return false;
  double lat = 90.0 - y / 380926.0, lon = -180.0 + x / 190463.0;
  if (!(lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)) return false;
  char code = (char)info[i + 9];
  if (code < 0x21 || code > 0x7E) return false;
  uint8_t c = info[i + 10], s = info[i + 11], t = info[i + 12];

  p->kind = APRS_KIND_POSITION; p->compressed = true;
  p->hasPos = true; p->latDeg = lat; p->lonDeg = lon; p->ambiguity = 0;
  p->symTable = table; p->symCode = code;
  bool hasCs = false; int csCourse = 0; double csKnots = 0;
  if (c != ' ' && t >= 33 && t <= 127 && s >= 33 && s <= 127) {
    int type = t - 33;
    if (c == '{') {
      // 電波の範囲: 使わない
    } else if (((type >> 3) & 3) == 2) {          // GGA から作った高度 (フィート) = 1.002^(cs)
      if (c >= 33 && c <= 123) { p->hasAltitude = true; p->altitudeM = pow(1.002, (c - 33) * 91 + (s - 33)) * 0.3048; }
    } else if (c >= '!' && c <= 'z') {            // 進行方向 = c×4 度、速度 = 1.08^s - 1 ノット
      hasCs = true; csCourse = (c - 33) * 4; csKnots = pow(1.08, s - 33) - 1.0;
    }
  }
  size_t rest = i + 13;
  if (code == '_') {                              // 気象局: cs は風向・風速。コメントに g t r p P h b …
    if (hasCs) {
      p->weather.hasWindDir = true; p->weather.windDirDeg = (uint16_t)(csCourse % 360);
      p->weather.hasWindSpeed = true; p->weather.windSpeedMs = csKnots * 0.514444;
    }
    size_t used = parse_weather_fields(info + rest, len - rest, false, &p->weather);
    p->weather.present = true;
    copy_text(p->comment, sizeof(p->comment), info + rest + used, len - rest - used);
    extract_altitude_feet(p->comment, p);
    return true;
  }
  if (hasCs) {
    long kn = lround(csKnots);
    p->hasSpeed = true; p->speedKnots = (uint16_t)(kn > 999 ? 999 : kn);
    if (csCourse > 0 && csCourse <= 360) { p->hasCourse = true; p->courseDeg = (uint16_t)(csCourse % 360); }
  }
  copy_text(p->comment, sizeof(p->comment), info + rest, len - rest);
  extract_altitude_feet(p->comment, p);
  return true;
}

static bool parse_uncompressed(const uint8_t* info, size_t len, AprsPacket* p) {
  size_t i = 1;                                // DTI の次
  if (p->dti == '/' || p->dti == '@') i += 7;  // 時刻(7文字)
  if (len > i && !is_digit(info[i])) return parse_compressed(info, len, i, p);   // 緯度が数字で始まらなければ圧縮形式
  if (len < i + 8 + 1 + 9 + 1) return false;
  double lat, lon; int amb;
  if (!parse_lat(info + i, &lat, &amb)) return false;
  char table = (char)info[i + 8];
  if (!parse_lon(info + i + 9, amb, &lon)) return false;
  char code = (char)info[i + 18];
  if (table < 0x21 || table > 0x7E || code < 0x21 || code > 0x7E) return false;
  size_t rest = i + 19;

  p->kind = APRS_KIND_POSITION;
  p->hasPos = true; p->latDeg = lat; p->lonDeg = lon; p->ambiguity = (uint8_t)amb;
  p->symTable = table; p->symCode = code;

  if (code == '_') {                              // 気象局: "ddd/sss" は風向/風速 (mph)。続けて g t r p P h b …
    size_t used = parse_weather_fields(info + rest, len - rest, true, &p->weather);
    p->weather.present = true;
    copy_text(p->comment, sizeof(p->comment), info + rest + used, len - rest - used);
    extract_altitude_feet(p->comment, p);
    return true;
  }

  // 進行方向・速度 "CCC/SSS"
  if (len >= rest + 7 && is_digit(info[rest]) && is_digit(info[rest + 1]) && is_digit(info[rest + 2]) &&
      info[rest + 3] == '/' && is_digit(info[rest + 4]) && is_digit(info[rest + 5]) && is_digit(info[rest + 6])) {
    int c = (info[rest] - '0') * 100 + (info[rest + 1] - '0') * 10 + (info[rest + 2] - '0');
    int s = (info[rest + 4] - '0') * 100 + (info[rest + 5] - '0') * 10 + (info[rest + 6] - '0');
    p->hasSpeed = true; p->speedKnots = (uint16_t)s;
    if (c >= 1 && c <= 360) { p->hasCourse = true; p->courseDeg = (uint16_t)(c % 360); }
    rest += 7;
  }
  copy_text(p->comment, sizeof(p->comment), info + rest, len - rest);
  extract_altitude_feet(p->comment, p);
  return true;
}

// ---------------------------------------------------------------
// Mic-E
// ---------------------------------------------------------------
// 機種の印 (APRS Foundation の aprs-deviceid、tocalls.yaml の mice / micelegacy)
// この表 (MICE_DEV) は aprs-deviceid (https://github.com/aprsorg/aprs-deviceid) のデータにもとづくもので、
// CC BY-SA 2.0 (https://creativecommons.org/licenses/by-sa/2.0/) で提供される (ほかの部分は MIT)。THIRD_PARTY_LICENSES.md 参照
struct MiceDev { const char* suffix; const char* model; };
static const MiceDev MICE_DEV[] = {
  { "_ ", "VX-8" }, { "_\"", "FTM-350" }, { "_#", "VX-8G" }, { "_$", "FT1D" }, { "_(", "FT2D" },
  { "_0", "FT3D" }, { "_3", "FT5D" }, { "_1", "FTM-300D" }, { "_2", "FTM-200D" }, { "_4", "FTM-500D" },
  { "_5", "FTM-510D" }, { "_6", "FTX-1" }, { "_7", "FTM-310D" }, { "_)", "FTM-100D" }, { "_%", "FTM-400DR" },
  { "(5", "D578UV" }, { "(8", "D878UV" }, { "|3", "TinyTrak3" }, { "|4", "TinyTrak4" }, { "^A", "APRS Connect" },
  { "^v", "anyfrog" }, { "*v", "KissOZ Tracker" }, { "*9", "AVRT9" }, { ":2", "VP-Tracker" }, { " X", "AP510" },
  { "[1", "APRSdroid" } };

static const char* const MICE_STD[8] = {          // 添字は A*4 + B*2 + C
  "Emergency", "Priority", "Special", "Committed", "Returning", "In Service", "En Route", "Off Duty" };
static const char* const MICE_CUSTOM[8] = {
  "Emergency", "Custom-6", "Custom-5", "Custom-4", "Custom-3", "Custom-2", "Custom-1", "Custom-0" };

static bool parse_mice(const char* dest, const uint8_t* destRaw, const uint8_t* info, size_t len, AprsPacket* p) {
  (void)dest;
  if (len < 9) return false;
  // 宛先の6文字
  int digit[6]; int bit[3] = {0, 0, 0}; int custom[3] = {0, 0, 0};
  int north = 0, off100 = 0, west = 0;
  for (int i = 0; i < 6; i++) {
    int c = destRaw[i] >> 1;
    int d = -1, flag = 0, cust = 0;
    if (c >= '0' && c <= '9')      { d = c - '0'; }
    else if (c >= 'A' && c <= 'J') { d = c - 'A'; flag = 1; cust = 1; }
    else if (c == 'K')             { d = -2; flag = 1; cust = 1; }       // 空白(曖昧化)
    else if (c == 'L')             { d = -2; }
    else if (c >= 'P' && c <= 'Y') { d = c - 'P'; flag = 1; }
    else if (c == 'Z')             { d = -2; flag = 1; }
    else return false;
    if (i >= 3 && cust) return false;                                   // 4〜6文字目に A〜K は使わない
    digit[i] = d;
    if (i < 3) { bit[i] = flag; custom[i] = cust; }
    else if (i == 3) north = flag;
    else if (i == 4) off100 = flag;
    else             west = flag;
  }
  // 曖昧化: 後ろから連続する空白の数 (最大4)。それより前に空白があれば不正
  int amb = 0;
  while (amb < 4 && digit[5 - amb] == -2) amb++;
  for (int i = 0; i < 6 - amb; i++) if (digit[i] < 0) return false;
  int lat[6];
  for (int i = 0; i < 6; i++) lat[i] = digit[i] < 0 ? 0 : digit[i];
  int latDeg = lat[0] * 10 + lat[1];
  int latMin = lat[2] * 10 + lat[3];
  int latHun = lat[4] * 10 + lat[5];
  if (latDeg > 90 || latMin > 59) return false;
  double lm = apply_ambiguity(latMin + latHun / 100.0, amb);
  double latitude = latDeg + lm / 60.0;
  if (latitude > 90.0) latitude = 90.0;
  if (!north) latitude = -latitude;

  // メッセージ (A,B,C)
  int nCustom = custom[0] + custom[1] + custom[2];
  int nBits = bit[0] + bit[1] + bit[2];
  int idx = bit[0] * 4 + bit[1] * 2 + bit[2];
  if (nBits == 0)                      p->micEMessage = "Emergency";
  else if (nCustom == 0)               p->micEMessage = MICE_STD[idx];
  else if (nCustom == nBits)           p->micEMessage = MICE_CUSTOM[idx];
  else                                 p->micEMessage = "";             // 標準とカスタムが混在: 不明

  // 情報フィールド
  int d = (int)info[1] - 28;
  int m = (int)info[2] - 28;
  int h = (int)info[3] - 28;
  if (info[1] < 28 || info[2] < 28 || info[3] < 28) return false;
  if (off100) d += 100;
  if (d >= 190 && d <= 199) d -= 190;
  else if (d >= 180 && d <= 189) d -= 80;
  if (m >= 60) m -= 60;
  if (d < 0 || d > 179 || m < 0 || m > 59 || h < 0 || h > 99) return false;
  double nm = apply_ambiguity(m + h / 100.0, amb);
  double longitude = d + nm / 60.0;
  if (longitude > 180.0) longitude = 180.0;
  if (west) longitude = -longitude;

  // 速度・進行方向
  if (info[4] < 28 || info[5] < 28 || info[6] < 28) return false;
  int sp = ((int)info[4] - 28) * 10;
  int dc = (int)info[5] - 28;
  int se = (int)info[6] - 28;
  int speed = sp + dc / 10;
  int course = (dc % 10) * 100 + se;
  if (speed >= 800) speed -= 800;
  if (course >= 400) course -= 400;

  p->kind = APRS_KIND_MICE;
  p->hasPos = true; p->latDeg = latitude; p->lonDeg = longitude; p->ambiguity = (uint8_t)amb;
  p->symCode = (char)info[7]; p->symTable = (char)info[8];
  p->hasSpeed = true; p->speedKnots = (uint16_t)speed;
  if (course >= 1 && course <= 360) { p->hasCourse = true; p->courseDeg = (uint16_t)(course % 360); }

  // 9バイト目より後: 機種の印、状態テキスト、高度 (APRS 1.2 の Mic-E の機種の決め方)
  //   Kenwood: 先頭 '>' (ハンディ) / ']' (モービル)、末尾に1文字 ('=' '^' '&') が付くことがある
  //   それ以外 (Yaesuなど): 先頭 '`' (メッセージ対応) / '\'' (非対応)、末尾に機種の2文字 ("_(" "_1" "|3" など)
  // 機種名は使わないので、印は取り除いてコメントだけにする。古い仕様の '`' '\'' の16進テレメトリは扱わない
  const uint8_t* s = info + 9;
  size_t n = len - 9;
  int vendor = 0;                                           // 1 = Kenwood、2 = '`' '\'' の形
  bool handheld = false;
  if (n >= 1 && s[0] == 0x1d) {                             // バイナリのテレメトリ(5バイト)
    size_t skip = n >= 6 ? 6 : n;
    s += skip; n -= skip;
  } else if (n >= 1 && (s[0] == '>' || s[0] == ']')) { handheld = (s[0] == '>'); s++; n--; vendor = 1; }
  else if (n >= 1 && (s[0] == '`' || s[0] == '\'')) { s++; n--; vendor = 2; }

  char text[PARSE_COMMENT_MAX + 8];
  copy_text(text, sizeof(text), s, n);
  // 高度 "xxx}" (xxxは0x21〜0x7Bの3文字、10000m下を基準にした91進数)
  size_t tl = strlen(text);
  for (size_t i = 3; i < tl; i++) {
    if (text[i] == '}') {
      bool ok = true;
      for (size_t k = i - 3; k < i; k++) if (text[k] < 33 || text[k] > 123) ok = false;
      if (!ok) continue;
      long v = (text[i - 3] - 33) * 8281L + (text[i - 2] - 33) * 91L + (text[i - 1] - 33);
      p->hasAltitude = true; p->altitudeM = (double)(v - 10000);
      memmove(text + i - 3, text + i + 1, tl - i);
      break;
    }
  }
  // 末尾の機種の印を取り除き、機種名を覚える (詳細表示用)
  tl = strlen(text);
  if (vendor == 1) {                                        // Kenwood: 先頭 '>' はハンディ、']' はモービル
    char last = tl >= 1 ? text[tl - 1] : 0;
    const char* m = handheld ? (last == '=' ? "TH-D72" : last == '^' ? "TH-D74" : last == '&' ? "TH-D75" : "TH-D7A")
                             : (last == '=' ? "TM-D710" : "TM-D700");
    if (last == '=' || (handheld && (last == '^' || last == '&'))) text[--tl] = '\0';
    snprintf(p->model, sizeof(p->model), "%s", m);
  } else if (vendor == 2 && tl >= 2) {
    const char* m = NULL;
    for (size_t k = 0; k < sizeof(MICE_DEV) / sizeof(MICE_DEV[0]); k++)
      if (text[tl - 2] == MICE_DEV[k].suffix[0] && text[tl - 1] == MICE_DEV[k].suffix[1]) { m = MICE_DEV[k].model; break; }
    if (m) snprintf(p->model, sizeof(p->model), "%s", m);
    else if (strchr("_|:*^~([", text[tl - 2])) snprintf(p->model, sizeof(p->model), "不明(%c%c)", text[tl - 2], text[tl - 1]);
    if (m || strchr("_|:*^~([", text[tl - 2])) { tl -= 2; text[tl] = '\0'; }
  }
  strncpy(p->comment, text, sizeof(p->comment) - 1);
  p->comment[sizeof(p->comment) - 1] = '\0';
  return true;
}

// ---------------------------------------------------------------
// フレーム全体
// ---------------------------------------------------------------
// ---------------------------------------------------------------
// 宛先 (TOCALL) から機種を見分ける (Mic-E 以外。0.14.0)
// 表 (tocalls_table.h) は aprs-deviceid のデータで CC BY-SA 2.0。tools/gen_tocalls.py で作る
// ---------------------------------------------------------------
#include "tocalls_table.h"
static bool tocall_match(const char* pat, const char* call) {
  for (size_t i = 0; ; i++) {
    char p = pat[i], c = call[i];
    if (p == '*') return true;                         // 以降なんでも
    if (!p || !c) return !p && !c;
    if (p == '?') continue;
    if (p == 'n') { if (c < '0' || c > '9') return false; continue; }
    if (p != c) return false;
  }
}
const char* aprs_tocall_model(const char* dest) {
  if (!dest) return NULL;
  char base[8]; size_t n = 0;
  while (dest[n] && dest[n] != '-' && n < sizeof(base) - 1) { base[n] = dest[n]; n++; }
  base[n] = 0;
  if (dest[n] && dest[n] != '-') return NULL;          // 7文字以上は宛先として長すぎる
  for (size_t k = 0; k < sizeof(TOCALL_DEV) / sizeof(TOCALL_DEV[0]); k++)
    if (tocall_match(TOCALL_DEV[k].pattern, base)) return TOCALL_DEV[k].model;
  return NULL;
}

static bool parse_frame(const uint8_t* f, size_t len, AprsPacket* out);
bool aprs_parse_frame(const uint8_t* f, size_t len, AprsPacket* out) {
  bool ok = parse_frame(f, len, out);
  // Mic-E は宛先に緯度などを入れているので、宛先の表は使わない (機種はコメント末尾の印から)
  if (ok && !out->model[0] && out->dti != '`' && out->dti != '\'' && out->kind != APRS_KIND_MICE) {
    const char* m = aprs_tocall_model(out->dest);
    if (m) snprintf(out->model, sizeof(out->model), "%s", m);
  }
  return ok;
}

static bool parse_frame(const uint8_t* f, size_t len, AprsPacket* out) {
  if (!f || !out) return false;
  memset(out, 0, sizeof(*out));
  out->kind = APRS_KIND_OTHER;
  out->micEMessage = "";
  if (len < 16) return false;                          // 宛先+送信元+制御+PID

  // アドレス部: 最後のアドレスの最下位ビットが1
  size_t n = 0; bool last = false;
  while (!last) {
    if (n >= 10 || (n + 1) * 7 > len) return false;    // 宛先+送信元+デジピーター最大8
    last = (f[n * 7 + 6] & 1) != 0;
    n++;
  }
  if (n < 2) return false;
  size_t ctlPos = n * 7;
  if (len < ctlPos + 2) return false;

  decode_address(f, out->dest, NULL);
  decode_address(f + 7, out->src, NULL);
  int lastUsed = -1;
  char digis[8][PARSE_CALL_MAX]; bool used[8];
  for (size_t k = 2; k < n; k++) {
    decode_address(f + k * 7, digis[k - 2], &used[k - 2]);
    if (used[k - 2]) lastUsed = (int)(k - 2);
  }
  for (size_t k = 0; k + 2 < n; k++) {
    if (k) append(out->via, sizeof(out->via), ",");
    append(out->via, sizeof(out->via), digis[k]);
    if ((int)k == lastUsed) append(out->via, sizeof(out->via), "*");
  }

  const uint8_t* info = f + ctlPos + 2;
  size_t infoLen = len - ctlPos - 2;
  while (infoLen > 0 && (info[infoLen - 1] == '\r' || info[infoLen - 1] == '\n')) infoLen--;   // 末尾の改行 (多くの機器が付ける)
  if (f[ctlPos] != 0x03 || f[ctlPos + 1] != 0xF0 || infoLen == 0) {
    copy_text(out->comment, sizeof(out->comment), info, infoLen);
    return true;                                       // UI以外、または情報なし
  }
  out->dti = (char)info[0];

  bool ok = false;
  switch (info[0]) {
    case '!': case '=': case '/': case '@':
      ok = parse_uncompressed(info, infoLen, out);
      break;
    case '`': case '\'': case 0x1c: case 0x1d:
      ok = parse_mice(out->dest, f, info, infoLen, out);
      break;
    case '_':
      ok = parse_weather_positionless(info, infoLen, out);
      break;
    case ':':
      ok = parse_message(info, infoLen, out);
      break;
    case '>': {                                        // ステータス。先頭に時刻 "DDHHMMz" があれば除く
      size_t k = 1;
      if (infoLen >= 8) {
        bool ts = true; for (int i = 1; i <= 6; i++) if (!is_digit(info[i])) ts = false;
        if (ts && (info[7] == 'z' || info[7] == '/' || info[7] == 'h')) k = 8;
      }
      out->kind = APRS_KIND_STATUS;
      copy_text(out->comment, sizeof(out->comment), info + k, infoLen - k);
      return true; }
    case '<':                                          // 機能の通知
      out->kind = APRS_KIND_CAPS;
      copy_text(out->comment, sizeof(out->comment), info + 1, infoLen - 1);
      return true;
    case '}':                                          // 第三者パケット: 中身 "SRC>DEST,PATH:INFO"
      out->kind = APRS_KIND_THIRDPARTY;
      copy_text(out->comment, sizeof(out->comment), info + 1, infoLen - 1);
      return true;
    case 'T':
      if (infoLen >= 2 && info[1] == '#') {            // テレメトリ
        out->kind = APRS_KIND_TELEMETRY;
        copy_text(out->comment, sizeof(out->comment), info, infoLen);
        return true;
      }
      break;
    default: break;
  }
  if (!ok) {
    // 位置を読めなかったので、途中まで入った値を消して、情報フィールドをそのまま見せる
    const char* ms = "";
    memset(out->comment, 0, sizeof(out->comment));
    out->kind = APRS_KIND_OTHER; out->hasPos = false; out->hasCourse = out->hasSpeed = out->hasAltitude = false;
    out->ambiguity = 0; out->symTable = out->symCode = 0; out->micEMessage = ms; out->model[0] = 0;
    memset(&out->weather, 0, sizeof(out->weather));
    memset(&out->msg, 0, sizeof(out->msg));
    copy_text(out->comment, sizeof(out->comment), info, infoLen);
  }
  return true;
}

size_t aprs_tnc2_string(const uint8_t* f, size_t len, char* out, size_t cap) {
  if (!out || cap < 2) return 0;
  out[0] = '\0';
  AprsPacket p;
  if (!aprs_parse_frame(f, len, &p)) return 0;
  // 情報フィールドの位置 (aprs_parse_frame と同じ規則でアドレス部の長さを求める)
  size_t n = 0; bool last = false;
  while (!last) { last = (f[n * 7 + 6] & 1) != 0; n++; }
  size_t pos = n * 7 + 2;
  size_t o = 0;
  const char* parts[4] = { p.src, ">", p.dest, NULL };
  for (int i = 0; parts[i]; i++) {
    for (const char* s = parts[i]; *s; s++) { if (o + 1 >= cap) { out[0] = '\0'; return 0; } out[o++] = *s; }
  }
  if (p.via[0]) {
    if (o + 1 >= cap) { out[0] = '\0'; return 0; }
    out[o++] = ',';
    for (const char* s = p.via; *s; s++) { if (o + 1 >= cap) { out[0] = '\0'; return 0; } out[o++] = *s; }
  }
  if (o + 1 >= cap) { out[0] = '\0'; return 0; }
  out[o++] = ':';
  for (size_t i = pos; i < len; i++) {
    uint8_t c = f[i];
    char tmp[5]; size_t tl;
    if (c == '\\') { tmp[0] = '\\'; tmp[1] = '\\'; tl = 2; }
    else if (c >= 0x20 && c <= 0x7E) { tmp[0] = (char)c; tl = 1; }
    else { static const char hx[] = "0123456789ABCDEF"; tmp[0] = '\\'; tmp[1] = 'x'; tmp[2] = hx[c >> 4]; tmp[3] = hx[c & 15]; tl = 4; }
    if (o + tl + 1 > cap) { out[0] = '\0'; return 0; }
    memcpy(out + o, tmp, tl); o += tl;
  }
  out[o] = '\0';
  return o;
}

// ---------------------------------------------------------------
// 距離・方位
// ---------------------------------------------------------------
static const double PI_D = 3.14159265358979323846;

double aprs_knots_to_kmh(double knots) { return knots * 1.852; }

double geo_distance_km(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * PI_D / 180.0, p2 = lat2 * PI_D / 180.0;
  double dp = p2 - p1, dl = (lon2 - lon1) * PI_D / 180.0;
  double s1 = sin(dp / 2.0), s2 = sin(dl / 2.0);
  double h = s1 * s1 + cos(p1) * cos(p2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  if (h < 0.0) h = 0.0;
  return 2.0 * 6371.0088 * asin(sqrt(h));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * PI_D / 180.0, p2 = lat2 * PI_D / 180.0;
  double dl = (lon2 - lon1) * PI_D / 180.0;
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  if (x == 0.0 && y == 0.0) return 0.0;
  double b = atan2(y, x) * 180.0 / PI_D;
  b = fmod(b + 360.0, 360.0);
  if (b >= 360.0) b = 0.0;
  return b;
}
