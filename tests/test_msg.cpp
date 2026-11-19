// test_msg.cpp  (パソコン上のテスト用。スケッチには入れない)
#include "test_cfg.h"
#include "msg_core.h"
#include "packet_build.h"
#include <stdio.h>
#include <string.h>
#include <string>
static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// 受信したことにするパケット (src から info を受信)
static AprsPacket pkt(const char* src, int ssid, const char* info) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { src, (uint8_t)ssid, "APRS", 0, "WIDE1-1" };
  size_t len = ax25_build_ui_frame(p, (const uint8_t*)info, strlen(info), f, sizeof(f));
  AprsPacket a; aprs_parse_frame(f, len - 2, &a); return a;
}
static const char* ME = "NOCALL-9";

int main() {
  test_cfg_setup();
  // ---- コールサインの比較 ----
  CHECK(msg_call_equal("NOCALL-9", "nocall-9") && msg_call_equal("NOCALL", "NOCALL-0") && msg_call_equal("NOCALL-0", "NOCALL"));
  CHECK(!msg_call_equal("NOCALL-9", "NOCALL-7") && !msg_call_equal("NOCALL", "NOCALL-1") && !msg_call_equal("NOCAL", "NOCALL") && !msg_call_equal("", ""));
  CHECK(msg_call_base_equal("NOCALL-9", "NOCALL") && msg_call_base_equal("nocall-1", "NOCALL-15") && !msg_call_base_equal("NOCALL2", "NOCALL"));

  // ---- 情報フィールド ----
  { char b[100];
    CHECK(msg_build_info(b, sizeof(b), "N0CALL-7", "Hello", "12") == strlen(":N0CALL-7 :Hello{12") && !strcmp(b, ":N0CALL-7 :Hello{12"));
    msg_build_info(b, sizeof(b), "N0CALL-7", "ack12", NULL); CHECK(!strcmp(b, ":N0CALL-7 :ack12"));
    msg_build_info(b, sizeof(b), "ABCDEFGHIJK", "x", NULL); CHECK(!strcmp(b, ":ABCDEFGHI:x")); }

  // ---- 受信: 自分宛て (SSIDまで一致) → 未読、Ackを返す ----
  { MsgCore m; msg_init(&m, 0); LogTime t; memset(&t, 0, sizeof(t)); t.valid = true; t.hour = 9; t.minute = 8; t.second = 7;
    AprsPacket p = pkt("N0CALL", 7, ":NOCALL-9 :Hello 73{12");
    CHECK(msg_on_packet(&m, &p, ME, 1000, &t) == MSG_RX_NEW && msg_unread(&m) == 1);
    uint8_t o[MSG_STORE_COUNT]; CHECK(msg_order(&m, o) == 1);
    MsgEntry* e = &m.e[o[0]];
    CHECK(e->dir == MSG_DIR_IN && e->exact && !strcmp(e->from, "N0CALL-7") && !strcmp(e->text, "Hello 73") && !strcmp(e->msgNo, "12") && !strcmp(e->time, "09:08:07"));
    CHECK(msg_pending(&m));
    MsgTx tx;
    CHECK(msg_poll(&m, 1100, false, true, true, &tx) == MSGP_SEND && tx.isAck && tx.entry == -1 && !strcmp(tx.info, ":N0CALL-7 :ack12") && !strcmp(tx.to, "N0CALL-7"));
    CHECK(msg_poll(&m, 1200, false, true, true, &tx) == MSGP_IDLE && !msg_pending(&m) && m.acksSent == 1);
    // 同じ番号の送り直し: 1件にまとめ、Ackだけ返し直す
    CHECK(msg_on_packet(&m, &p, ME, 30000, &t) == MSG_RX_REPEAT && msg_order(&m, o) == 1 && m.e[o[0]].repeats == 1);
    CHECK(msg_poll(&m, 30100, false, true, true, &tx) == MSGP_SEND && tx.isAck);
    // 既読にする
    msg_mark_read(&m, o[0]); CHECK(msg_unread(&m) == 0);
    // 番号なし: Ackは返さない。同じ本文は10分以内なら1件にまとめる
    AprsPacket q = pkt("N0CALL", 7, ":NOCALL-9 :no number");
    CHECK(msg_on_packet(&m, &q, ME, 40000, &t) == MSG_RX_NEW && !msg_pending(&m));
    CHECK(msg_on_packet(&m, &q, ME, 50000, &t) == MSG_RX_REPEAT && msg_order(&m, o) == 2);
    CHECK(msg_on_packet(&m, &q, ME, 50000 + 600000, &t) == MSG_RX_NEW && msg_order(&m, o) == 3); }

  // ---- 受信: SSIDが違う自分宛て → 表示するがAckは返さない。他局宛ては無視 ----
  { MsgCore m; msg_init(&m, 0); MsgTx tx;
    AprsPacket p = pkt("N0CALL", 7, ":NOCALL-7 :to my other SSID{5");
    CHECK(msg_on_packet(&m, &p, ME, 0, NULL) == MSG_RX_NEW && !msg_pending(&m) && msg_unread(&m) == 1);
    uint8_t o[MSG_STORE_COUNT]; msg_order(&m, o); CHECK(!m.e[o[0]].exact && !strcmp(m.e[o[0]].time, "--:--:--"));
    AprsPacket p0 = pkt("N0CALL", 7, ":NOCALL   :to SSID 0{6");
    CHECK(msg_on_packet(&m, &p0, ME, 0, NULL) == MSG_RX_NEW && !msg_pending(&m));
    CHECK(msg_on_packet(&m, &p0, "NOCALL", 0, NULL) == MSG_RX_REPEAT && msg_pending(&m));   // SSIDなしの局なら一致
    CHECK(msg_poll(&m, 0, false, true, true, &tx) == MSGP_SEND && !strcmp(tx.info, ":N0CALL-7 :ack6"));
    AprsPacket x = pkt("N0CALL", 7, ":NOCAL2-9 :not for me{1");
    CHECK(msg_on_packet(&m, &x, ME, 0, NULL) == MSG_RX_NONE);
    AprsPacket me = pkt("NOCALL", 9, ":N0CALL-7 :my own echo{3");                      // 自分の送信の折り返し
    CHECK(msg_on_packet(&m, &me, ME, 0, NULL) == MSG_RX_NONE);
    AprsPacket pos = pkt("N0CALL", 7, "!3540.00N/13945.00E>");
    CHECK(msg_on_packet(&m, &pos, ME, 0, NULL) == MSG_RX_NONE); }

  // ---- Ackを返さない設定 (自動送信=オフ): 待ち行列から捨てる ----
  { MsgCore m; msg_init(&m, 0); MsgTx tx;
    AprsPacket p = pkt("N0CALL", 7, ":NOCALL-9 :hi{1"); msg_on_packet(&m, &p, ME, 0, NULL);
    CHECK(msg_poll(&m, 10, false, true, false, &tx) == MSGP_IDLE && !msg_pending(&m) && m.acksDropped == 1); }

  // ---- 一斉通知 ----
  { MsgCore m; msg_init(&m, 0);
    AprsPacket b = pkt("N0CALL", 0, ":BLN1     :Net tonight");
    CHECK(msg_on_packet(&m, &b, ME, 0, NULL) == MSG_RX_BULLETIN && msg_unread(&m) == 0 && !msg_pending(&m));
    CHECK(msg_on_packet(&m, &b, ME, 1000, NULL) == MSG_RX_NONE);                  // 同じ通知は1件
    uint8_t o[MSG_STORE_COUNT]; CHECK(msg_order(&m, o) == 1 && m.e[o[0]].dir == MSG_DIR_BLN && !strcmp(m.e[o[0]].to, "BLN1") && m.e[o[0]].repeats == 1); }

  // ---- 送信: 番号を付けて送り、間隔を延ばして送り直す。Ackで止まる ----
  { MsgCore m; msg_init(&m, 41); MsgTx tx;
    int i = msg_send(&m, "n0call-7", "Hello", 1000, NULL);
    CHECK(i >= 0 && m.e[i].state == MSG_ST_SENDING && !strcmp(m.e[i].to, "N0CALL-7") && !strcmp(m.e[i].msgNo, "42"));
    CHECK(msg_poll(&m, 1000, false, true, false, &tx) == MSGP_SEND && !tx.isAck && tx.entry == i && !strcmp(tx.info, ":N0CALL-7 :Hello{42"));   // Ack設定に関係なく送る
    CHECK(msg_poll(&m, 1000 + 29900, false, true, false, &tx) == MSGP_IDLE);
    CHECK(msg_poll(&m, 1000 + 30000, false, true, false, &tx) == MSGP_SEND && m.e[i].sends == 2);              // 30秒後に1回目の送り直し
    CHECK(msg_poll(&m, 31000 + 59900, false, true, false, &tx) == MSGP_IDLE);
    CHECK(msg_poll(&m, 31000 + 60000, false, true, false, &tx) == MSGP_SEND && m.e[i].sends == 3);
    // Ack を受けたら止まる (SSIDまで一致したときだけ)
    AprsPacket wrong = pkt("N0CALL", 7, ":NOCALL-7 :ack42"); CHECK(msg_on_packet(&m, &wrong, ME, 100000, NULL) == MSG_RX_NONE);
    AprsPacket ack = pkt("N0CALL", 7, ":NOCALL-9 :ack42");
    CHECK(msg_on_packet(&m, &ack, ME, 100000, NULL) == MSG_RX_ACKED && m.e[i].state == MSG_ST_ACKED);
    CHECK(msg_on_packet(&m, &ack, ME, 100100, NULL) == MSG_RX_NONE);              // 2回目のAckは無視
    CHECK(msg_poll(&m, 999999, false, true, false, &tx) == MSGP_IDLE && !msg_pending(&m));
    // 次の番号
    int j = msg_send(&m, "N0CALL-7", "Second", 2000000, NULL); CHECK(!strcmp(m.e[j].msgNo, "43"));
    AprsPacket rej = pkt("N0CALL", 7, ":NOCALL-9 :rej43");
    CHECK(msg_on_packet(&m, &rej, ME, 2000001, NULL) == MSG_RX_REJECTED && m.e[j].state == MSG_ST_REJECTED);
    // 返信Ack形式で、返事に Ack が付いてくる
    int k = msg_send(&m, "N0CALL-7", "Third", 3000000, NULL);
    AprsPacket rep = pkt("N0CALL", 7, ":NOCALL-9 :Reply{77}44");
    CHECK(msg_on_packet(&m, &rep, ME, 3000001, NULL) == MSG_RX_NEW && m.e[k].state == MSG_ST_ACKED && msg_unread(&m) == 1 && msg_pending(&m)); }

  // ---- 送信: 応答がなければ、最大5回送り直して「応答なし」 ----
  { MsgCore m; msg_init(&m, 0); MsgTx tx;
    int i = msg_send(&m, "N0CALL-7", "Anyone?", 0, NULL);
    uint32_t t = 0; int sends = 0; MsgPoll r = MSGP_IDLE;
    for (; t < 3600000; t += 500) { r = msg_poll(&m, t, false, true, true, &tx); if (r == MSGP_SEND) sends++; if (r == MSGP_FAILED) break; }
    CHECK(r == MSGP_FAILED && tx.entry == i && sends == 6 && m.e[i].state == MSG_ST_FAILED && m.failed == 1);
    CHECK(t >= (30u + 60 + 120 + 240 + 480) * 1000 + MSG_FINAL_WAIT_MS && t < (30u + 60 + 120 + 240 + 480) * 1000 + MSG_FINAL_WAIT_MS + 1000);
    CHECK(msg_poll(&m, t + 1000, false, true, true, &tx) == MSGP_IDLE && !msg_pending(&m)); }

  // ---- 他局の送信中は待つ。30秒で見送り (Ackは捨てる、メッセージは1回分として数える) ----
  { MsgCore m; msg_init(&m, 0); MsgTx tx;
    AprsPacket p = pkt("N0CALL", 7, ":NOCALL-9 :hi{1"); msg_on_packet(&m, &p, ME, 0, NULL);
    CHECK(msg_poll(&m, 0, true, true, true, &tx) == MSGP_IDLE && m.waiting);
    CHECK(msg_poll(&m, 10000, true, true, true, &tx) == MSGP_IDLE);
    CHECK(msg_poll(&m, 12000, false, true, true, &tx) == MSGP_SEND && tx.isAck && !m.waiting);   // 止んだら送る
    msg_on_packet(&m, &p, ME, 20000, NULL);
    msg_poll(&m, 20000, true, true, true, &tx);
    CHECK(msg_poll(&m, 20000 + MSG_BUSY_WAIT_MS, true, true, true, &tx) == MSGP_SKIP_BUSY && !msg_pending(&m) && m.acksDropped == 1);
    int i = msg_send(&m, "N0CALL-7", "x", 100000, NULL);
    msg_poll(&m, 100000, true, true, true, &tx);
    CHECK(msg_poll(&m, 100000 + MSG_BUSY_WAIT_MS, true, true, true, &tx) == MSGP_SKIP_BUSY && m.e[i].sends == 1 && m.e[i].state == MSG_ST_SENDING);
    // 自局の送信中は待つだけ
    CHECK(msg_poll(&m, 100000 + MSG_BUSY_WAIT_MS + 30000, false, false, true, &tx) == MSGP_IDLE && m.e[i].sends == 1);
    CHECK(msg_poll(&m, 100000 + MSG_BUSY_WAIT_MS + 30000, false, true, true, &tx) == MSGP_SEND && m.e[i].sends == 2); }

  // ---- 送信の入力チェック ----
  { MsgCore m; msg_init(&m, 998);
    CHECK(msg_send(&m, "", "x", 0, NULL) == -1 && msg_send(&m, "N0CALL", "", 0, NULL) == -1 && msg_send(&m, "N0 CALL!", "x", 0, NULL) == -1);
    CHECK(msg_send(&m, "TOOLONGCALL", "x", 0, NULL) == -1);
    int i = msg_send(&m, "N0CALL", "a{b|c~d\xE3", 0, NULL); CHECK(i >= 0 && !strcmp(m.e[i].text, "a?b?c?d?") && !strcmp(m.e[i].msgNo, "999"));
    std::string L(80, 'y'); int j = msg_send(&m, "N0CALL", L.c_str(), 0, NULL); CHECK(strlen(m.e[j].text) == APRS_MSG_TEXT_MAX && !strcmp(m.e[j].msgNo, "1")); }

  // ---- いっぱいになったら古いものから入れ替え (送信中のものは残す) ----
  { MsgCore m; msg_init(&m, 0);
    int keep = msg_send(&m, "N0CALL", "keep me", 0, NULL); uint32_t keepSeq = m.e[keep].seq;
    char info[40];
    for (int k = 0; k < MSG_STORE_COUNT + 5; k++) { snprintf(info, sizeof(info), ":NOCALL-9 :n%d{%d", k, k + 1); AprsPacket p = pkt("N0CALL", 7, info); msg_on_packet(&m, &p, ME, (uint32_t)k, NULL); }
    uint8_t o[MSG_STORE_COUNT]; CHECK(msg_order(&m, o) == MSG_STORE_COUNT && msg_find_seq(&m, keepSeq) == keep);
    CHECK(!strcmp(m.e[o[0]].text, "n24") && msg_find_seq(&m, 999999) == -1); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
