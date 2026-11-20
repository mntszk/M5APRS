// afsk_hw.cpp
#include "afsk_hw.h"

#include "board_check.h"     // ボードパッケージ 3.x ならここで止める (タイマーの使い方が違う)
#include <Arduino.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <string.h>

#include "app_config.h"
#include "afsk_tx_core.h"
#include "afsk_rx_core.h"
#include "afsk_ring.h"

// ---- ハードウェアの設定 (fw_config.h に無ければ、ここで既定値を決める) ----
#ifndef AFSK_TX_TIMER_NUM
#define AFSK_TX_TIMER_NUM   3          // ESP32のハードウェアタイマー番号 (0〜3)
#endif
#ifndef AFSK_RX_TIMER_NUM
#define AFSK_RX_TIMER_NUM   2
#endif
#ifndef AFSK_TIMER_DIVIDER
#define AFSK_TIMER_DIVIDER  2          // 80MHz / 2 = 40MHz。サンプル周期の誤差を0.01%以下にするため
#endif
#ifndef AFSK_RX_ADC_ATTEN
#define AFSK_RX_ADC_ATTEN   ADC_ATTEN_DB_11   // 約0〜3.1V (仮。外付け回路の出力振幅に合わせて調整する)
#endif
#ifndef AFSK_POLL_MAX_SAMPLES
#define AFSK_POLL_MAX_SAMPLES 2000     // 1回のpollで復調するサンプルの上限 (loopを長く占有しない)
#endif
#ifndef RX_DATA_SYNC_FLAGS
#define RX_DATA_SYNC_FLAGS  3          // フラグがこの数だけ連続したら「データ」(AFSKのパケット)とみなす
#endif
#ifndef PTT_PIN
#define PTT_PIN             (-1)
#endif
#ifndef PTT_MAX_ON_MS
#define PTT_MAX_ON_MS       10000
#endif
#ifndef CALIB_MAX_ON_MS
#define CALIB_MAX_ON_MS     30000      // 調整トーンを出し続ける最長時間。超えたら自動で止める
#endif
#define AFSK_ADC_MID        2048       // 12ビットADCの中心

#define TIMER_BASE_HZ       (80000000UL / AFSK_TIMER_DIVIDER)
#define RING_SIZE           ((RX_SAMPLE_RATE_HZ * RX_RINGBUF_MS) / 1000 + 1)   // 入れられる数は RING_SIZE-1

// ---- 状態 ----
static AfskTx       g_tx;
static AfskRx       g_rx;
static SampleRing   g_ring;
static int16_t      g_ringBuf[RING_SIZE];
static PacketQueue  g_queue;

static hw_timer_t*  g_txTimer = NULL;
static hw_timer_t*  g_rxTimer = NULL;
static dac_channel_t   g_dacCh = DAC_CHANNEL_1;
static dac_channel_t   g_monCh = DAC_CHANNEL_1;
static bool            g_monAvail = false;      // モニター出力のピンが使える
static volatile bool   g_monOn = false;         // この送信でモニターを出している (loopが送信開始時に決める)
static volatile int32_t g_monScale = 0;         // 0〜256。256で無線機側と同じ大きさ
static uint8_t         g_monLevel = DEFAULT_MONITOR_LEVEL;
static const uint16_t  MON_TABLE[MONITOR_LEVEL_MAX + 1] = MONITOR_SCALE_TABLE;
static adc1_channel_t  g_adcCh = ADC1_CHANNEL_0;

// 受信音 (お知らせ音)。受信の割り込みの中で、モニター側のDACに矩形波を出す。
// 音の並び (高さと長さ) を順に鳴らす。half = 半周期のサンプル数 (13200Hz / (half*2) Hz。0 は無音)
struct ChimeStep { uint8_t half; uint16_t ms; };
static const ChimeStep CH_PACKET[] = { { 8, 40 } };                                            // 825Hz
static const ChimeStep CH_ECHO[]   = { { 7, 60 }, { 4, 80 } };                                 // 943Hz → 1650Hz
static const ChimeStep CH_ACK[]    = { { 5, 80 }, { 4, 200 } };                                // 1320Hz → 1650Hz
static const ChimeStep CH_MSG[]    = { { 6, 100 }, { 0, 60 }, { 6, 100 }, { 0, 60 }, { 6, 100 } };   // 1100Hz ×3
#define CHIME_STEPS_MAX 5
#ifndef CHIME_AMP_TABLE
#define CHIME_AMP_TABLE { 0, 1, 4, 9, 20 }      // 受信音の音量ごとの振れ幅 (中心128から±。要素数は MONITOR_LEVEL_MAX + 1)
#endif
static const uint8_t CHIME_AMP[MONITOR_LEVEL_MAX + 1] = CHIME_AMP_TABLE;
struct ChimeStepS { uint8_t half; uint32_t samples; };
static ChimeStepS        g_seq[CHIME_STEPS_MAX];  // 鳴らしている並び (割り込みが読むのでRAMに写しておく)
static volatile uint8_t  g_seqNext = 0, g_seqCount = 0;
static volatile uint8_t  g_beepHalf = 0;
static volatile uint32_t g_beepLeft = 0;        // 今の音の残りのサンプル数 (割り込みが減らす)
static volatile uint8_t  g_beepPh = 0;
static volatile uint8_t  g_beepHi = 128, g_beepLo = 128;
static bool              g_beepOn = false;      // モニター側のDACを有効にしている (loop側)

static volatile bool g_txDone = false;      // 割り込みが送信の終わりを知らせる
static bool          g_txActive = false;    // loop側の送信状態
static bool          g_ready = false;

// 送信の進み方 (loop側だけが変える)。PTT_PIN が無ければ LEAD/TAIL の待ちは 0 になる。
enum PttPhase { PH_IDLE = 0, PH_LEAD, PH_TX, PH_TAIL };
static PttPhase      g_phase = PH_IDLE;
static bool          g_pttOn = false;
static uint32_t      g_pttOnMs = 0;       // PTTをオンにした時刻
static uint32_t      g_phaseMs = 0;       // 現在の段階に入った時刻 (TAILの待ち用)
static uint32_t      g_pttTimeouts = 0;
static bool          g_calib = false;           // 調整トーンの送信中
static uint8_t       g_calibMode = 0;
static volatile bool g_calibStop = false;       // 止める要求
static uint32_t      g_audioStartMs = 0;        // 音声を出し始めた時刻

// 診断用の出来事の待ち行列 (loop側だけが触る)
#define EV_QUEUE_N 16
static AfskHwEvent   g_ev[EV_QUEUE_N];
static uint8_t       g_evHead = 0, g_evCount = 0;
static void ev_push(uint8_t type, uint32_t ms, uint32_t a) {
  if (g_evCount >= EV_QUEUE_N) return;                      // あふれたら新しいものを捨てる (診断用なので)
  AfskHwEvent* e = &g_ev[(g_evHead + g_evCount) % EV_QUEUE_N];
  e->ms = ms; e->type = type; e->a = a; g_evCount++;
}
static AfskHwStats   g_stats;
static uint32_t      g_seenOverruns = 0;   // 処理済みのあふれ回数
static uint32_t      g_seenOk = 0, g_seenFcs = 0;   // 結果の変化の検出用
static AfskRxResult  g_lastResult = AFSK_RESULT_NONE;
static uint32_t      g_lastResultMs = 0, g_resultSeq = 0;

// ---- PTT出力 ----
static inline bool ptt_enabled() { return PTT_PIN >= 0; }

static void ptt_set(bool on) {
  g_pttOn = on && ptt_enabled();
  if (!ptt_enabled()) return;
  digitalWrite(PTT_PIN, (on == (g_cfg.pttActiveHigh != 0)) ? HIGH : LOW);
}

// ---- ピン → チャンネル ----
static bool dac_channel_for_pin(int pin, dac_channel_t* ch) {
  if (pin == 25) { *ch = DAC_CHANNEL_1; return true; }
  if (pin == 26) { *ch = DAC_CHANNEL_2; return true; }
  return false;
}

static bool adc1_channel_for_pin(int pin, adc1_channel_t* ch) {
  switch (pin) {
    case 36: *ch = ADC1_CHANNEL_0; return true;
    case 37: *ch = ADC1_CHANNEL_1; return true;
    case 38: *ch = ADC1_CHANNEL_2; return true;
    case 39: *ch = ADC1_CHANNEL_3; return true;
    case 32: *ch = ADC1_CHANNEL_4; return true;
    case 33: *ch = ADC1_CHANNEL_5; return true;
    case 34: *ch = ADC1_CHANNEL_6; return true;
    case 35: *ch = ADC1_CHANNEL_7; return true;
    default: return false;     // ADC2はWi-Fi/他と共用なので使わない
  }
}

// ---- 割り込み (短い処理だけ) ----
static void IRAM_ATTR txIsr() {
  if (g_txDone) return;
  uint8_t v;
  if (afsk_tx_next_sample(&g_tx, &v)) {
    dac_output_voltage(g_dacCh, v);
    if (g_monOn) dac_output_voltage(g_monCh, (uint8_t)(128 + ((((int32_t)v - 128) * g_monScale) >> 8)));
  } else {
    dac_output_voltage(g_dacCh, 128);
    if (g_monOn) dac_output_voltage(g_monCh, 128);
    g_txDone = true;
  }
}

static void IRAM_ATTR rxIsr() {
  int raw = adc1_get_raw(g_adcCh);
  if (raw < 0) raw = AFSK_ADC_MID;          // 読み取りエラーのときは無音として扱う
  ring_push(&g_ring, (int16_t)raw);
  if (g_beepLeft) {
    uint8_t h = g_beepHalf;
    if (h) { dac_output_voltage(g_monCh, g_beepPh < h ? g_beepHi : g_beepLo); if (++g_beepPh >= h * 2) g_beepPh = 0; }
    else dac_output_voltage(g_monCh, 128);
    if (--g_beepLeft == 0) {
      if (g_seqNext < g_seqCount) { const ChimeStepS* s = &g_seq[g_seqNext++]; g_beepHalf = s->half; g_beepPh = 0; g_beepLeft = s->samples; }
      else dac_output_voltage(g_monCh, 128);
    }
  }
}

static void beep_stop() {
  g_beepLeft = 0; g_seqCount = 0; g_seqNext = 0;
  if (g_beepOn) { dac_output_voltage(g_monCh, 128); dac_output_disable(g_monCh); g_beepOn = false; }
}

bool afsk_hw_chime(uint8_t kind, uint8_t level) {
  if (!g_ready || g_txActive || !g_monAvail || level == 0 || kind >= AFSK_CHIME_COUNT) return false;
  if (level > MONITOR_LEVEL_MAX) level = MONITOR_LEVEL_MAX;
  const ChimeStep* st; int n;
  switch (kind) {
    case AFSK_CHIME_PACKET: st = CH_PACKET; n = (int)(sizeof(CH_PACKET) / sizeof(CH_PACKET[0])); break;
    case AFSK_CHIME_ECHO:   st = CH_ECHO;   n = (int)(sizeof(CH_ECHO) / sizeof(CH_ECHO[0])); break;
    case AFSK_CHIME_ACK:    st = CH_ACK;    n = (int)(sizeof(CH_ACK) / sizeof(CH_ACK[0])); break;
    default:                st = CH_MSG;    n = (int)(sizeof(CH_MSG) / sizeof(CH_MSG[0])); break;
  }
  g_beepLeft = 0;                                       // 鳴っている途中なら止めてから入れ替える (割り込みは g_beepLeft が 0 なら何もしない)
  int32_t amp = CHIME_AMP[level];
  g_beepHi = (uint8_t)(128 + amp); g_beepLo = (uint8_t)(128 - amp);
  for (int i = 0; i < n && i < CHIME_STEPS_MAX; i++) { g_seq[i].half = st[i].half; g_seq[i].samples = (uint32_t)st[i].ms * RX_SAMPLE_RATE_HZ / 1000u; }
  g_seqCount = (uint8_t)(n < CHIME_STEPS_MAX ? n : CHIME_STEPS_MAX); g_seqNext = 1;
  if (!g_beepOn) { dac_output_voltage(g_monCh, 128); dac_output_enable(g_monCh); g_beepOn = true; }
  g_beepHalf = g_seq[0].half; g_beepPh = 0;
  g_beepLeft = g_seq[0].samples;                        // 最後に入れる (これで割り込みが鳴らし始める)
  return true;
}

// ---- 受信の開始・停止 (loop側から) ----
static void rx_start() {
  ring_flush(&g_ring);
  afsk_rx_reset(&g_rx);
  timerWrite(g_rxTimer, 0);
  timerAlarmEnable(g_rxTimer);
}

static void rx_stop() {
  timerAlarmDisable(g_rxTimer);
}

bool afsk_hw_begin() {
  g_ready = false;
  if (ptt_enabled()) { pinMode(PTT_PIN, OUTPUT); ptt_set(false); }   // 何より先に、PTTをオフで確定させる
  g_phase = PH_IDLE; g_pttTimeouts = 0;
  if (!dac_channel_for_pin(AFSK_TX_DAC_PIN, &g_dacCh)) return false;
  if (!adc1_channel_for_pin(AFSK_RX_ADC_PIN, &g_adcCh)) return false;
  // モニター出力は、使えなくても本体の動作は止めない
  g_monAvail = (AFSK_TX_MONITOR_PIN != 0) && dac_channel_for_pin(AFSK_TX_MONITOR_PIN, &g_monCh) && (g_monCh != g_dacCh);
  g_monOn = false;
  afsk_hw_set_monitor_level(g_monLevel);

  AfskTxConfig tc; memset(&tc, 0, sizeof(tc));
  tc.sampleRateHz = TX_SAMPLE_RATE_HZ; tc.baud = AFSK_BAUD; tc.markHz = AFSK_MARK_HZ; tc.spaceHz = AFSK_SPACE_HZ;
  tc.ampMark = g_cfg.txAmpMark; tc.ampSpace = g_cfg.txAmpSpace; tc.preambleFlags = g_cfg.txPreambleFlags; tc.tailFlags = TX_TAIL_FLAGS;
  if (!afsk_tx_init(&g_tx, &tc)) return false;

  AfskRxConfig rc; memset(&rc, 0, sizeof(rc));
  rc.sampleRateHz = RX_SAMPLE_RATE_HZ; rc.baud = AFSK_BAUD; rc.markHz = AFSK_MARK_HZ; rc.spaceHz = AFSK_SPACE_HZ;
  rc.toneLevelInit = RX_TONE_LEVEL_INIT;
  if (!afsk_rx_init(&g_rx, &rc)) return false;

  ring_init(&g_ring, g_ringBuf, RING_SIZE);
  g_seenOverruns = 0;
  g_seenOk = g_seenFcs = 0; g_lastResult = AFSK_RESULT_NONE; g_lastResultMs = 0; g_resultSeq = 0;
  pq_init(&g_queue, RX_PACKET_QUEUE_COUNT);
  memset(&g_stats, 0, sizeof(g_stats));

  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) return false;
  if (adc1_config_channel_atten(g_adcCh, AFSK_RX_ADC_ATTEN) != ESP_OK) return false;

  g_txTimer = timerBegin(AFSK_TX_TIMER_NUM, AFSK_TIMER_DIVIDER, true);
  g_rxTimer = timerBegin(AFSK_RX_TIMER_NUM, AFSK_TIMER_DIVIDER, true);
  if (!g_txTimer || !g_rxTimer) return false;
  timerAttachInterrupt(g_txTimer, &txIsr, true);
  timerAttachInterrupt(g_rxTimer, &rxIsr, true);
  timerAlarmWrite(g_txTimer, (TIMER_BASE_HZ + TX_SAMPLE_RATE_HZ / 2) / TX_SAMPLE_RATE_HZ, true);
  timerAlarmWrite(g_rxTimer, (TIMER_BASE_HZ + RX_SAMPLE_RATE_HZ / 2) / RX_SAMPLE_RATE_HZ, true);

  g_txActive = false; g_txDone = false;
  g_ready = true;
  rx_start();
  return true;
}

// 音声(通常のフレーム、または連続トーン)を用意して afsk_tx_start / afsk_tx_start_tone を呼んだあとの、共通の開始処理
static void tx_begin(bool calib) {
  rx_stop();
  beep_stop();                                               // お知らせ音は止める (モニター側のDACは送信が使う)
  dac_output_voltage(g_dacCh, 128);                          // 有効にする前に中心値にしておく (段差を出さない)
  dac_output_enable(g_dacCh);
  g_monOn = g_monAvail && g_monScale > 0;                    // 音量ミュートなら、モニター側のDACは動かさない
  if (g_monOn) { g_monOn = true; dac_output_voltage(g_monCh, 128); dac_output_enable(g_monCh); }
  g_txDone = false;
  g_txActive = true;
  g_calib = calib; g_calibStop = false;
  // DACを中心値にして落ち着かせてからPTTを入れる。音声はPTT_ON_DELAY_MS 後に出し始める
  ptt_set(true);
  g_pttOnMs = g_phaseMs = (uint32_t)millis();
  if (g_pttOn) ev_push(AFSK_EV_PTT_ON, g_pttOnMs, 0);
  if (!ptt_enabled() || g_cfg.pttOnDelayMs == 0) {
    g_phase = PH_TX;
    g_audioStartMs = g_pttOnMs;
    ev_push(AFSK_EV_AUDIO_START, g_pttOnMs, 0);
    timerWrite(g_txTimer, 0);
    timerAlarmEnable(g_txTimer);
  } else {
    g_phase = PH_LEAD;
  }
}

bool afsk_hw_send(const uint8_t* frame, size_t len) {
  if (!g_ready || g_txActive || !frame || len == 0) return false;
  if (!afsk_tx_prepare(&g_tx, frame, len)) return false;     // 長すぎる等。受信はそのまま続ける
  afsk_tx_start(&g_tx);
  tx_begin(false);
  return true;
}

bool afsk_hw_calib_start(uint8_t mode) {
  if (!g_ready || g_txActive || mode < AFSK_TONE_MARK || mode > AFSK_TONE_ALT) return false;
  afsk_tx_start_tone(&g_tx, mode);
  if (!afsk_tx_is_active(&g_tx)) return false;
  g_calibMode = mode;
  tx_begin(true);
  ev_push(AFSK_EV_CALIB_START, g_pttOnMs, mode);
  return true;
}

void afsk_hw_calib_stop() { if (g_calib) g_calibStop = true; }
bool afsk_hw_calib_active() { return g_calib && g_txActive; }
uint8_t afsk_hw_calib_mode() { return g_calibMode; }

bool afsk_hw_pop_event(AfskHwEvent* out) {
  if (!g_evCount) return false;
  *out = g_ev[g_evHead]; g_evHead = (uint8_t)((g_evHead + 1) % EV_QUEUE_N); g_evCount--;
  return true;
}

// 送信の終了処理 (正常終了・中止の共通)。PTTを先にオフにしてから、DACを止める (キーが入ったままDACを切って音を出さないため)
static void tx_finish(uint32_t nowMs) {
  bool wasOn = g_pttOn;
  ptt_set(false);
  if (wasOn) ev_push(AFSK_EV_PTT_OFF, nowMs, (uint32_t)(nowMs - g_pttOnMs));
  if (g_calib) ev_push(AFSK_EV_CALIB_STOP, nowMs, 0);
  dac_output_disable(g_dacCh);
  if (g_monOn) { g_monOn = false; dac_output_disable(g_monCh); }
  g_txActive = false;
  g_calib = false; g_calibStop = false;
  g_phase = PH_IDLE;
  rx_start();
}

void afsk_hw_poll(uint32_t nowMs) {
  if (g_beepOn && g_beepLeft == 0 && !g_txActive) beep_stop();   // お知らせ音が終わったらモニター側のDACを止める
  if (!g_ready) return;

  if (g_txActive) {
    // 保険: PTTを入れてから最大時間を超えたら、送信を中止して強制オフ (調整トーンは、自動停止より少し長く待つ)
    const uint32_t maxOn = g_calib ? (uint32_t)CALIB_MAX_ON_MS + 2000u : (uint32_t)PTT_MAX_ON_MS;
    if (g_pttOn && (uint32_t)(nowMs - g_pttOnMs) > maxOn) {
      timerAlarmDisable(g_txTimer);
      afsk_tx_abort(&g_tx);
      dac_output_voltage(g_dacCh, 128);
      if (g_monOn) dac_output_voltage(g_monCh, 128);
      g_pttTimeouts++;
      ev_push(AFSK_EV_PTT_TIMEOUT, nowMs, (uint32_t)(nowMs - g_pttOnMs));
      tx_finish(nowMs);
      return;
    }
    // 調整トーン: 止める要求、または自動停止の時間で、音を止めて終了処理へ
    if (g_calib && (g_phase == PH_LEAD || g_phase == PH_TX) &&
        (g_calibStop || (uint32_t)(nowMs - g_pttOnMs) >= (uint32_t)CALIB_MAX_ON_MS)) {
      timerAlarmDisable(g_txTimer);
      afsk_tx_abort(&g_tx);
      dac_output_voltage(g_dacCh, 128);
      if (g_monOn) dac_output_voltage(g_monCh, 128);
      if (g_phase == PH_TX) ev_push(AFSK_EV_AUDIO_END, nowMs, (uint32_t)(nowMs - g_audioStartMs));
      g_calibStop = false;
      g_phase = PH_TAIL; g_phaseMs = nowMs;
    }
    switch (g_phase) {
      case PH_LEAD:                                          // PTTオン後の待ち
        if ((uint32_t)(nowMs - g_phaseMs) >= (uint32_t)g_cfg.pttOnDelayMs) {
          g_phase = PH_TX;
          g_audioStartMs = nowMs;
          ev_push(AFSK_EV_AUDIO_START, nowMs, (uint32_t)(nowMs - g_pttOnMs));
          timerWrite(g_txTimer, 0);
          timerAlarmEnable(g_txTimer);
        }
        break;
      case PH_TX:
        if (g_txDone) {                                      // 音声を出し終えた。少し待ってからPTTオフ
          timerAlarmDisable(g_txTimer);
          ev_push(AFSK_EV_AUDIO_END, nowMs, (uint32_t)(nowMs - g_audioStartMs));
          g_phase = PH_TAIL; g_phaseMs = nowMs;
          if (!ptt_enabled() || g_cfg.pttOffDelayMs == 0) { g_stats.txCount++; tx_finish(nowMs); }
        }
        break;
      case PH_TAIL:
        if ((uint32_t)(nowMs - g_phaseMs) >= (uint32_t)g_cfg.pttOffDelayMs) { if (!g_calib) g_stats.txCount++; tx_finish(nowMs); }
        break;
      default:
        break;
    }
    return;                                                  // 送信中は復調しない
  }

  // リングバッファがあふれていたら、サンプルが欠けているので、途中のフレームを捨ててフラグ検出からやり直す
  uint32_t ov = g_ring.overruns;
  if (ov != g_seenOverruns) {
    g_seenOverruns = ov;
    afsk_rx_discard_frame(&g_rx);
    g_stats.rxDiscards++;
  }

  int n = 0;
  int16_t raw;
  while (n < AFSK_POLL_MAX_SAMPLES && ring_pop(&g_ring, &raw)) {
    n++;
    if (afsk_rx_process(&g_rx, (int16_t)(raw - AFSK_ADC_MID))) {
      size_t flen; const uint8_t* f = afsk_rx_frame(&g_rx, &flen);
      pq_push(&g_queue, f, flen, nowMs);
    }
  }
  g_stats.rxSamples += (uint32_t)n;

  // 完成したフレームの結果 (OK / FCS不良) を記録する
  if (g_rx.framesOk != g_seenOk || g_rx.fcsErrors != g_seenFcs) {
    g_lastResult = (g_rx.framesOk != g_seenOk) ? AFSK_RESULT_OK : AFSK_RESULT_FCS_ERROR;
    g_lastResultMs = nowMs; g_resultSeq++;
    g_seenOk = g_rx.framesOk; g_seenFcs = g_rx.fcsErrors;
  }
}

void afsk_hw_set_monitor_level(uint8_t level) {
  if (level > MONITOR_LEVEL_MAX) level = MONITOR_LEVEL_MAX;
  g_monLevel = level;
  if (!g_txActive) g_monScale = MON_TABLE[level];            // 送信中は変えない (波形が途中で変わらないように)
}

uint8_t afsk_hw_monitor_level() { return g_monLevel; }

bool afsk_hw_tx_busy() { return g_txActive; }

bool afsk_hw_ptt_on() { return g_pttOn; }

bool afsk_hw_busy() { return g_txActive || afsk_rx_carrier(&g_rx); }

bool afsk_hw_pop_frame(QueuedPacket* out) { return pq_pop(&g_queue, out); }

AfskHwStats afsk_hw_stats() {
  AfskHwStats s = g_stats;
  s.rxOverruns = g_ring.overruns;
  s.rxFramesOk = g_rx.framesOk;
  s.rxFcsErrors = g_rx.fcsErrors;
  s.rxFragments = g_rx.fragments;
  s.queueDropped = g_queue.dropped;
  s.rxFlags = g_rx.flagsSeen;
  s.pttTimeouts = g_pttTimeouts;
  return s;
}

void afsk_hw_rx_status(AfskHwRxStatus* o) {
  memset(o, 0, sizeof(*o));
  o->lastResult = g_lastResult; o->lastResultMs = g_lastResultMs; o->resultSeq = g_resultSeq;
  if (!g_ready) return;
  { AfskRxMonitor m0; afsk_rx_monitor(&g_rx, &m0); o->lastOkMarkAmp = m0.lastOkMarkAmp; o->lastOkSpaceAmp = m0.lastOkSpaceAmp; }
  if (g_txActive) return;                        // 送信中は受信していない
  AfskRxMonitor m;
  afsk_rx_monitor(&g_rx, &m);
  o->peak = m.peak; o->clipped = m.clipped; o->dcOffset = m.dcOffset; o->markAmp = m.markAmp; o->spaceAmp = m.spaceAmp;
  o->markAvgAmp = m.markAvgAmp; o->spaceAvgAmp = m.spaceAvgAmp;
  o->syncFlags = m.syncFlags; o->frameBytes = m.frameBytes;
  if (m.carrier) o->phase = (m.syncFlags >= RX_DATA_SYNC_FLAGS) ? AFSK_PHASE_DATA : AFSK_PHASE_SIGNAL;
  else o->phase = AFSK_PHASE_IDLE;
}
