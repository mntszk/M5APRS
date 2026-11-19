// diag.cpp
#include "diag.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "packet_parse.h"

static void emit(DiagState* d, uint32_t ms, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void emit(DiagState* d, uint32_t ms, const char* fmt, ...) {
  if (!d || !d->enabled || !d->out) return;
  char body[DIAG_LINE_MAX - 16];
  va_list ap; va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  char line[DIAG_LINE_MAX];
  snprintf(line, sizeof(line), "[%7lu.%03lu] %s", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u), body);
  d->out(line);
}

void diag_init(DiagState* d, DiagOut out, bool enabled) {
  memset(d, 0, sizeof(*d));
  d->out = out; d->enabled = enabled;
  d->lastPhase = AFSK_PHASE_IDLE;
}

void diag_boot(DiagState* d, bool hwOk, uint32_t nowMs) {
  emit(d, nowMs, "BOOT M5APRS hw=%s call=%s ver=%s build=%s %s if=%s", hwOk ? "ok" : "FAILED", g_cfg.callsign,
       FW_VERSION, FW_BUILD_DATE, FW_BUILD_TIME, HW_INTERFACE_REV);
  emit(d, nowMs, "CFG tx_dac=%d mon=%d rx_adc=%d ptt=%d(%s) ptt_delay=%d/%dms ptt_max=%dms",
       (int)AFSK_TX_DAC_PIN, (int)AFSK_TX_MONITOR_PIN, (int)AFSK_RX_ADC_PIN, (int)PTT_PIN,
       g_cfg.pttActiveHigh ? "active-high" : "active-low", (int)g_cfg.pttOnDelayMs, (int)g_cfg.pttOffDelayMs, (int)PTT_MAX_ON_MS);
  emit(d, nowMs, "CFG tx_rate=%d rx_rate=%d amp_mark=%d amp_space=%d preamble=%d tail=%d",
       (int)TX_SAMPLE_RATE_HZ, (int)RX_SAMPLE_RATE_HZ, (int)g_cfg.txAmpMark, (int)g_cfg.txAmpSpace, (int)g_cfg.txPreambleFlags, (int)TX_TAIL_FLAGS);
}

void diag_note(DiagState* d, uint32_t nowMs, const char* text) { emit(d, nowMs, "%s", text ? text : ""); }

void diag_hw_event(DiagState* d, const AfskHwEvent* e) {
  switch (e->type) {
    case AFSK_EV_PTT_ON:       emit(d, e->ms, "PTT ON"); break;
    case AFSK_EV_AUDIO_START:  emit(d, e->ms, "TX audio start (+%lums after PTT)", (unsigned long)e->a); break;
    case AFSK_EV_AUDIO_END:    emit(d, e->ms, "TX audio end (%lums)", (unsigned long)e->a); break;
    case AFSK_EV_PTT_OFF:      emit(d, e->ms, "PTT OFF (on %lums)", (unsigned long)e->a); break;
    case AFSK_EV_PTT_TIMEOUT:  emit(d, e->ms, "PTT TIMEOUT: forced off after %lums, TX aborted", (unsigned long)e->a); break;
    case AFSK_EV_CALIB_START: {
      const char* m = (e->a == 1) ? "MARK 1200Hz" : (e->a == 2) ? "SPACE 2200Hz" : "ALT 1200/2200Hz";
      emit(d, e->ms, "CALIB start: %s", m); break; }
    case AFSK_EV_CALIB_STOP:   emit(d, e->ms, "CALIB stop"); break;
    default: break;
  }
}

void diag_rx_update(DiagState* d, const AfskHwRxStatus* rx, uint32_t nowMs) {
  if (!d || !d->enabled) return;
  // 受信結果 (OK / FCS不良)
  if (rx->resultSeq != d->lastResultSeq) {
    d->lastResultSeq = rx->resultSeq;
    emit(d, rx->lastResultMs, "RX %s", rx->lastResult == AFSK_RESULT_OK ? "ok" : "fcs-error");
  }
  // 状態の変化。ノイズでスケルチが開閉するときに出しすぎないよう、間引く
  if (!d->phaseInit) { d->phaseInit = true; d->lastPhase = rx->phase; d->lastPhaseLineMs = nowMs - DIAG_RX_MIN_INTERVAL_MS; return; }
  if (rx->phase == d->lastPhase) return;
  if ((uint32_t)(nowMs - d->lastPhaseLineMs) < DIAG_RX_MIN_INTERVAL_MS) { d->skipped++; d->lastPhase = rx->phase; return; }
  const char* name = rx->phase == AFSK_PHASE_DATA ? "data  " : rx->phase == AFSK_PHASE_SIGNAL ? "signal" : "idle  ";
  if (rx->phase == AFSK_PHASE_IDLE) emit(d, nowMs, "RX %s%s", name, "");
  else emit(d, nowMs, "RX %s peak=%u mark=%u space=%u dc=%d flags=%u%s", name, (unsigned)rx->peak, (unsigned)rx->markAmp, (unsigned)rx->spaceAmp,
            (int)rx->dcOffset, (unsigned)rx->syncFlags, rx->clipped ? " CLIP" : "");
  if (d->skipped) { emit(d, nowMs, "RX (%lu state changes skipped)", (unsigned long)d->skipped); d->skipped = 0; }
  d->lastPhase = rx->phase; d->lastPhaseLineMs = nowMs;
}

void diag_tx_packet(DiagState* d, const uint8_t* frame, size_t len, uint32_t nowMs) {
  if (!d || !d->enabled) return;
  char raw[256];
  if (len < 2 || aprs_tnc2_string(frame, len - 2, raw, sizeof(raw)) == 0) emit(d, nowMs, "TX pkt: (unparsable)");
  else emit(d, nowMs, "TX pkt: %s", raw);
}

void diag_rx_packet(DiagState* d, const uint8_t* frame, size_t len, uint32_t nowMs) {
  if (!d || !d->enabled) return;
  char raw[256];
  if (aprs_tnc2_string(frame, len, raw, sizeof(raw)) == 0) emit(d, nowMs, "RX pkt: (unparsable, %u bytes)", (unsigned)len);
  else emit(d, nowMs, "RX pkt: %s", raw);
}

void diag_status(DiagState* d, const DiagStatus* s, bool busy, uint32_t nowMs) {
  if (!d || !d->enabled || DIAG_STATUS_INTERVAL_MS == 0) return;
  if ((uint32_t)(nowMs - d->lastStatusMs) < (uint32_t)DIAG_STATUS_INTERVAL_MS) return;
  if (busy && d->lastStatusDue < 1) { d->lastStatusDue++; d->lastStatusMs = nowMs; return; }     // 受信中は1回見送る
  d->lastStatusDue = 0; d->lastStatusMs = nowMs;
  static const char* const gps[] = { "NODATA", "NOFIX", "STALE", "FIX" };
  static const char* const src[] = { "GPS", "LAST", "FIXED" };
  emit(d, nowMs, "STAT up=%lus gps=%s sat=%u src=%s pos=%s sd=%s(%luB)",
       (unsigned long)(s->uptimeMs / 1000u), gps[(s->gps >= 0 && s->gps < 4) ? s->gps : 0], (unsigned)s->satellites,
       src[s->posSource < 3 ? s->posSource : 0], s->posValid ? "ok" : "none", s->sdOk ? "ok" : "ERR", (unsigned long)s->sdPendingBytes);
  emit(d, nowMs, "STAT rx_ok=%lu fcs=%lu frag=%lu flags=%lu overrun=%lu discard=%lu qdrop=%lu tx=%lu ptt_timeout=%lu heap=%lu",
       (unsigned long)s->st.rxFramesOk, (unsigned long)s->st.rxFcsErrors, (unsigned long)s->st.rxFragments, (unsigned long)s->st.rxFlags, (unsigned long)s->st.rxOverruns,
       (unsigned long)s->st.rxDiscards, (unsigned long)s->st.queueDropped, (unsigned long)s->st.txCount, (unsigned long)s->st.pttTimeouts,
       (unsigned long)s->freeHeap);
  emit(d, nowMs, "STAT adc peak=%u mark=%u space=%u dc=%d%s", (unsigned)s->rx.peak, (unsigned)s->rx.markAmp, (unsigned)s->rx.spaceAmp, (int)s->rx.dcOffset, s->rx.clipped ? " CLIP" : "");
}
