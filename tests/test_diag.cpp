// test_diag.cpp  (パソコン上のテスト用)
#include "Arduino.h"
#include "version.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "diag.h"
#include "packet_build.h"
#include "test_cfg.h"

StubHw g_stub;
static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static std::vector<std::string> g_lines;
static void sink(const char* l) { g_lines.push_back(l); }
static bool has(const char* sub) { for (size_t i = 0; i < g_lines.size(); i++) if (strstr(g_lines[i].c_str(), sub)) return true; return false; }

static std::vector<uint8_t> frame_with_fcs(const char* info) {
  uint8_t f[AX25_FRAME_MAX];
  Ax25Params p = { "NOCALL", 9, "APRS", 0, "WIDE1-1" };
  size_t n = ax25_build_ui_frame(p, (const uint8_t*)info, strlen(info), f, sizeof(f));
  return std::vector<uint8_t>(f, f + n);
}

int main() {
  test_cfg_setup();
  DiagState d;
  // 無効のときは何も出さない
  diag_init(&d, sink, false);
  diag_boot(&d, true, 100); AfskHwEvent e0 = { 1000, AFSK_EV_PTT_ON, 0 }; diag_hw_event(&d, &e0);
  CHECK(g_lines.empty());
  diag_init(&d, NULL, true); diag_hw_event(&d, &e0); CHECK(g_lines.empty());      // 出力先なしでも落ちない

  diag_init(&d, sink, true);
  diag_boot(&d, true, 1234);
  CHECK(g_lines.size() == 3 && has("BOOT M5APRS hw=ok") && has("call=" MY_CALLSIGN) && has("ver=" FW_VERSION) && has("if=" HW_INTERFACE_REV) && has("ptt=5(active-high)") && has("ptt_delay=300/50ms") && has("amp_mark="));
  CHECK(g_lines[0].compare(0, 14, "[      1.234] ") == 0);                     // 時刻の書式
  for (size_t i = 0; i < g_lines.size(); i++) CHECK(g_lines[i].size() < DIAG_LINE_MAX);
  g_lines.clear(); diag_boot(&d, false, 5); CHECK(has("hw=FAILED")); g_lines.clear();

  // PTT・送信の経過
  { AfskHwEvent ev[] = { { 12345, AFSK_EV_PTT_ON, 0 }, { 12646, AFSK_EV_AUDIO_START, 301 }, { 13751, AFSK_EV_AUDIO_END, 1105 },
                         { 13802, AFSK_EV_PTT_OFF, 1457 }, { 20000, AFSK_EV_PTT_TIMEOUT, 10001 }, { 30000, AFSK_EV_CALIB_START, 1 },
                         { 30001, AFSK_EV_CALIB_START, 2 }, { 30002, AFSK_EV_CALIB_START, 3 }, { 40000, AFSK_EV_CALIB_STOP, 0 }, { 1, 99, 0 } };
    for (size_t i = 0; i < sizeof(ev) / sizeof(ev[0]); i++) diag_hw_event(&d, &ev[i]);
    CHECK(g_lines.size() == 9);                                                  // 未知の種類は出さない
    CHECK(g_lines[0] == "[     12.345] PTT ON");
    CHECK(g_lines[1] == "[     12.646] TX audio start (+301ms after PTT)");
    CHECK(g_lines[2] == "[     13.751] TX audio end (1105ms)");
    CHECK(g_lines[3] == "[     13.802] PTT OFF (on 1457ms)");
    CHECK(has("PTT TIMEOUT: forced off after 10001ms") && has("CALIB start: MARK 1200Hz") && has("CALIB start: SPACE 2200Hz") && has("CALIB start: ALT 1200/2200Hz") && has("CALIB stop")); }
  g_lines.clear();

  // 受信の状態: 最初の呼び出しでは出さない。変化で出す
  { AfskHwRxStatus rx; memset(&rx, 0, sizeof(rx));
    diag_rx_update(&d, &rx, 1000); CHECK(g_lines.empty());
    rx.phase = AFSK_PHASE_SIGNAL; rx.peak = 1204; rx.markAmp = 380; rx.spaceAmp = 402; rx.dcOffset = -12;
    diag_rx_update(&d, &rx, 2000); CHECK(g_lines.size() == 1 && has("RX signal") && has("peak=1204 mark=380 space=402 dc=-12 flags=0"));
    diag_rx_update(&d, &rx, 2010); CHECK(g_lines.size() == 1);                   // 変化なし
    rx.phase = AFSK_PHASE_DATA; rx.syncFlags = 3; diag_rx_update(&d, &rx, 2300); CHECK(g_lines.size() == 2 && has("RX data") && has("flags=3"));
    // 結果
    rx.lastResult = AFSK_RESULT_OK; rx.lastResultMs = 2900; rx.resultSeq = 1; rx.phase = AFSK_PHASE_IDLE;
    diag_rx_update(&d, &rx, 3000); CHECK(g_lines.size() == 4 && has("[      2.900] RX ok") && has("RX idle"));
    rx.lastResult = AFSK_RESULT_FCS_ERROR; rx.lastResultMs = 3500; rx.resultSeq = 2; diag_rx_update(&d, &rx, 3500); CHECK(has("RX fcs-error"));
    g_lines.clear();
    // 速い変化は間引く: 100ms 以内の変化は出さず、数えて次の行で知らせる
    rx.phase = AFSK_PHASE_SIGNAL; diag_rx_update(&d, &rx, 4000); CHECK(g_lines.size() == 1);
    rx.phase = AFSK_PHASE_IDLE; diag_rx_update(&d, &rx, 4020);
    rx.phase = AFSK_PHASE_SIGNAL; diag_rx_update(&d, &rx, 4040);
    rx.phase = AFSK_PHASE_IDLE; diag_rx_update(&d, &rx, 4060);
    CHECK(g_lines.size() == 1);
    rx.phase = AFSK_PHASE_SIGNAL; rx.clipped = true; diag_rx_update(&d, &rx, 4200);
    CHECK(g_lines.size() == 3 && has("3 state changes skipped") && has("CLIP")); }
  g_lines.clear();

  // パケット
  { std::vector<uint8_t> f = frame_with_fcs("!3541.00N/13946.00E>test");
    diag_tx_packet(&d, f.data(), f.size(), 7000);
    CHECK(g_lines.size() == 1 && g_lines[0] == "[      7.000] TX pkt: NOCALL-9>APRS,WIDE1-1:!3541.00N/13946.00E>test");
    diag_rx_packet(&d, f.data(), f.size() - 2, 7100);
    CHECK(g_lines.size() == 2 && g_lines[1] == "[      7.100] RX pkt: NOCALL-9>APRS,WIDE1-1:!3541.00N/13946.00E>test");
    uint8_t bad[3] = { 1, 2, 3 };
    diag_rx_packet(&d, bad, 3, 7200); diag_tx_packet(&d, bad, 1, 7300);
    CHECK(has("RX pkt: (unparsable, 3 bytes)") && has("TX pkt: (unparsable)")); }
  g_lines.clear();

  // 定期の状態: 間隔、受信中の見送り
  { DiagState s; diag_init(&s, sink, true); DiagStatus st; memset(&st, 0, sizeof(st));
    st.gps = 3; st.satellites = 9; st.posSource = 0; st.posValid = true; st.sdOk = true; st.freeHeap = 150000;
    st.st.rxFramesOk = 4; st.st.pttTimeouts = 1; st.rx.peak = 500; st.rx.dcOffset = 7;
    diag_status(&s, &st, false, 1000); CHECK(g_lines.empty());                   // 最初の間隔が過ぎるまで出さない
    diag_status(&s, &st, false, DIAG_STATUS_INTERVAL_MS + 10); CHECK(g_lines.size() == 3);
    CHECK(has("STAT up=0s gps=FIX sat=9 src=GPS pos=ok sd=ok(0B)") && has("rx_ok=4 fcs=0") && has("ptt_timeout=1 heap=150000") && has("STAT adc peak=500 mark=0 space=0 dc=7"));
    g_lines.clear();
    diag_status(&s, &st, false, DIAG_STATUS_INTERVAL_MS + 20); CHECK(g_lines.empty());
    uint32_t t1 = 2 * DIAG_STATUS_INTERVAL_MS + 100;
    diag_status(&s, &st, true, t1); CHECK(g_lines.empty());                      // 受信中は1回見送る
    diag_status(&s, &st, true, t1 + 10); CHECK(g_lines.empty());                 // 見送った直後はまだ間隔内
    diag_status(&s, &st, true, t1 + DIAG_STATUS_INTERVAL_MS + 10); CHECK(g_lines.size() == 3);   // 続けて受信中でも、2回目は出す
    st.gps = 9; st.posSource = 7; g_lines.clear();
    diag_status(&s, &st, false, 10 * DIAG_STATUS_INTERVAL_MS); CHECK(g_lines.size() == 3 && has("gps=NODATA") && has("src=GPS")); }   // 範囲外の値でも落ちない

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
