// position_source.cpp
#include "position_source.h"
#include <string.h>
#include <stdio.h>

void position_resolve(const SettingsMgr* s, const GpsState* g, const LastPosMgr* lp, uint32_t nowMs, OwnPos* o) {
  memset(o, 0, sizeof(*o));
  o->source = s->cur.positionSource;
  switch (o->source) {
    case POSITION_SOURCE_LAST: {
      if (!lp || !lp->cur.valid) { o->reason = POS_LAST_NONE; return; }
      o->valid = true; o->latDeg = lp->cur.latDeg; o->lonDeg = lp->cur.lonDeg;
      o->hasAlt = lp->cur.hasAlt; o->altM = lp->cur.altM;
      return; }
    case POSITION_SOURCE_FIXED: {
      int i = s->cur.fixedIndex;
      if (!fixed_position_set(i)) { o->reason = POS_FIXED_UNSET; return; }
      const FixedPosition* f = &g_cfg.fixed[i];
      o->valid = true; o->latDeg = f->latDeg; o->lonDeg = f->lonDeg; o->hasAlt = f->hasAlt; o->altM = f->hasAlt ? f->altM : 0.0;
      snprintf(o->label, sizeof(o->label), "%s", f->name);
      return; }
    default: {
      GpsPosition p;
      if (!gps_get_position(g, nowMs, GPS_LAST_FIX_VALID_SEC, &p)) {
        o->reason = (gps_get_status(g, nowMs, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC) == GPS_NO_DATA) ? POS_GPS_NO_DATA : POS_GPS_NO_FIX;
        return;
      }
      o->valid = true; o->latDeg = p.latDeg; o->lonDeg = p.lonDeg; o->ageSec = p.ageSec;
      o->hasCourse = p.hasCourse; o->courseDeg = p.courseDeg; o->hasSpeed = p.hasSpeed; o->speedKnots = p.speedKnots;
      double alt; if (gps_get_altitude(g, nowMs, GPS_LAST_FIX_VALID_SEC, &alt)) { o->hasAlt = true; o->altM = alt; }
      o->stale = (gps_get_status(g, nowMs, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC) != GPS_FIX);
      return; }
  }
}
