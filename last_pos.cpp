// last_pos.cpp
#include "last_pos.h"
#include "packet_parse.h"     // geo_distance_km
#include <string.h>
#include <math.h>

#define FIRST_SAVE_ALT_WAIT_MS   5000      // 高度が分かるまで待つ最大時間
#define RETRY_MS                 10000     // 書けなかったときの再試行間隔
#define SAME_PLACE_M             50.0      // 保存済みとこの距離以内なら、起動後の最初の保存は不要

void last_pos_init(LastPosMgr* m, const LastPosBackend* be) {
  memset(m, 0, sizeof(*m));
  if (be) m->be = *be;
  if (!m->be.load) return;
  LastPos p; memset(&p, 0, sizeof(p));
  if (m->be.load(m->be.ctx, &p) && pos_plausible(p.latDeg, p.lonDeg)) {
    m->cur = p; m->cur.valid = true;
    if (!(m->cur.hasAlt && isfinite(m->cur.altM))) { m->cur.hasAlt = false; m->cur.altM = 0; }
  }
}

static bool do_save(LastPosMgr* m, uint32_t nowMs, double lat, double lon, bool hasAlt, double alt) {
  LastPos p; p.valid = true; p.latDeg = lat; p.lonDeg = lon; p.hasAlt = hasAlt; p.altM = hasAlt ? alt : 0.0;
  if (m->be.save && m->be.save(m->be.ctx, &p)) {
    m->cur = p; m->savedThisBoot = true; m->lastSaveMs = nowMs; m->failed = false; m->writeCount++;
    return true;
  }
  m->writeErrors++; m->failed = true; m->failMs = nowMs;
  return false;
}

bool last_pos_update(LastPosMgr* m, uint32_t nowMs, bool fixValid, double lat, double lon,
                     bool hasAlt, double alt, bool busy) {
  if (!fixValid || !pos_plausible(lat, lon)) return false;
  if (!m->haveFirstFix) { m->haveFirstFix = true; m->firstFixMs = nowMs; }
  if (!m->be.save) return false;                                   // 保存先なし: 保存しない

  if (!m->savedThisBoot) {
    // 起動後の最初の保存。高度が分かるまで少し待つ (測位の直後は高度が来ていないことがある)
    if (!hasAlt && (uint32_t)(nowMs - m->firstFixMs) < FIRST_SAVE_ALT_WAIT_MS) return false;
    if (m->cur.valid && geo_distance_km(m->cur.latDeg, m->cur.lonDeg, lat, lon) * 1000.0 <= SAME_PLACE_M) {
      m->savedThisBoot = true; m->lastSaveMs = nowMs;               // 保存済みと同じ場所: 書かずに、動いた距離の基準にする
      return false;
    }
  } else {
    if ((uint32_t)(nowMs - m->lastSaveMs) < (uint32_t)LAST_POS_SAVE_INTERVAL_SEC * 1000u) return false;
    if (m->cur.valid && geo_distance_km(m->cur.latDeg, m->cur.lonDeg, lat, lon) * 1000.0 < LAST_POS_SAVE_DISTANCE_M) return false;
  }
  if (busy) return false;
  if (m->failed && (uint32_t)(nowMs - m->failMs) < RETRY_MS) return false;
  return do_save(m, nowMs, lat, lon, hasAlt, alt);
}
