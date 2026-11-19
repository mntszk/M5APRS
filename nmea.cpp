// nmea.cpp
#include "nmea.h"

#include <string.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool nmea_checksum_ok(const char* s) {
  if (!s || s[0] != '$') return false;
  uint8_t x = 0;
  const char* p = s + 1;
  while (*p && *p != '*') x ^= (uint8_t)*p++;
  if (*p != '*') return false;
  int h1 = hex_val(p[1]), h2 = h1 < 0 ? -1 : hex_val(p[2]);
  if (h1 < 0 || h2 < 0) return false;
  if (p[3] != '\0') return false;
  return x == (uint8_t)(h1 * 16 + h2);
}

// ---- 数値の読み取り (文字列そのもの。ロケールや浮動小数点の文字列変換に頼らない) ----

// "123.456" 形式。符号なし。空・不正なら false
static bool parse_num(const char* s, double* out) {
  if (!s || !*s) return false;
  double v = 0.0, scale = 1.0;
  bool dot = false, any = false;
  for (; *s; s++) {
    if (is_digit(*s)) {
      any = true;
      if (!dot) v = v * 10.0 + (*s - '0');
      else { scale /= 10.0; v += (*s - '0') * scale; }
    } else if (*s == '.' && !dot) dot = true;
    else return false;
  }
  if (!any) return false;
  *out = v;
  return true;
}

static bool parse_signed(const char* s, double* out) {
  if (s && *s == '-') { double v; if (!parse_num(s + 1, &v)) return false; *out = -v; return true; }
  return parse_num(s, out);
}

// 緯度 "ddmm.mmmm" / 経度 "dddmm.mmmm" と、N/S/E/W → 度
static bool parse_coord(const char* val, const char* hemi, int degDigits, bool isLat, double* out) {
  if (!val || !hemi || !hemi[0] || hemi[1]) return false;
  size_t n = strlen(val);
  if (n < (size_t)degDigits + 2) return false;
  int deg = 0;
  for (int i = 0; i < degDigits; i++) { if (!is_digit(val[i])) return false; deg = deg * 10 + (val[i] - '0'); }
  double min;
  if (!parse_num(val + degDigits, &min)) return false;
  if (!is_digit(val[degDigits]) || !is_digit(val[degDigits + 1])) return false;
  if (min >= 60.0) return false;
  double v = deg + min / 60.0;
  if (isLat) { if (v > 90.0) return false; }
  else       { if (v > 180.0) return false; }
  char h = hemi[0];
  if (isLat) { if (h != 'N' && h != 'S') return false; *out = (h == 'S') ? -v : v; }
  else       { if (h != 'E' && h != 'W') return false; *out = (h == 'W') ? -v : v; }
  return true;
}

// "hhmmss(.sss)" と "ddmmyy"
static bool parse_hms(const char* s, uint8_t* h, uint8_t* m, uint8_t* sec) {
  if (!s || strlen(s) < 6) return false;
  for (int i = 0; i < 6; i++) if (!is_digit(s[i])) return false;
  int hh = (s[0] - '0') * 10 + (s[1] - '0'), mm = (s[2] - '0') * 10 + (s[3] - '0'), ss = (s[4] - '0') * 10 + (s[5] - '0');
  if (hh > 23 || mm > 59 || ss > 60) return false;
  *h = (uint8_t)hh; *m = (uint8_t)mm; *sec = (uint8_t)(ss > 59 ? 59 : ss);
  return true;
}

static bool parse_dmy(const char* s, uint16_t* y, uint8_t* mo, uint8_t* d) {
  if (!s || strlen(s) != 6) return false;
  for (int i = 0; i < 6; i++) if (!is_digit(s[i])) return false;
  int dd = (s[0] - '0') * 10 + (s[1] - '0'), mm = (s[2] - '0') * 10 + (s[3] - '0'), yy = (s[4] - '0') * 10 + (s[5] - '0');
  if (dd < 1 || dd > 31 || mm < 1 || mm > 12) return false;
  *d = (uint8_t)dd; *mo = (uint8_t)mm; *y = (uint16_t)(2000 + yy);
  return true;
}

// ---- センテンスの処理 ----
#define MAX_FIELDS 20

static void handle_rmc(GpsState* g, char* f[], int nf, uint32_t nowMs) {
  // 0:$xxRMC 1:time 2:status 3:lat 4:N/S 5:lon 6:E/W 7:speed 8:course 9:date ... 12:mode
  if (nf < 10) return;
  bool valid = (f[2][0] == 'A' && f[2][1] == '\0');
  if (nf > 12 && f[12][0] == 'N') valid = false;       // モード指示が N (測位なし)

  // 日時: 測位前 (V) でも、衛星から時刻を得ていれば日付が入る。日付のない行や、時刻合わせ前の
  // 既定の日付 (1980年など) は使わない。測位している (A) ときは、そのまま使う
  uint8_t hh, mm, ss, d, mo; uint16_t y;
  if (parse_hms(f[1], &hh, &mm, &ss) && parse_dmy(f[9], &y, &mo, &d) &&
      (valid || (y >= NMEA_V_MIN_YEAR && y <= NMEA_V_MAX_YEAR))) {
    g->hasTime = true; g->timeMs = nowMs; g->year = y; g->month = mo; g->day = d; g->hour = hh; g->minute = mm; g->second = ss;
  }
  if (!valid) { g->lastRmcValid = false; return; }     // V は位置に使わない

  double lat, lon;
  if (!parse_coord(f[3], f[4], 2, true, &lat) || !parse_coord(f[5], f[6], 3, false, &lon)) {
    g->lastRmcValid = false;
    return;
  }
  g->latDeg = lat; g->lonDeg = lon;
  g->hasPos = true; g->posMs = nowMs; g->lastRmcValid = true;

  double v;
  g->hasSpeed = parse_num(f[7], &v);
  g->speedKnots = g->hasSpeed ? v : 0.0;
  g->hasCourse = parse_num(f[8], &v) && v >= 0.0 && v <= 360.0;
  g->courseDeg = g->hasCourse ? (v >= 360.0 ? 0.0 : v) : 0.0;
}

static void handle_gga(GpsState* g, char* f[], int nf, uint32_t nowMs) {
  // 0:$xxGGA 1:time 2:lat 3:N/S 4:lon 5:E/W 6:quality 7:sats 8:hdop 9:alt 10:unit
  if (nf < 11) return;
  if (!is_digit(f[6][0]) || f[6][1] != '\0') return;
  int q = f[6][0] - '0';
  if (is_digit(f[7][0])) {
    int s = 0; for (const char* p = f[7]; *p && is_digit(*p); p++) s = s * 10 + (*p - '0');
    g->satellites = (uint8_t)(s > 255 ? 255 : s);
  }
  if (q == 0) return;                                  // 測位なし
  double alt;
  if (f[10][0] != 'M' || f[10][1] != '\0') return;      // 単位がメートルのものだけ
  if (!parse_signed(f[9], &alt)) return;
  g->hasAlt = true; g->altM = alt; g->altMs = nowMs;
}

static bool process_line(GpsState* g, char* line, uint32_t nowMs) {
  if (!nmea_checksum_ok(line)) { g->checksumErrors++; return false; }
  // チェックサム部分を切り落とし、カンマで分ける
  char* star = strchr(line, '*');
  *star = '\0';
  char* f[MAX_FIELDS]; int nf = 0;
  char* p = line;
  f[nf++] = p;
  for (; *p; p++) {
    if (*p == ',') {
      *p = '\0';
      if (nf < MAX_FIELDS) f[nf++] = p + 1;
      else break;
    }
  }
  // チェックサムが合っていれば、種類を問わず「GPSからデータが届いている」と数える
  g->anyData = true; g->lastDataMs = nowMs; g->sentencesOk++;
  if (strlen(f[0]) != 6) return true;                   // "$GPRMC" の形でないもの (独自センテンスなど)は使わない
  if (strcmp(f[0] + 3, "RMC") == 0) handle_rmc(g, f, nf, nowMs);
  else if (strcmp(f[0] + 3, "GGA") == 0) handle_gga(g, f, nf, nowMs);
  return true;
}

void gps_init(GpsState* g) {
  memset(g, 0, sizeof(*g));
}

bool gps_feed_char(GpsState* g, char c, uint32_t nowMs) {
  if (c == '$') {                                       // 新しい行の開始 (途中の行は捨てる)
    g->inLine = true; g->overflow = false; g->len = 0;
    g->line[g->len++] = c;
    return false;
  }
  if (!g->inLine) return false;
  if (c == '\r' || c == '\n') {
    bool r = false;
    if (!g->overflow && g->len > 0) {
      g->line[g->len] = '\0';
      r = process_line(g, g->line, nowMs);
    }
    g->inLine = false; g->len = 0; g->overflow = false;
    return r;
  }
  if (g->len + 1 >= NMEA_LINE_MAX) {                    // 長すぎる行は捨てる
    if (!g->overflow) g->overflows++;
    g->overflow = true;
    return false;
  }
  g->line[g->len++] = c;
  return false;
}

size_t gps_feed(GpsState* g, const char* data, size_t n, uint32_t nowMs) {
  size_t cnt = 0;
  for (size_t i = 0; i < n; i++) if (gps_feed_char(g, data[i], nowMs)) cnt++;
  return cnt;
}

// 時刻の差 (millisの一周に対応)
static uint32_t age_ms(uint32_t now, uint32_t then) { return (uint32_t)(now - then); }

bool gps_get_position(const GpsState* g, uint32_t nowMs, uint32_t maxAgeSec, GpsPosition* out) {
  if (!g->hasPos) return false;
  uint32_t a = age_ms(nowMs, g->posMs);
  if (a / 1000u > maxAgeSec || (a / 1000u == maxAgeSec && a % 1000u != 0)) return false;
  if (out) {
    out->latDeg = g->latDeg; out->lonDeg = g->lonDeg;
    out->hasSpeed = g->hasSpeed; out->speedKnots = g->speedKnots;
    out->hasCourse = g->hasCourse; out->courseDeg = g->courseDeg;
    out->ageSec = a / 1000u;
  }
  return true;
}

bool gps_get_altitude(const GpsState* g, uint32_t nowMs, uint32_t maxAgeSec, double* altM) {
  if (!g->hasAlt) return false;
  uint32_t a = age_ms(nowMs, g->altMs);
  if (a / 1000u > maxAgeSec || (a / 1000u == maxAgeSec && a % 1000u != 0)) return false;
  if (altM) *altM = g->altM;
  return true;
}

GpsStatus gps_get_status(const GpsState* g, uint32_t nowMs, uint32_t noDataMs, uint32_t maxAgeSec) {
  if (!g->anyData || age_ms(nowMs, g->lastDataMs) >= noDataMs) return GPS_NO_DATA;
  if (gps_get_position(g, nowMs, maxAgeSec, NULL)) return g->lastRmcValid ? GPS_FIX : GPS_STALE;
  return GPS_NO_FIX;
}

static int days_in_month(int y, int m) {
  static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

bool gps_get_utc(const GpsState* g, uint32_t nowMs, GpsUtc* out) {
  if (!g->hasTime) return false;
  uint32_t sec = age_ms(nowMs, g->timeMs) / 1000u;
  uint32_t t = (uint32_t)g->hour * 3600u + (uint32_t)g->minute * 60u + g->second + sec % 86400u;
  uint32_t days = sec / 86400u;
  if (t >= 86400u) { t -= 86400u; days++; }
  int y = g->year, m = g->month, d = g->day;
  while (days--) {
    if (++d > days_in_month(y, m)) { d = 1; if (++m > 12) { m = 1; y++; } }
  }
  if (out) {
    out->year = (uint16_t)y; out->month = (uint8_t)m; out->day = (uint8_t)d;
    out->hour = (uint8_t)(t / 3600u); out->minute = (uint8_t)((t / 60u) % 60u); out->second = (uint8_t)(t % 60u);
  }
  return true;
}
