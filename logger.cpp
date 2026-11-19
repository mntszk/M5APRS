// logger.cpp
#include "logger.h"
#include "nmea.h"        // GpsStatus の名前

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char* const RX_HEADER  = "time_utc,src,via,type,lat,lon,alt_m,dist_km,bearing_deg,symbol,comment,raw\n";
static const char* const OPS_HEADER = "time_utc,uptime_ms,event,detail\n";

enum { T_RX = 0, T_OPS = 1 };
#define DATE_LEN 9                          // "YYYYMMDD" または "nodate" + NUL

struct EntryHdr { uint8_t type; char date[DATE_LEN]; uint16_t len; };
#define HDR_BYTES  ((uint32_t)sizeof(EntryHdr))

// ---------------------------------------------------------------
// 書式
// ---------------------------------------------------------------
size_t logger_csv_field(char* out, size_t cap, const char* s) {
  if (!out || cap < 1) return 0;
  if (!s) s = "";
  bool quote = false;
  for (const char* p = s; *p; p++) if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { quote = true; break; }
  size_t o = 0;
  if (quote) { if (o + 1 >= cap) { out[0] = 0; return 0; } out[o++] = '"'; }
  for (const char* p = s; *p; p++) {
    size_t need = (*p == '"') ? 2 : 1;
    if (o + need + (quote ? 1 : 0) + 1 > cap) { out[0] = 0; return 0; }
    if (*p == '"') out[o++] = '"';
    out[o++] = *p;
  }
  if (quote) out[o++] = '"';
  out[o] = 0;
  return o;
}

// 行の組み立て用
struct LineBuf { char* p; size_t cap, n; bool over; };
static void lb_raw(LineBuf* b, const char* s) {
  size_t l = strlen(s);
  if (b->n + l + 1 > b->cap) { b->over = true; return; }
  memcpy(b->p + b->n, s, l); b->n += l; b->p[b->n] = 0;
}
static void lb_field(LineBuf* b, const char* s, bool comma) {
  char tmp[LOGGER_LINE_MAX];
  size_t l = logger_csv_field(tmp, sizeof(tmp), s);
  if (l == 0 && s && *s) { b->over = true; return; }
  if (comma) lb_raw(b, ",");
  lb_raw(b, tmp);
}

static void time_string(const LogTime* t, char* out, size_t cap) {
  if (t && t->valid) snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ", t->year, t->month, t->day, t->hour, t->minute, t->second);
  else out[0] = 0;
}

static void date_key(const LogTime* t, char out[DATE_LEN]) {
  if (t && t->valid) snprintf(out, DATE_LEN, "%04u%02u%02u", t->year % 10000, t->month % 100, t->day % 100);
  else strcpy(out, "nodate");
}

size_t logger_format_rx_line(char* out, size_t cap, const AprsPacket* pkt, const uint8_t* frame, size_t len,
                             const LogTime* t, const LogOwnPos* own) {
  if (!out || cap < 2) return 0;
  LineBuf b = { out, cap, 0, false };
  out[0] = 0;
  char tm[32]; time_string(t, tm, sizeof(tm));
  lb_field(&b, tm, false);

  // 生パケット (TNC2形式)。読めないフレームは16進
  static char raw[LOGGER_LINE_MAX];
  bool parsed = (pkt != NULL) && aprs_tnc2_string(frame, len, raw, sizeof(raw)) > 0;
  if (!parsed) {
    size_t o = 0;
    o += (size_t)snprintf(raw, sizeof(raw), "HEX:");
    for (size_t i = 0; i < len && o + 3 < sizeof(raw); i++) o += (size_t)snprintf(raw + o, sizeof(raw) - o, "%02X", frame[i]);
  }

  if (!pkt) {
    lb_field(&b, "", true); lb_field(&b, "", true); lb_field(&b, "invalid", true);
    for (int i = 0; i < 6; i++) lb_field(&b, "", true);
    lb_field(&b, "", true);
  } else {
    lb_field(&b, pkt->src, true);
    lb_field(&b, pkt->via, true);
    lb_field(&b, pkt->kind == APRS_KIND_POSITION ? (pkt->compressed ? "compressed" : "uncompressed") : pkt->kind == APRS_KIND_MICE ? "mic-e" : pkt->kind == APRS_KIND_WEATHER ? "weather" : pkt->kind == APRS_KIND_MESSAGE ? "message" :
             pkt->kind == APRS_KIND_STATUS ? "status" : pkt->kind == APRS_KIND_TELEMETRY ? "telemetry" : pkt->kind == APRS_KIND_CAPS ? "capabilities" :
             pkt->kind == APRS_KIND_THIRDPARTY ? "third-party" : "other", true);
    char num[32];
    if (pkt->hasPos) {
      snprintf(num, sizeof(num), "%.5f", pkt->latDeg); lb_field(&b, num, true);
      snprintf(num, sizeof(num), "%.5f", pkt->lonDeg); lb_field(&b, num, true);
      if (pkt->hasAltitude) { snprintf(num, sizeof(num), "%.1f", pkt->altitudeM); lb_field(&b, num, true); }
      else lb_field(&b, "", true);
      if (own && own->valid) {
        snprintf(num, sizeof(num), "%.2f", geo_distance_km(own->latDeg, own->lonDeg, pkt->latDeg, pkt->lonDeg));
        lb_field(&b, num, true);
        int br = (int)lround(geo_bearing_deg(own->latDeg, own->lonDeg, pkt->latDeg, pkt->lonDeg)) % 360;
        snprintf(num, sizeof(num), "%d", br); lb_field(&b, num, true);
      } else { lb_field(&b, "", true); lb_field(&b, "", true); }
      char sym[3] = { pkt->symTable, pkt->symCode, 0 };
      lb_field(&b, sym, true);
    } else {
      for (int i = 0; i < 6; i++) lb_field(&b, "", true);      // 緯度・経度・高度・距離・方位・シンボル
    }
    lb_field(&b, pkt->comment, true);
  }
  lb_field(&b, raw, true);
  lb_raw(&b, "\n");
  if (b.over) {
    // 長すぎる行は、生パケットを切り詰めて作り直す
    char shortRaw[300]; strncpy(shortRaw, raw, sizeof(shortRaw) - 1); shortRaw[sizeof(shortRaw) - 1] = 0;
    LineBuf c = { out, cap, 0, false }; out[0] = 0;
    lb_field(&c, tm, false);
    lb_field(&c, pkt ? pkt->src : "", true);
    lb_field(&c, pkt ? pkt->via : "", true);
    lb_field(&c, "truncated", true);
    for (int i = 0; i < 7; i++) lb_field(&c, "", true);
    lb_field(&c, shortRaw, true);
    lb_raw(&c, "\n");
    if (c.over) return 0;
    return c.n;
  }
  return b.n;
}

size_t logger_format_event_line(char* out, size_t cap, const char* event, const char* detail,
                                const LogTime* t, uint32_t nowMs) {
  if (!out || cap < 2) return 0;
  LineBuf b = { out, cap, 0, false };
  out[0] = 0;
  char tm[32]; time_string(t, tm, sizeof(tm));
  char ms[16]; snprintf(ms, sizeof(ms), "%lu", (unsigned long)nowMs);
  lb_field(&b, tm, false); lb_field(&b, ms, true); lb_field(&b, event ? event : "", true); lb_field(&b, detail ? detail : "", true);
  lb_raw(&b, "\n");
  return b.over ? 0 : b.n;
}

// ---------------------------------------------------------------
// 待ち行列 (バイト単位の循環バッファ)
// ---------------------------------------------------------------
static void q_write(Logger* lg, const void* src, uint32_t n) {
  const uint8_t* s = (const uint8_t*)src;
  uint32_t pos = (lg->head + lg->used) % LOGGER_QUEUE_BYTES;
  for (uint32_t i = 0; i < n; i++) { lg->q[pos] = s[i]; pos = (pos + 1) % LOGGER_QUEUE_BYTES; }
  lg->used += n;
}

static void q_read(const Logger* lg, uint32_t offset, void* dst, uint32_t n) {
  uint8_t* d = (uint8_t*)dst;
  uint32_t pos = (lg->head + offset) % LOGGER_QUEUE_BYTES;
  for (uint32_t i = 0; i < n; i++) { d[i] = lg->q[pos]; pos = (pos + 1) % LOGGER_QUEUE_BYTES; }
}

// 入りきらないときは、古いものから捨てて場所を空ける (新しい記録を残す)
static bool enqueue(Logger* lg, uint8_t type, const LogTime* t, const char* line, size_t n) {
  if (n == 0 || n > 0xFFFF) return false;
  if (HDR_BYTES + n > LOGGER_QUEUE_BYTES) { lg->entriesDropped++; return false; }   // 1件で入りきらない
  while (lg->used + HDR_BYTES + n > LOGGER_QUEUE_BYTES) {
    EntryHdr old; q_read(lg, 0, &old, HDR_BYTES);
    uint32_t sz = HDR_BYTES + old.len;
    lg->head = (lg->head + sz) % LOGGER_QUEUE_BYTES;
    lg->used -= sz;
    lg->entriesDropped++;
  }
  EntryHdr h; memset(&h, 0, sizeof(h));
  h.type = type; date_key(t, h.date); h.len = (uint16_t)n;
  q_write(lg, &h, HDR_BYTES);
  q_write(lg, line, (uint32_t)n);
  lg->entriesQueued++;
  return true;
}

void logger_init(Logger* lg, const LogSink* sink, const char* dir) {
  memset(lg, 0, sizeof(*lg));
  if (sink) lg->sink = *sink;
  strncpy(lg->dir, dir ? dir : "/", sizeof(lg->dir) - 1);
  lg->lastGps = -1;
}

void logger_set_bus_guard(Logger* lg, const LogBusGuard* guard) {
  if (guard) lg->guard = *guard; else memset(&lg->guard, 0, sizeof(lg->guard));
}

bool logger_log_rx(Logger* lg, const AprsPacket* pkt, const uint8_t* frame, size_t len,
                   const LogTime* t, const LogOwnPos* own, uint32_t nowMs) {
  (void)nowMs;
  static char line[LOGGER_LINE_MAX + 64];
  size_t n = logger_format_rx_line(line, sizeof(line), pkt, frame, len, t, own);
  if (n == 0) return false;
  return enqueue(lg, T_RX, t, line, n);
}

bool logger_log_event(Logger* lg, const char* event, const char* detail, const LogTime* t, uint32_t nowMs) {
  char line[LOGGER_LINE_MAX];
  size_t n = logger_format_event_line(line, sizeof(line), event, detail, t, nowMs);
  if (n == 0) return false;
  return enqueue(lg, T_OPS, t, line, n);
}

bool logger_log_tx(Logger* lg, const uint8_t* frame, size_t len, const LogTime* t, uint32_t nowMs) {
  static char raw[LOGGER_LINE_MAX / 2];
  // 送信フレームはFCS込み。ログにはFCSを除いたTNC2形式を入れる
  if (len < 2 || aprs_tnc2_string(frame, len - 2, raw, sizeof(raw)) == 0) return logger_log_event(lg, "tx", "unparsable", t, nowMs);
  return logger_log_event(lg, "tx", raw, t, nowMs);
}

void logger_note_gps_status(Logger* lg, int status, const LogTime* t, uint32_t nowMs) {
  if (status == lg->lastGps) return;
  lg->lastGps = status;
  const char* name = status == GPS_FIX ? "fix" : status == GPS_STALE ? "stale" : status == GPS_NO_FIX ? "no_fix" : "no_data";
  logger_log_event(lg, "gps", name, t, nowMs);
}

void logger_note_counters(Logger* lg, uint32_t ringOverruns, uint32_t rxDiscards, uint32_t queueDropped,
                          const LogTime* t, uint32_t nowMs) {
  char d[48];
  if (ringOverruns != lg->seenOverruns) {
    snprintf(d, sizeof(d), "+%lu total=%lu", (unsigned long)(ringOverruns - lg->seenOverruns), (unsigned long)ringOverruns);
    lg->seenOverruns = ringOverruns; logger_log_event(lg, "rx_overrun", d, t, nowMs);
  }
  if (rxDiscards != lg->seenDiscards) {
    snprintf(d, sizeof(d), "+%lu total=%lu", (unsigned long)(rxDiscards - lg->seenDiscards), (unsigned long)rxDiscards);
    lg->seenDiscards = rxDiscards; logger_log_event(lg, "rx_discard", d, t, nowMs);
  }
  if (queueDropped != lg->seenQueueDrops) {
    snprintf(d, sizeof(d), "+%lu total=%lu", (unsigned long)(queueDropped - lg->seenQueueDrops), (unsigned long)queueDropped);
    lg->seenQueueDrops = queueDropped; logger_log_event(lg, "rx_queue_drop", d, t, nowMs);
  }
}

// ---------------------------------------------------------------
// SDへの書き込み
// ---------------------------------------------------------------
int logger_service(Logger* lg, uint32_t nowMs, bool busy, const LogTime* t) {
  if (busy || lg->used == 0 || !lg->sink.append) return 0;
  if (lg->sdFailed && (uint32_t)(nowMs - lg->lastFailMs) < LOGGER_RETRY_MS) return 0;
  if (lg->guard.lock && !lg->guard.lock()) return 0;     // SPIバスが使えない。次回

  // 先頭の1件と、同じファイル向けの連続した分を集める
  EntryHdr first; q_read(lg, 0, &first, HDR_BYTES);
  uint32_t off = 0, bn = 0; int count = 0;
  while (off < lg->used) {
    EntryHdr h; q_read(lg, off, &h, HDR_BYTES);
    if (h.type != first.type || strcmp(h.date, first.date) != 0) break;
    if (bn + h.len > LOGGER_BATCH_MAX) break;
    q_read(lg, off + HDR_BYTES, lg->batch + bn, h.len);
    bn += h.len; off += HDR_BYTES + h.len; count++;
  }
  lg->batch[bn < LOGGER_BATCH_MAX ? bn : LOGGER_BATCH_MAX - 1] = 0;

  char name[40];
  if (first.type == T_RX) snprintf(name, sizeof(name), "rx_%s.csv", first.date);
  else                    snprintf(name, sizeof(name), "ops_%s.log", first.date);
  bool ok = lg->sink.append(lg->sink.ctx, lg->dir, name, first.type == T_RX ? RX_HEADER : OPS_HEADER, lg->batch, bn);
  if (lg->guard.unlock) lg->guard.unlock();

  if (!ok) {
    lg->writeErrors++;
    lg->lastFailMs = nowMs;
    if (!lg->sdFailed) {                                  // 失敗に変わったとき1回だけ記録 (書けるようになってから書かれる)
      lg->sdFailed = true;
      logger_log_event(lg, "sd_error", "write failed", t, nowMs);
    }
    return 0;
  }
  lg->head = (lg->head + off) % LOGGER_QUEUE_BYTES;
  lg->used -= off;
  lg->entriesWritten += (uint32_t)count;
  if (lg->sdFailed) {
    lg->sdFailed = false;
    logger_log_event(lg, "sd_ok", "recovered", t, nowMs);
  }
  if (lg->entriesDropped != lg->reportedDropped) {         // 待ち行列があふれて捨てた分
    char d[40]; snprintf(d, sizeof(d), "+%lu", (unsigned long)(lg->entriesDropped - lg->reportedDropped));
    lg->reportedDropped = lg->entriesDropped;
    logger_log_event(lg, "log_dropped", d, t, nowMs);
  }
  return count;
}

bool logger_sd_ok(const Logger* lg) { return !lg->sdFailed; }
uint32_t logger_pending_bytes(const Logger* lg) { return lg->used; }
