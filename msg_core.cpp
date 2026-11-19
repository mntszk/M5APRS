// msg_core.cpp
#include "msg_core.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const uint16_t RETRY_S[MSG_RETRY_COUNT] = MSG_RETRY_INTERVALS_S;

void msg_init(MsgCore* m, uint16_t firstNo) {
  memset(m, 0, sizeof(*m));
  m->nextNo = (uint16_t)(firstNo % 999 + 1);
}

void msg_clear(MsgCore* m) {
  memset(m->e, 0, sizeof(m->e));
  m->waiting = false; m->waitSinceMs = 0;
}

static size_t base_len(const char* s) { size_t n = 0; while (s[n] && s[n] != '-') n++; return n; }

// "nocall-0" → "NOCALL"、"nocall-7" → "NOCALL-7" (SSID 0 は付けない)
static void norm_call(const char* s, char* out, size_t cap) {
  size_t o = 0, i = 0;
  for (; s[i] && s[i] != '-' && o + 1 < cap; i++) if (s[i] != ' ') out[o++] = (char)toupper((unsigned char)s[i]);
  out[o] = 0;
  if (s[i] == '-' && s[i + 1] && strcmp(s + i + 1, "0") != 0 && o + 2 < cap) snprintf(out + o, cap - o, "-%s", s + i + 1);
}

bool msg_call_equal(const char* a, const char* b) {
  char x[16], y[16]; norm_call(a, x, sizeof(x)); norm_call(b, y, sizeof(y));
  return x[0] && !strcmp(x, y);
}

bool msg_call_base_equal(const char* a, const char* b) {
  size_t la = base_len(a), lb = base_len(b);
  if (la != lb || la == 0) return false;
  for (size_t i = 0; i < la; i++) if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return false;
  return true;
}

static void time_text(char* out, const LogTime* t) {
  if (t && t->valid) snprintf(out, 9, "%02u:%02u:%02u", t->hour % 100u, t->minute % 100u, t->second % 100u);
  else snprintf(out, 9, "--:--:--");
}

// 空きを探す。なければ、送信中でないいちばん古いものを使う
static int alloc_slot(MsgCore* m) {
  for (int i = 0; i < MSG_STORE_COUNT; i++) if (!m->e[i].used) return i;
  int best = -1;
  for (int pass = 0; pass < 2 && best < 0; pass++) {
    for (int i = 0; i < MSG_STORE_COUNT; i++) {
      if (pass == 0 && m->e[i].state == MSG_ST_SENDING) continue;
      if (best < 0 || m->e[i].seq < m->e[best].seq) best = i;
    }
  }
  return best;
}

static MsgEntry* new_entry(MsgCore* m, uint32_t nowMs, const LogTime* t) {
  int i = alloc_slot(m);
  MsgEntry* e = &m->e[i];
  memset(e, 0, sizeof(*e));
  e->used = true; e->ms = nowMs; e->utc = log_time_epoch(t); e->seq = ++m->seq; time_text(e->time, t);
  return e;
}

static void queue_ack(MsgCore* m, const char* to, const char* no) {
  for (int i = 0; i < MSG_ACK_QUEUE; i++)
    if (m->acks[i].used && msg_call_equal(m->acks[i].to, to) && !strcmp(m->acks[i].no, no)) return;   // 同じAckは1つだけ
  for (int i = 0; i < MSG_ACK_QUEUE; i++) {
    if (!m->acks[i].used) {
      m->acks[i].used = true;
      snprintf(m->acks[i].to, sizeof(m->acks[i].to), "%s", to);
      snprintf(m->acks[i].no, sizeof(m->acks[i].no), "%s", no);
      return;
    }
  }
  m->acksDropped++;                      // 待ち行列がいっぱい (相手が送り直してくれば、そのとき返す)
}

static MsgRxResult handle_ack(MsgCore* m, const char* from, const char* no, bool rej) {
  if (!no[0]) return MSG_RX_NONE;
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    if (!e->used || e->dir != MSG_DIR_OUT || strcmp(e->msgNo, no) || !msg_call_equal(e->to, from)) continue;
    if (e->state == MSG_ST_ACKED || e->state == MSG_ST_REJECTED) return MSG_RX_NONE;   // 2回目以降のAck
    e->state = rej ? MSG_ST_REJECTED : MSG_ST_ACKED;
    return rej ? MSG_RX_REJECTED : MSG_RX_ACKED;
  }
  return MSG_RX_NONE;
}

MsgRxResult msg_on_packet(MsgCore* m, const AprsPacket* p, const char* myCall, uint32_t nowMs, const LogTime* t) {
  if (!p || !p->msg.present || !myCall) return MSG_RX_NONE;
  const AprsMessage* am = &p->msg;
  if (msg_call_equal(p->src, myCall)) return MSG_RX_NONE;           // 自分の送信 (デジピーターの折り返し)

  if (!strncmp(am->addressee, "BLN", 3)) {                            // 一斉通知
    for (int i = 0; i < MSG_STORE_COUNT; i++) {
      MsgEntry* e = &m->e[i];
      if (e->used && e->dir == MSG_DIR_BLN && msg_call_equal(e->from, p->src) && !strcmp(e->to, am->addressee) && !strcmp(e->text, am->text)) {
        e->ms = nowMs; e->utc = log_time_epoch(t); e->restored = false; time_text(e->time, t); e->repeats++;            // 繰り返しの通知は1件にまとめる
        return MSG_RX_NONE;
      }
    }
    MsgEntry* e = new_entry(m, nowMs, t);
    e->dir = MSG_DIR_BLN;
    snprintf(e->from, sizeof(e->from), "%s", p->src);
    snprintf(e->to, sizeof(e->to), "%s", am->addressee);
    snprintf(e->text, sizeof(e->text), "%s", am->text);
    return MSG_RX_BULLETIN;
  }

  if (!msg_call_base_equal(am->addressee, myCall)) return MSG_RX_NONE;   // 他局宛て
  bool exact = msg_call_equal(am->addressee, myCall);

  if (am->isAck || am->isRej) return exact ? handle_ack(m, p->src, am->ackNo, am->isRej) : MSG_RX_NONE;

  if (exact && am->ackNo[0]) handle_ack(m, p->src, am->ackNo, false);   // 返信Ack形式 "{MM}AA" の AA (送ったメッセージのAck)

  // 送り直し: 同じ局・同じ番号 (番号がなければ同じ本文) のものがあれば1件にまとめる
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    if (!e->used || e->dir != MSG_DIR_IN || !msg_call_equal(e->from, p->src)) continue;
    bool same = am->msgNo[0] ? (!strcmp(e->msgNo, am->msgNo) && !strcmp(e->text, am->text))
                             : (!e->msgNo[0] && !strcmp(e->text, am->text) && (uint32_t)(nowMs - e->ms) < 600000u);
    if (!same) continue;
    e->repeats++;
    if (exact && am->msgNo[0]) queue_ack(m, p->src, am->msgNo);
    return MSG_RX_REPEAT;
  }

  MsgEntry* e = new_entry(m, nowMs, t);
  e->dir = MSG_DIR_IN; e->unread = true; e->exact = exact;
  snprintf(e->from, sizeof(e->from), "%s", p->src);
  snprintf(e->to, sizeof(e->to), "%s", am->addressee);
  snprintf(e->text, sizeof(e->text), "%s", am->text);
  snprintf(e->msgNo, sizeof(e->msgNo), "%s", am->msgNo);
  if (exact && am->msgNo[0]) queue_ack(m, p->src, am->msgNo);
  return MSG_RX_NEW;
}

size_t msg_build_info(char* out, size_t cap, const char* to, const char* text, const char* no) {
  int n;
  if (no && no[0]) n = snprintf(out, cap, ":%-9.9s:%s{%s", to, text, no);
  else n = snprintf(out, cap, ":%-9.9s:%s", to, text);
  if (n < 0) { if (cap) out[0] = 0; return 0; }
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

int msg_send(MsgCore* m, const char* to, const char* text, uint32_t nowMs, const LogTime* t) {
  if (!to || !text) return -1;
  char call[MSG_CALL_MAX]; size_t o = 0;
  for (size_t i = 0; to[i] && o < sizeof(call) - 1; i++) {
    char c = (char)toupper((unsigned char)to[i]);
    if (c == ' ') continue;
    if (!isalnum((unsigned char)c) && c != '-') return -1;
    call[o++] = c;
  }
  call[o] = 0;
  if (!call[0] || strlen(to) > 9) return -1;
  char body[APRS_MSG_TEXT_MAX + 1]; size_t b = 0;
  for (size_t i = 0; text[i] && b < APRS_MSG_TEXT_MAX; i++) {
    unsigned char c = (unsigned char)text[i];
    body[b++] = (c >= 0x20 && c <= 0x7E && c != '|' && c != '~' && c != '{') ? (char)c : '?';
  }
  body[b] = 0;
  if (!b) return -1;
  MsgEntry* e = new_entry(m, nowMs, t);
  e->dir = MSG_DIR_OUT; e->state = MSG_ST_SENDING; e->nextMs = nowMs;
  snprintf(e->to, sizeof(e->to), "%s", call);
  snprintf(e->text, sizeof(e->text), "%s", body);
  snprintf(e->msgNo, sizeof(e->msgNo), "%u", (unsigned)m->nextNo);
  m->nextNo = (uint16_t)(m->nextNo % 999 + 1);
  return (int)(e - m->e);
}

// 送り直し・Ackの返送で、次に送るもの。-2 = Ack、-1 = なし、0〜 = メッセージの添字
static int pick(MsgCore* m, uint32_t nowMs, bool ackEnabled) {
  if (!ackEnabled) { for (int i = 0; i < MSG_ACK_QUEUE; i++) if (m->acks[i].used) { m->acks[i].used = false; m->acksDropped++; } }
  for (int i = 0; i < MSG_ACK_QUEUE; i++) if (m->acks[i].used) return -2;
  int best = -1;
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    if (!e->used || e->dir != MSG_DIR_OUT || e->state != MSG_ST_SENDING) continue;
    if (e->sends > MSG_RETRY_COUNT) continue;                                   // 送り終わって応答待ち
    if ((int32_t)(nowMs - e->nextMs) < 0) continue;
    if (best < 0 || e->seq < m->e[best].seq) best = i;
  }
  return best;
}

static void after_attempt(MsgEntry* e, uint32_t nowMs) {
  e->sends++;
  if (e->sends <= MSG_RETRY_COUNT) e->nextMs = nowMs + (uint32_t)RETRY_S[e->sends - 1] * 1000u;
  else e->nextMs = nowMs + MSG_FINAL_WAIT_MS;
}

MsgPoll msg_poll(MsgCore* m, uint32_t nowMs, bool channelBusy, bool canTx, bool ackEnabled, MsgTx* out) {
  memset(out, 0, sizeof(*out)); out->entry = -1;
  // 「応答なし」の判定 (最後に送ってから MSG_FINAL_WAIT_MS)
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    if (e->used && e->dir == MSG_DIR_OUT && e->state == MSG_ST_SENDING && e->sends > MSG_RETRY_COUNT && (int32_t)(nowMs - e->nextMs) >= 0) {
      e->state = MSG_ST_FAILED; m->failed++; out->entry = i;
      return MSGP_FAILED;
    }
  }
  int k = pick(m, nowMs, ackEnabled);
  if (k == -1) { m->waiting = false; return MSGP_IDLE; }
  if (!canTx) return MSGP_IDLE;
  if (channelBusy) {
    if (!m->waiting) { m->waiting = true; m->waitSinceMs = nowMs; return MSGP_IDLE; }
    if ((uint32_t)(nowMs - m->waitSinceMs) < MSG_BUSY_WAIT_MS) return MSGP_IDLE;
    m->waiting = false;                                                  // 待ちきれない: この回は見送り
    if (k == -2) { for (int i = 0; i < MSG_ACK_QUEUE; i++) if (m->acks[i].used) { m->acks[i].used = false; m->acksDropped++; break; } }
    else after_attempt(&m->e[k], nowMs);
    return MSGP_SKIP_BUSY;
  }
  m->waiting = false;
  if (k == -2) {
    for (int i = 0; i < MSG_ACK_QUEUE; i++) {
      if (!m->acks[i].used) continue;
      out->isAck = true;
      snprintf(out->to, sizeof(out->to), "%s", m->acks[i].to);
      char a[12]; snprintf(a, sizeof(a), "ack%s", m->acks[i].no);
      msg_build_info(out->info, sizeof(out->info), m->acks[i].to, a, NULL);
      m->acks[i].used = false; m->acksSent++;
      return MSGP_SEND;
    }
  }
  MsgEntry* e = &m->e[k];
  out->entry = k;
  snprintf(out->to, sizeof(out->to), "%s", e->to);
  msg_build_info(out->info, sizeof(out->info), e->to, e->text, e->msgNo);
  after_attempt(e, nowMs);
  m->sent++;
  return MSGP_SEND;
}

int msg_order(const MsgCore* m, uint8_t out[MSG_STORE_COUNT]) {
  int n = 0;
  for (int i = 0; i < MSG_STORE_COUNT; i++) if (m->e[i].used) out[n++] = (uint8_t)i;
  for (int a = 1; a < n; a++) {
    uint8_t x = out[a]; int b = a - 1;
    while (b >= 0 && m->e[out[b]].seq < m->e[x].seq) { out[b + 1] = out[b]; b--; }
    out[b + 1] = x;
  }
  return n;
}

int msg_find_seq(const MsgCore* m, uint32_t seq) {
  for (int i = 0; i < MSG_STORE_COUNT; i++) if (m->e[i].used && m->e[i].seq == seq) return i;
  return -1;
}

int msg_unread(const MsgCore* m) {
  int n = 0;
  for (int i = 0; i < MSG_STORE_COUNT; i++) if (m->e[i].used && m->e[i].unread) n++;
  return n;
}

void msg_mark_read(MsgCore* m, int idx) { if (idx >= 0 && idx < MSG_STORE_COUNT) m->e[idx].unread = false; }

bool msg_pending(const MsgCore* m) {
  for (int i = 0; i < MSG_ACK_QUEUE; i++) if (m->acks[i].used) return true;
  for (int i = 0; i < MSG_STORE_COUNT; i++) if (m->e[i].used && m->e[i].state == MSG_ST_SENDING) return true;
  return false;
}

void msg_after_restore(MsgCore* m, uint32_t nowMs) {
  uint32_t maxSeq = 0;
  for (int i = 0; i < MSG_STORE_COUNT; i++) {
    MsgEntry* e = &m->e[i];
    if (!e->used) continue;
    e->restored = true;
    e->ms = nowMs - 0x40000000u;                    // 十分に古い時刻 (同じ内容のまとめ判定に掛からない)
    if (e->dir == MSG_DIR_OUT && e->state == MSG_ST_SENDING) e->state = MSG_ST_FAILED;   // 勝手に送り直さない
    if (e->seq > maxSeq) maxSeq = e->seq;
  }
  if (maxSeq > m->seq) m->seq = maxSeq;
  memset(m->acks, 0, sizeof(m->acks));
  m->waiting = false;
}
