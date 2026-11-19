// state_store.cpp
#include "state_store.h"
#include <string.h>

uint32_t state_crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }

// ヘッダ: magic[4] ver[2] rxSize[2] rxCount[2] wxSize[2] wxCount[2] msgSize[2] msgCount[2] stSize[2] payloadLen[4] crc[4] stCount[2] reserved[2]
static void header_expect(uint8_t* h, uint32_t payloadLen, uint32_t crc) {
  memset(h, 0, STATE_HEADER_BYTES);
  memcpy(h, STATE_MAGIC, 4);
  put16(h + 4, STATE_FORMAT_VERSION);
  put16(h + 6, (uint16_t)sizeof(UiRxEntry));  put16(h + 8, RX_LIST_COUNT);
  put16(h + 10, (uint16_t)sizeof(UiWxEntry)); put16(h + 12, WX_STATION_COUNT);
  put16(h + 14, (uint16_t)sizeof(MsgEntry));  put16(h + 16, MSG_STORE_COUNT);
  put16(h + 18, (uint16_t)sizeof(UiStation)); put16(h + 28, RADAR_STATION_COUNT);
  put32(h + 20, payloadLen);
  put32(h + 24, crc);
}

size_t state_serialize(const UiState* ui, const MsgCore* m, uint8_t* out, size_t cap) {
  if (!ui || !m || !out || cap < STATE_MAX_BYTES) return 0;
  uint8_t* p = out + STATE_HEADER_BYTES;
  put16(p, ui->rxHead); p += 2;
  put16(p, ui->rxCount); p += 2;
  put32(p, ui->rxTotal); p += 4;
  memcpy(p, ui->rx, sizeof(ui->rx)); p += sizeof(ui->rx);
  memcpy(p, ui->wx, sizeof(ui->wx)); p += sizeof(ui->wx);
  put32(p, m->seq); p += 4;
  memcpy(p, m->e, sizeof(m->e)); p += sizeof(m->e);
  memcpy(p, ui->st, sizeof(ui->st)); p += sizeof(ui->st);
  uint32_t len = (uint32_t)(p - (out + STATE_HEADER_BYTES));
  header_expect(out, len, state_crc32(out + STATE_HEADER_BYTES, len));
  return STATE_HEADER_BYTES + len;
}

uint32_t state_crc_of(const uint8_t* buf, size_t len) {
  return (buf && len >= STATE_HEADER_BYTES) ? get32(buf + 24) : 0;
}

// 文字列の終端を必ず付ける
#define TERM(a) ((a)[sizeof(a) - 1] = 0)

StateLoadResult state_restore(UiState* ui, MsgCore* m, const uint8_t* buf, size_t len, uint32_t nowMs) {
  if (!buf || len == 0) return STATE_LOAD_EMPTY;
  if (len < STATE_HEADER_BYTES || memcmp(buf, STATE_MAGIC, 4) != 0) return STATE_LOAD_BAD_HEADER;
  uint8_t want[STATE_HEADER_BYTES];
  header_expect(want, (uint32_t)STATE_PAYLOAD_BYTES, 0);
  if (memcmp(buf, want, 20) != 0 || memcmp(buf + 28, want + 28, 2) != 0 || get32(buf + 20) != STATE_PAYLOAD_BYTES || len < STATE_MAX_BYTES) return STATE_LOAD_MISMATCH;
  const uint8_t* p = buf + STATE_HEADER_BYTES;
  if (state_crc32(p, STATE_PAYLOAD_BYTES) != get32(buf + 24)) return STATE_LOAD_BAD_CRC;

  uint16_t head = get16(p), count = get16(p + 2);
  if (head >= RX_LIST_COUNT || count > RX_LIST_COUNT) return STATE_LOAD_MISMATCH;
  ui->rxHead = head; ui->rxCount = count; ui->rxTotal = get32(p + 4); p += 8;
  memcpy(ui->rx, p, sizeof(ui->rx)); p += sizeof(ui->rx);
  memcpy(ui->wx, p, sizeof(ui->wx)); p += sizeof(ui->wx);
  uint32_t seq = get32(p); p += 4;
  memcpy(m->e, p, sizeof(m->e)); p += sizeof(m->e);
  memcpy(ui->st, p, sizeof(ui->st));
  m->seq = seq;

  for (int i = 0; i < RX_LIST_COUNT; i++) {
    UiRxEntry* e = &ui->rx[i];
    TERM(e->src); TERM(e->time); TERM(e->symbol); TERM(e->comment);
  }
  for (int i = 0; i < WX_STATION_COUNT; i++) {
    UiWxEntry* e = &ui->wx[i];
    e->used = e->used ? true : false;
    TERM(e->src); TERM(e->time); TERM(e->comment);
  }
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    e->used = e->used ? true : false;
    if (e->dir > MSG_DIR_BLN || e->state > MSG_ST_FAILED) { memset(e, 0, sizeof(*e)); continue; }
    TERM(e->from); TERM(e->to); TERM(e->text); TERM(e->msgNo); TERM(e->time);
  }
  for (int i = 0; i < RADAR_STATION_COUNT; i++) {
    UiStation* e = &ui->st[i];
    e->used = e->used ? true : false;
    TERM(e->call); TERM(e->symbol);
    if (!(e->latDeg >= -90.0 && e->latDeg <= 90.0 && e->lonDeg >= -180.0 && e->lonDeg <= 180.0)) memset(e, 0, sizeof(*e));
  }
  ui_after_restore(ui, nowMs);
  msg_after_restore(m, nowMs);
  return STATE_LOAD_OK;
}

void state_saver_init(StateSaver* s, uint32_t nowMs) {
  memset(s, 0, sizeof(*s));
  s->lastCheckMs = nowMs;
}

int state_service(StateSaver* s, const StateFileOps* ops, const UiState* ui, const MsgCore* m,
                  uint8_t* buf, size_t cap, uint32_t nowMs, bool busy) {
  if (busy || !ops) return 0;
  if (!s->writing) {
    if ((uint32_t)(nowMs - s->lastCheckMs) < STATE_SAVE_INTERVAL_MS) return 0;
    s->lastCheckMs = nowMs;
    size_t n = state_serialize(ui, m, buf, cap);
    if (n == 0) return 0;
    uint32_t crc = state_crc_of(buf, n);
    if (s->haveSaved && crc == s->savedCrc) return 0;              // 変わっていない
    if (!ops->begin()) { s->writeErrors++; return -1; }
    s->writing = true; s->len = n; s->pos = 0; s->pendingCrc = crc;
    return 0;
  }
  size_t k = s->len - s->pos; if (k > STATE_WRITE_CHUNK) k = STATE_WRITE_CHUNK;
  if (!ops->append(buf + s->pos, k)) { s->writing = false; s->writeErrors++; return -1; }
  s->pos += k;
  if (s->pos < s->len) return 0;
  s->writing = false;
  if (!ops->commit()) { s->writeErrors++; return -1; }
  s->haveSaved = true; s->savedCrc = s->pendingCrc; s->writes++;
  return 1;
}
