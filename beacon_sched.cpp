// beacon_sched.cpp
#include "beacon_sched.h"
#include <math.h>

static const uint16_t INTERVALS[AUTO_TX_INTERVAL_COUNT] = AUTO_TX_INTERVALS_MIN;

uint16_t auto_tx_interval_min(uint8_t mode) {
  if (mode < 2 || mode >= AUTO_TX_FLEX) return 0;
  return INTERVALS[mode - 2];
}
uint32_t auto_tx_interval_ms(uint8_t mode) { return (uint32_t)auto_tx_interval_min(mode) * 60000u; }
bool auto_tx_ack_enabled(uint8_t mode) { return mode != AUTO_TX_OFF && mode < AUTO_TX_CHOICES; }
bool auto_tx_is_flex(uint8_t mode) { return mode == AUTO_TX_FLEX; }
bool auto_tx_beacon(uint8_t mode) { return auto_tx_interval_min(mode) != 0 || auto_tx_is_flex(mode); }

void beacon_init(BeaconSched* b, uint8_t mode, uint32_t nowMs) {
  b->mode = mode; b->lastMs = nowMs; b->waiting = false; b->waitSinceMs = 0; b->sent = 0; b->skipped = 0;
  b->flex = 0; b->mv = false; b->kmh = 0; b->hasCourse = false; b->course = 0; b->lat = b->lon = 0;
  b->refCourseOk = false; b->refCourse = 0; b->refPosOk = false; b->refLat = b->refLon = 0;
}

void beacon_set_flex(BeaconSched* b, const FlexConfig* f) { b->flex = f; }

void beacon_set_motion(BeaconSched* b, bool valid, double kmh, bool hasCourse, double courseDeg, double latDeg, double lonDeg) {
  b->mv = valid; b->kmh = valid ? (kmh < 0 ? 0 : kmh) : 0;
  b->hasCourse = valid && hasCourse; b->course = courseDeg; b->lat = latDeg; b->lon = lonDeg;
  // 数え始めてから初めての測位: そこを基準にする (曲がり・距離の判定のため)
  if (valid && !b->refPosOk) { b->refPosOk = true; b->refLat = latDeg; b->refLon = lonDeg; }
  if (b->hasCourse && !b->refCourseOk && b->flex && b->kmh >= b->flex->lowSpeed) { b->refCourseOk = true; b->refCourse = courseDeg; }
}

// 送った (または数え始めた) ときの動きを基準として覚える
static void set_ref(BeaconSched* b) {
  b->refPosOk = b->mv; b->refLat = b->lat; b->refLon = b->lon;
  b->refCourseOk = b->hasCourse && b->flex && b->kmh >= b->flex->lowSpeed; b->refCourse = b->course;
}

uint32_t beacon_flex_interval_ms(const BeaconSched* b) {
  if (!auto_tx_is_flex(b->mode) || !b->flex) return 0;
  const FlexConfig* f = b->flex;
  uint32_t slow = (uint32_t)f->slowRateMin * 60000u, fast = (uint32_t)f->fastRateSec * 1000u;
  if (!b->mv || b->kmh < f->lowSpeed) return slow;
  if (b->kmh > f->highSpeed) return fast;
  double ms = (double)fast * f->highSpeed / b->kmh;      // 速度に反比例 (LOW〜HIGH の間)
  return ms > slow ? slow : (uint32_t)ms;
}

// 2点の距離 (m)。数km までの近い距離用 (平面で近似)
static double near_dist_m(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0, D = 3.14159265358979323846 / 180.0;
  double x = (lon2 - lon1) * D * cos((lat1 + lat2) * 0.5 * D), y = (lat2 - lat1) * D;
  return R * sqrt(x * x + y * y);
}

// フレックスで、間隔を待たずに送る条件 (曲がった・遅いときに離れた)
static bool flex_early(const BeaconSched* b, uint32_t elapsed) {
  const FlexConfig* f = b->flex;
  if (!b->mv || elapsed < (uint32_t)f->turnTimeSec * 1000u) return false;
  if (b->kmh >= f->lowSpeed) {
    if (!b->hasCourse || !b->refCourseOk) return false;
    double d = fabs(b->course - b->refCourse); while (d >= 360) d -= 360; if (d > 180) d = 360 - d;
    double th = f->turnAngle + (double)f->turnSlope * 10.0 / b->kmh;   // 遅いほど大きく曲がらないと送らない
    return d > th;
  }
  return f->slowDistM && b->refPosOk && near_dist_m(b->refLat, b->refLon, b->lat, b->lon) >= f->slowDistM;
}

void beacon_set_mode(BeaconSched* b, uint8_t mode, uint32_t nowMs) {
  if (mode == b->mode) return;
  b->mode = mode; b->lastMs = nowMs; b->waiting = false; set_ref(b);
}

void beacon_note_tx(BeaconSched* b, uint32_t nowMs) { b->lastMs = nowMs; b->waiting = false; set_ref(b); }

static uint32_t interval_ms(const BeaconSched* b) {
  return auto_tx_is_flex(b->mode) ? beacon_flex_interval_ms(b) : auto_tx_interval_ms(b->mode);
}

BeaconDecision beacon_poll(BeaconSched* b, uint32_t nowMs, bool channelBusy, bool canTx) {
  uint32_t iv = interval_ms(b);
  if (iv == 0) { b->waiting = false; return BEACON_IDLE; }
  uint32_t el = nowMs - b->lastMs;
  if (!b->waiting && el < iv && !(auto_tx_is_flex(b->mode) && flex_early(b, el))) return BEACON_IDLE;
  if (!canTx) return BEACON_IDLE;                         // 自局の送信・調整トーンが終わるまで待つ
  if (channelBusy) {
    if (!b->waiting) { b->waiting = true; b->waitSinceMs = nowMs; return BEACON_IDLE; }
    if ((uint32_t)(nowMs - b->waitSinceMs) < BEACON_BUSY_WAIT_MS) return BEACON_IDLE;
    b->lastMs = nowMs; b->waiting = false; b->skipped++; set_ref(b);   // 待ちきれない: この回は見送り、次の間隔へ
    return BEACON_SKIP_BUSY;
  }
  b->lastMs = nowMs; b->waiting = false; b->sent++;   // 基準は送信後の beacon_note_tx でも覚え直す
  set_ref(b);
  return BEACON_SEND;
}

uint32_t beacon_remaining_ms(const BeaconSched* b, uint32_t nowMs) {
  uint32_t iv = interval_ms(b);
  if (iv == 0) return 0;
  uint32_t el = nowMs - b->lastMs;
  return el >= iv ? 0 : iv - el;
}
