// tx_compose.cpp
#include "tx_compose.h"
#include "packet_build.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static TxComposeResult frame_for(const SettingsMgr* s, const uint8_t* info, size_t il, TxComposed* out);

TxComposeResult tx_compose(const SettingsMgr* s, const OwnPos* pos, TxComposed* out) {
  memset(out, 0, sizeof(*out));
  if (!app_config_callsign_ok(&g_cfg)) return TXC_NO_CALLSIGN;
  if (!pos || !pos->valid) return TXC_NO_POSITION;

  const SymbolChoice* sym = settings_symbol(s);
  PositionReport rpt;
  memset(&rpt, 0, sizeof(rpt));
  rpt.latDeg = pos->latDeg; rpt.lonDeg = pos->lonDeg;
  rpt.ambiguity = s->cur.ambiguity;
  rpt.symTable = sym->table; rpt.symCode = sym->code;
  if (s->cur.sendCourseSpeed && pos->source == POSITION_SOURCE_GPS && pos->hasSpeed) {
    long c = pos->hasCourse ? lround(pos->courseDeg) : 0;
    long v = lround(pos->speedKnots);
    if (c >= 360 || c < 0) c = 0;
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    rpt.hasCourseSpeed = true; rpt.courseDeg = (uint16_t)c; rpt.speedKnots = (uint16_t)v;
  }
  if (s->cur.sendAltitude && pos->hasAlt) {
    rpt.hasAltitude = true; rpt.altitudeM = pos->altM;
  }
  rpt.comment = g_cfg.comment;

  uint8_t info[APRS_INFO_MAX];
  size_t il = aprs_build_position_info(rpt, APRS_DTI, info, sizeof(info));
  if (il == 0) return TXC_BUILD_ERROR;
  TxComposeResult r = frame_for(s, info, il, out);
  if (r != TXC_OK) return r;
  out->posAgeSec = pos->ageSec;
  out->source = pos->source;
  out->usedStaleFix = pos->stale;
  return TXC_OK;
}

TxComposeResult tx_compose_info(const SettingsMgr* s, const char* info, TxComposed* out) {
  memset(out, 0, sizeof(*out));
  if (!app_config_callsign_ok(&g_cfg)) return TXC_NO_CALLSIGN;
  if (!info || !info[0]) return TXC_BUILD_ERROR;
  return frame_for(s, (const uint8_t*)info, strlen(info), out);
}

static TxComposeResult frame_for(const SettingsMgr* s, const uint8_t* info, size_t il, TxComposed* out) {
  Ax25Params ap;
  ap.srcCall = g_cfg.callsign; ap.srcSsid = settings_ssid(s);
  ap.destCall = DEST_CALLSIGN; ap.destSsid = DEST_SSID;
  ap.digipath = settings_digipath(s);
  out->frameLen = ax25_build_ui_frame(ap, info, il, out->frame, sizeof(out->frame));
  if (out->frameLen == 0) return TXC_BUILD_ERROR;

  char src[16];
  uint8_t ssid = settings_ssid(s);
  if (ssid) snprintf(src, sizeof(src), "%s-%u", g_cfg.callsign, (unsigned)ssid);
  else snprintf(src, sizeof(src), "%s", g_cfg.callsign);
  if (ap.digipath[0]) snprintf(out->header, sizeof(out->header), "%s>%s,%s:", src, DEST_CALLSIGN, ap.digipath);
  else snprintf(out->header, sizeof(out->header), "%s>%s:", src, DEST_CALLSIGN);
  size_t n = il < sizeof(out->info) - 1 ? il : sizeof(out->info) - 1;
  memcpy(out->info, info, n); out->info[n] = 0;
  return TXC_OK;
}
