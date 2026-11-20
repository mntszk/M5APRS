// M5APRS.ino
// M5Stack Basic V2.6: GPS → APRS位置パケット送信 (AFSK 1200bps、DAC GPIO26 → 無線機のマイク端子、PTT出力 GPIO5)、
// 受信 (無線機のイヤホン端子 → ADC GPIO36) → 画面表示 → microSDログ。
//
// 必要なライブラリ: M5Unified (ライブラリマネージャで "M5Unified" を入れる。M5GFXも一緒に入る)
// ボード: ESP32 Arduino core 2.x (M5Stack-Core-ESP32 または "M5Stack-Core" 系)
// このフォルダの .h / .cpp は全部同じフォルダに置く。tests/ と docs/ はスケッチには含まれない。
// 局ごとの設定は microSD の /aprs/config.txt (無ければ見本を書き出す。README参照)。固定の設定は fw_config.h。
//
// 操作: 短押し A=前 / B=決定 / C=次 、長押し A=ビーコン送信 / B=戻る / C=設定メニュー
// 送信: A長押しで1パケット。設定メニューの「ビーコン自動送信」で一定間隔の自動送信もできる。PTTはソフトが自動で入れる。

#include "board_check.h"     // ボードパッケージ 3.x ならここで止める
#include <M5Unified.h>
#include <string.h>
#include <stdio.h>
#include "app_config.h"
#include "app_config_nvs.h"
#include "nmea.h"
#include "settings.h"
#include "settings_nvs.h"
#include "last_pos.h"
#include "last_pos_nvs.h"
#include "position_source.h"
#include "afsk_hw.h"
#include "packet_parse.h"
#include "logger.h"
#include "logger_sd.h"
#include "tx_compose.h"
#include "ui_core.h"
#include "ui_m5.h"
#include "diag.h"
#include "beacon_sched.h"
#include "msg_core.h"
#include "kbd_cardkb.h"
#include "state_store.h"
#include "rx_dedupe.h"

#define UI_REDRAW_MS         100     // 画面を更新する間隔 (受信状態画面のバーは、受信中もこの間隔で更新する)
#define UI_REDRAW_MAX_DEFER  1000    // 信号受信中は描画を見送る。ただし最大でこの時間まで
#define GPS_CHARS_PER_LOOP   512     // 1回のloopで読むGPSの文字数の上限

static GpsState     gps;
static SettingsMgr  settings;
static LastPosMgr   lastPos;
static Logger       logger;          // 約10KB
static UiState      ui;
static bool         hwOk = false;
static uint32_t     lastDraw = 0;
static bool         needDraw = true;
static bool         wasTxBusy = false;
static bool         hasLastTx = false;
static uint32_t     lastTxMs = 0;
static uint32_t     lastBarDraw = 0;
static BeaconSched  beacon;          // ビーコンの自動送信の時刻
static MsgCore      msgs;            // メッセージ (約3KB)
static RxDedupe     rxDupes;         // 複数のデジピーター経由で届いた同じパケットを1件にまとめる
static StateSaver   stateSaver;      // 受信一覧・WX・メッセージの保存 (再起動後に復元)
static uint8_t      stateBuf[STATE_MAX_BYTES];   // 保存する内容 (約8KB。書き終わるまで保つ)

// ---- 設定ファイル (microSD の /aprs/config.txt) ----
// 起動時に1回読む。SDが無い・読めないときは本体の控え (前回読めた中身)。ファイルが無ければ、控えか見本を書き出す。
static char cfgText[CFG_FILE_MAX];
static bool write_config_file(const char* text, size_t n) {
  return logger_sd_file_begin(LOG_DIR, CFG_FILE_NAME) && logger_sd_file_append(LOG_DIR, CFG_FILE_NAME, (const uint8_t*)text, n) &&
         logger_sd_file_commit(LOG_DIR, CFG_FILE_NAME);
}
static void load_app_config() {
  CfgStatus* st = &g_cfgStatus;
  memset(st, 0, sizeof(*st));
  long sz = logger_sd_file_size(LOG_DIR, CFG_FILE_NAME);
  if (sz == 0) { app_config_parse(&g_cfg, "", 0, st); st->source = CFG_SRC_FILE; return; }   // 空のファイル: すべて初期値
  if (sz > 0 && sz <= CFG_FILE_MAX) {
    size_t n = logger_sd_read_file(LOG_DIR, CFG_FILE_NAME, (uint8_t*)cfgText, sizeof(cfgText));
    if (n) { app_config_parse(&g_cfg, cfgText, n, st); st->source = CFG_SRC_FILE; app_config_nvs_save(cfgText, n); return; }
  }
  bool tooBig = sz > CFG_FILE_MAX;
  size_t cn = app_config_nvs_load(cfgText, sizeof(cfgText));
  if (cn) { app_config_parse(&g_cfg, cfgText, cn, st); st->source = CFG_SRC_CACHE; }
  else    { app_config_defaults(&g_cfg); st->source = CFG_SRC_DEFAULT; }
  st->tooBig = tooBig;
  if (sz == -2) {                                     // SDはあるが、ファイルが無い
    size_t wn = cn ? cn : app_config_template(cfgText, sizeof(cfgText));
    if (wn && write_config_file(cfgText, wn)) st->createdFile = true;
  }
}
static bool state_begin()  { return logger_sd_file_begin(LOG_DIR, STATE_FILE_NAME); }
static bool state_append(const uint8_t* d, size_t n) { return logger_sd_file_append(LOG_DIR, STATE_FILE_NAME, d, n); }
static bool state_commit() { return logger_sd_file_commit(LOG_DIR, STATE_FILE_NAME); }
static const StateFileOps stateOps = { state_begin, state_append, state_commit };

static LogTime current_log_time(uint32_t now) {
  LogTime t; memset(&t, 0, sizeof(t));
  GpsUtc u;
  if (gps_get_utc(&gps, now, &u)) {
    t.valid = true; t.year = u.year; t.month = u.month; t.day = u.day;
    t.hour = u.hour; t.minute = u.minute; t.second = u.second;
  }
  return t;
}

// 自局の位置 (設定の取得元に従う: GPS / 最後の位置 / 固定位置)
static void resolve_own(OwnPos* o, uint32_t now) { position_resolve(&settings, &gps, &lastPos, now, o); }
static bool resolve_valid(uint32_t now) { OwnPos o; resolve_own(&o, now); return o.valid; }

// 今の自局のコールサイン (SSID付き。メッセージの宛先の判定に使う)
static const char* my_call() {
  static char b[16];
  uint8_t ssid = settings_ssid(&settings);
  if (ssid) snprintf(b, sizeof(b), "%s-%u", g_cfg.callsign, (unsigned)ssid); else snprintf(b, sizeof(b), "%s", g_cfg.callsign);
  return b;
}

// シリアルモニタへの診断出力 (diag.cpp。115200bps)
static DiagState diagS;
static void diag_out(const char* line) { Serial.println(line); }

// 受信したパケットを、画面の一覧とSDログに入れる
static void handle_rx_packet(const QueuedPacket* qp, uint32_t now) {
  diag_rx_packet(&diagS, qp->data, qp->len, now);
  LogTime t = current_log_time(now);
  OwnPos own; resolve_own(&own, now);
  LogOwnPos lp; memset(&lp, 0, sizeof(lp));
  bool ownValid = own.valid;
  if (ownValid) { lp.valid = true; lp.latDeg = own.latDeg; lp.lonDeg = own.lonDeg; }

  static AprsPacket pkt;   // 大きいのでスタックに置かない
  if (!aprs_parse_frame(qp->data, qp->len, &pkt)) {
    logger_log_event(&logger, "rx_unparsed", "address", &t, now);
    return;
  }
  logger_log_rx(&logger, &pkt, qp->data, qp->len, &t, &lp, now);     // ログには重複も残す
  if (rx_dedupe_check(&rxDupes, qp->data, qp->len, now)) {            // 30秒以内の同じパケット: 一覧・WX・メッセージは1回だけ
    diag_note(&diagS, now, "RX duplicate (ignored)");
    return;
  }
  ui_rx_add(&ui, &pkt, ownValid, own.latDeg, own.lonDeg, &t, now);
  needDraw = true;
  // 受信音: パケット「ポッ」、自局の折り返し「ポピッ」。メッセージ・Ack なら下で替える (重複は上で除いている)
  uint8_t chime = msg_call_equal(pkt.src, my_call()) ? AFSK_CHIME_ECHO : AFSK_CHIME_PACKET;

  // メッセージ (自分宛て・一斉通知・送ったメッセージへのAck)
  MsgRxResult mr = msg_on_packet(&msgs, &pkt, my_call(), now, &t);
  char note[64];
  switch (mr) {
    case MSG_RX_NEW:
      snprintf(note, sizeof(note), L("メッセージ受信: %s"), pkt.src);
      ui_show_message(&ui, note, now, 5000);
      chime = AFSK_CHIME_MSG;
      logger_log_event(&logger, "msg_rx", pkt.src, &t, now);
      snprintf(note, sizeof(note), "MSG from %s", pkt.src); diag_note(&diagS, now, note);
      break;
    case MSG_RX_BULLETIN: chime = AFSK_CHIME_MSG; break;   // 一斉通知もメッセージの音
    case MSG_RX_ACKED: case MSG_RX_REJECTED:
      if (mr == MSG_RX_ACKED) chime = AFSK_CHIME_ACK;
      snprintf(note, sizeof(note), "%s: %s", mr == MSG_RX_ACKED ? L("Ack受信") : L("Rej受信"), pkt.src);
      ui_show_message(&ui, note, now, 4000);
      logger_log_event(&logger, mr == MSG_RX_ACKED ? "msg_acked" : "msg_rejected", pkt.src, &t, now);
      snprintf(note, sizeof(note), "MSG %s from %s", mr == MSG_RX_ACKED ? "ack" : "rej", pkt.src); diag_note(&diagS, now, note);
      break;
    default: break;
  }
  afsk_hw_chime(chime, settings.cur.chimeLevel);
}

// メッセージ・Ackの送信 (情報フィールドをそのまま送る)。送信を始められたら true
static bool send_info(const char* info, uint32_t now, const char* what) {
  static TxComposed c;
  LogTime t = current_log_time(now);
  if (tx_compose_info(&settings, info, &c) != TXC_OK || !afsk_hw_send(c.frame, c.frameLen)) {
    logger_log_event(&logger, "tx_skip", what, &t, now);
    return false;
  }
  logger_log_event(&logger, what, "sent", &t, now);
  logger_log_tx(&logger, c.frame, c.frameLen, &t, now);
  diag_tx_packet(&diagS, c.frame, c.frameLen, now);
  hasLastTx = true; lastTxMs = now;
  ui_set_tx_busy(&ui, true);
  wasTxBusy = true;
  needDraw = true;
  return true;
}

// ビーコン送信 (A長押し、または自動送信)。送信を始められたら true
static bool do_send(uint32_t now, bool isAuto) {
  LogTime t = current_log_time(now);
  static TxComposed c;     // 約500バイト
  OwnPos own; resolve_own(&own, now);
  TxComposeResult r = tx_compose(&settings, &own, &c);
  if (r == TXC_NO_CALLSIGN) {
    ui_show_message(&ui, L("コールサイン未設定のため送信しません"), now, 3000);
    logger_log_event(&logger, "tx_skip", "no_callsign", &t, now);
    if (isAuto) diag_note(&diagS, now, "AUTO TX skipped: no callsign");
    return false;
  }
  if (r == TXC_NO_POSITION) {
    ui_show_message(&ui, isAuto ? L("自動送信: 位置がないため見送り") : ui_send_refused_text(own.reason), now, 3000);
    logger_log_event(&logger, "tx_skip", isAuto ? "auto_no_position" : "no_position", &t, now);
    if (isAuto) diag_note(&diagS, now, "AUTO TX skipped: no position");
    return false;
  }
  if (r != TXC_OK) {
    ui_show_message(&ui, L("パケット作成エラー"), now, 3000);
    logger_log_event(&logger, "tx_skip", "build_error", &t, now);
    return false;
  }
  if (!afsk_hw_send(c.frame, c.frameLen)) {
    ui_show_message(&ui, L("送信を開始できません"), now, 3000);
    logger_log_event(&logger, "tx_skip", "hw_busy", &t, now);
    return false;
  }
  if (isAuto) { logger_log_event(&logger, "tx_auto", "beacon", &t, now); diag_note(&diagS, now, "AUTO TX beacon"); }
  logger_log_tx(&logger, c.frame, c.frameLen, &t, now);
  diag_tx_packet(&diagS, c.frame, c.frameLen, now);
  hasLastTx = true; lastTxMs = now;
  ui_set_tx_busy(&ui, true);
  wasTxBusy = true;
  needDraw = true;
  beacon_note_tx(&beacon, now);   // 手動・自動とも、ここから自動送信の間隔を数え直す
  return true;
}

static void fill_snapshot(UiSnapshot* s, uint32_t now) {
  memset(s, 0, sizeof(*s));
  s->settings = &settings;
  s->gps = gps_get_status(&gps, now, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC);
  OwnPos own; resolve_own(&own, now);
  if (own.valid) { s->hasPos = true; s->latDeg = own.latDeg; s->lonDeg = own.lonDeg; s->posAgeSec = own.ageSec; }
  s->posSource = own.source; s->posReason = own.reason;
  snprintf(s->posLabel, sizeof(s->posLabel), "%s", own.label);
  if (own.source == POSITION_SOURCE_FIXED && settings.cur.fixedIndex < FIXED_POSITION_COUNT)
    snprintf(s->posLabel, sizeof(s->posLabel), "%s", g_cfg.fixed[settings.cur.fixedIndex].name);   // 未設定の欄でも名前は出す
  s->lastPosValid = lastPos.cur.valid; s->lastPosLat = lastPos.cur.latDeg; s->lastPosLon = lastPos.cur.lonDeg;
  GpsUtc u;
  if (gps_get_utc(&gps, now, &u)) { s->hasUtc = true; s->utc = u; }
  s->satellites = gps.satellites;
  s->sdOk = logger_sd_ok(&logger);
  s->sdPendingBytes = logger_pending_bytes(&logger);
  // 電池: 電源ICへの問い合わせ(I2C)は5秒ごと。M5Stack Basic の IP5306 は 0/25/50/75/100% の5段階
  static uint32_t battAt = 0; static bool battRead = false; static int8_t battLv = -1; static bool battChg = false;
  if (!battRead || (uint32_t)(now - battAt) >= 5000) {
    battRead = true; battAt = now;
    int32_t lv = M5.Power.getBatteryLevel();
    battLv = (lv < 0 || lv > 100) ? -1 : (int8_t)lv;
    battChg = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  }
  s->battLevel = battLv; s->battCharging = battChg;
  afsk_hw_rx_status(&s->rx);
  s->rxCarrier = (s->rx.phase != AFSK_PHASE_IDLE);       // 自局画面の [受信中]: 信号があれば (ノイズも含む。データかどうかは受信状態画面で見る)
  s->hasLastTx = hasLastTx; s->lastTxMs = lastTxMs;
  static TxComposed c;
  if (tx_compose(&settings, &own, &c) == TXC_OK) {
    s->previewValid = true; s->previewStale = c.usedStaleFix;
    snprintf(s->previewHeader, sizeof(s->previewHeader), "%s", c.header);
    snprintf(s->previewInfo, sizeof(s->previewInfo), "%s", c.info);
  }
  AfskHwStats st = afsk_hw_stats();
  s->rxFramesOk = st.rxFramesOk; s->rxFcsErrors = st.rxFcsErrors; s->rxFragments = st.rxFragments; s->rxOverruns = st.rxOverruns;
  s->rxDiscards = st.rxDiscards; s->rxFlags = st.rxFlags;
  s->queueDropped = st.queueDropped; s->txCount = st.txCount;
  s->ptt = afsk_hw_ptt_on(); s->pttTimeouts = st.pttTimeouts;
  s->uptimeMs = now;
  s->autoTx = settings.cur.autoTx;
  s->autoRemainMs = beacon_remaining_ms(&beacon, now);
  s->autoWaiting = beacon.waiting;
  snprintf(s->sdkVersion, sizeof(s->sdkVersion), "%s", ESP.getSdkVersion());
  s->chipRev = (uint8_t)ESP.getChipRevision();
  s->freeHeap = (uint32_t)ESP.getFreeHeap();
}

// 起動時の画面のお知らせと、診断出力・動作ログ
static void report_app_config(uint32_t now) {
  const CfgStatus* st = &g_cfgStatus;
  static const char* const src[] = { "default", "file", "cache" };
  char line[96];
  snprintf(line, sizeof(line), "CFG source=%s errors=%u created=%d call=%s", src[st->source], (unsigned)st->errorCount, st->createdFile ? 1 : 0, g_cfg.callsign);
  diag_note(&diagS, now, line);
  for (int i = 0; i < st->errorCount && i < CFG_ERROR_MAX; i++) {
    snprintf(line, sizeof(line), "CFG ERROR line %u: %s %s", (unsigned)st->errors[i].line, app_config_error_text_en(st->errors[i].code), st->errors[i].key);
    diag_note(&diagS, now, line);
  }
  LogTime t = current_log_time(now);
  logger_log_event(&logger, "config", src[st->source], &t, now);
  static char note[96];
  if (st->tooBig) snprintf(note, sizeof(note), "%s", L("config.txtが大きすぎます(6KBまで)"));
  else if (st->errorCount) {
    snprintf(note, sizeof(note), L("config.txt %u行目:%s"), (unsigned)st->errors[0].line, L(app_config_error_text(st->errors[0].code)));
    if (st->errorCount > 1) { size_t k = strlen(note); snprintf(note + k, sizeof(note) - k, L(" (他%u件)"), (unsigned)(st->errorCount - 1)); }
  }
  else if (st->createdFile && st->source == CFG_SRC_DEFAULT) snprintf(note, sizeof(note), "%s", L("config.txt作成: コールサインを設定"));
  else if (st->createdFile) snprintf(note, sizeof(note), "%s", L("config.txtを控えから復元しました"));
  else if (st->source == CFG_SRC_CACHE) snprintf(note, sizeof(note), "%s", L("SDが読めないため前回の設定で動作します"));
  else return;
  ui_show_message(&ui, note, now, 10000);
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = false;     // 内蔵スピーカーはDAC(GPIO25)を使うので、M5Unifiedには触らせない
  cfg.internal_mic = false;
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  ui_m5_begin();
  load_app_config();                                  // 設定ファイル (コールサイン・送信レベルなど)。ほかの初期化より先に

  Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gps_init(&gps);
  settings_init(&settings, settings_nvs_backend());
  ui_m5_set_backlight(settings_backlight(&settings));   // 保存されていた画面の明るさ
  ui_set_lang(settings.cur.language);                   // 保存されていた画面の言語
  last_pos_init(&lastPos, last_pos_nvs_backend());
  logger_init(&logger, logger_sd_sink(), LOG_DIR);
  ui_init(&ui);

  afsk_hw_set_monitor_level(settings.cur.monitorLevel);
  diag_init(&diagS, diag_out, DIAG_SERIAL != 0);
  uint32_t now = millis();
  beacon_init(&beacon, settings.cur.autoTx, now);   // 保存されていた自動送信の設定を引き継ぐ。起動から間隔を数える
  msg_init(&msgs, (uint16_t)((uint32_t)ESP.getEfuseMac() ^ micros()));   // メッセージ番号は起動ごとに違うところから
  ui_set_msgs(&ui, &msgs);
  ui_set_my_call(&ui, my_call());
  rx_dedupe_init(&rxDupes);
  // 前回の受信一覧・WX局・メッセージを読み込む (SDがない・形式が違うときは空で始める)
  size_t sn = logger_sd_read_file(LOG_DIR, STATE_FILE_NAME, stateBuf, sizeof(stateBuf));
  StateLoadResult sr = state_restore(&ui, &msgs, stateBuf, sn, now);
  state_saver_init(&stateSaver, now);
  hwOk = afsk_hw_begin();                            // 受信の割り込みは、SDの読み込みが終わってから始める
  kbd_begin();
  diag_boot(&diagS, hwOk, now);
  LogTime t = current_log_time(now);
  logger_log_event(&logger, "boot", hwOk ? "ok" : "afsk_hw_failed", &t, now);
  { static const char* const names[] = { "ok", "none", "bad_header", "format_changed", "bad_crc" };
    logger_log_event(&logger, "state_restore", names[sr], &t, now); }
  report_app_config(now);
  if (!hwOk) ui_show_message(&ui, L("AFSK初期化エラー"), now, 600000);
}

void loop() {
  uint32_t now = millis();
  M5.update();

  // GPS
  for (int i = 0; i < GPS_CHARS_PER_LOOP && Serial2.available(); i++) gps_feed_char(&gps, (char)Serial2.read(), now);

  // 受信の復調 (ADCのサンプルは割り込みでリングバッファに入っている)
  afsk_hw_poll(now);
  static QueuedPacket qp;
  while (afsk_hw_pop_frame(&qp)) handle_rx_packet(&qp, now);

  // 診断: PTT・送信の経過 (afsk_hw のイベント)
  { AfskHwEvent ev; while (afsk_hw_pop_event(&ev)) diag_hw_event(&diagS, &ev); }

  // 調整トーン: 状態の変化を画面と動作ログに反映
  {
    bool ca = afsk_hw_calib_active();
    if (ca != ui.calibOn) {
      ui_set_calib_on(&ui, ca, now);
      needDraw = true;
      LogTime lt2 = current_log_time(now);
      logger_log_event(&logger, "calib", ca ? "start" : "stop", &lt2, now);
    }
  }

  // 送信の終了
  bool txBusy = afsk_hw_tx_busy();
  {  // PTT最大時間超過で中止したら、動作ログと画面メッセージに残す
    static uint32_t seenPttTimeouts = 0;
    uint32_t pt = afsk_hw_stats().pttTimeouts;
    if (pt != seenPttTimeouts) {
      seenPttTimeouts = pt;
      LogTime lt1 = current_log_time(now);
      logger_log_event(&logger, "ptt_timeout", "tx_aborted", &lt1, now);
      ui_show_message(&ui, L("PTT時間超過: 送信を中止"), now, 5000);
      needDraw = true;
    }
  }
  if (wasTxBusy && !txBusy) { ui_set_tx_busy(&ui, false); wasTxBusy = false; needDraw = true; }

  // ボタン
  ui_set_my_call(&ui, my_call());                 // メッセージの宛先の候補から自局を除くため (シンボル・SSIDを変えたら変わる)
  uint8_t before = ui.screen, beforeItem = ui.menuItem;
  UiAction act = ui_buttons(&ui, &settings, M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnC.isPressed(), now);
  // キーボード (CardKB / CardKB2)。メッセージ作成中だけ使う
  {
    char kc;
    if (kbd_poll(now, &kc)) {                    // 1回のloopで1文字
      UiAction ka = ui_key(&ui, kc, now);
      if (ka != UI_ACT_NONE) act = ka;
      needDraw = true;
    }
  }
  if (act == UI_ACT_SEND) do_send(now, false);   // 手動送信は他局の送信を待たない (ユーザーの判断)
  if (act == UI_ACT_CLEARED) {                   // 記録を消した (保存ファイルには次の定期保存で反映される)
    static const char* const tn[] = { "rx", "wx", "msg", "radar", "all" };
    static const char* const jn[] = { "受信一覧", "WX局", "メッセージ", "レーダーの局", "すべて" };
    uint8_t t = ui.clearDone < UI_CLEAR_COUNT ? ui.clearDone : UI_CLEAR_ALL;
    char m[48]; snprintf(m, sizeof(m), L("消去しました: %s"), L(jn[t]));
    ui_show_message(&ui, m, now, 3000);
    LogTime lc = current_log_time(now);
    logger_log_event(&logger, "clear", tn[t], &lc, now);
    needDraw = true;
  }
  if (act == UI_ACT_MSG_SEND) {                  // 作成したメッセージを登録 (送信は下の msg_poll が、他局の送信を待って行う)
    LogTime lm = current_log_time(now);
    int idx = msg_send(&msgs, ui.cmp.to, ui.cmp.text, now, &lm);
    if (idx < 0) ui_show_message(&ui, L("メッセージを作れません (宛先・本文を確認)"), now, 4000);
    else { ui_compose_done(&ui, idx); logger_log_event(&logger, "msg_queued", msgs.e[idx].to, &lm, now); }
    needDraw = true;
  }
  if (act == UI_ACT_CALIB_TOGGLE) {          // 調整トーンの開始 (停止中) / 停止 (出力中)
    if (afsk_hw_calib_active()) afsk_hw_calib_stop();
    else if (!afsk_hw_calib_start(ui.calibMode)) ui_show_message(&ui, L("調整トーンを開始できません"), now, 3000);
    needDraw = true;
  }
  if (ui.screen != before || ui.menuItem != beforeItem) needDraw = true;

  afsk_hw_set_monitor_level(settings.cur.monitorLevel);   // メニューで変えた音量を反映 (次の送信から)
  {  // 受信音の音量をメニューで変えたら、その大きさで試しに鳴らす
    static uint8_t lastChime = 255;
    if (settings.cur.chimeLevel != lastChime) {
      if (lastChime != 255 && ui.screen == UI_SCREEN_MENU) afsk_hw_chime(AFSK_CHIME_MSG, settings.cur.chimeLevel);
      lastChime = settings.cur.chimeLevel;
    }
  }
  ui_m5_set_backlight(settings_backlight(&settings));     // メニューで変えた画面の明るさをすぐ反映
  ui_set_lang(settings.cur.language);

  // メッセージの送信・送り直し・Ackの返送 (他局の送信中は止むまで待つ)
  {
    bool txNow = afsk_hw_tx_busy();
    bool channelBusy = afsk_hw_busy() && !txNow;
    bool canTx = hwOk && !txNow && !afsk_hw_calib_active() && !ui.calibOn;
    static MsgTx mt;
    MsgPoll mp = msg_poll(&msgs, now, channelBusy, canTx, auto_tx_ack_enabled(settings.cur.autoTx), &mt);
    if (mp == MSGP_SEND) {
      if (send_info(mt.info, now, mt.isAck ? "msg_ack" : "msg_tx") && !mt.isAck) {
        char n2[48]; snprintf(n2, sizeof(n2), "MSG TX to %s (%u)", mt.to, mt.entry >= 0 ? (unsigned)msgs.e[mt.entry].sends : 0u); diag_note(&diagS, now, n2);
      }
    } else if (mp == MSGP_FAILED) {
      char n2[48]; snprintf(n2, sizeof(n2), L("応答なし: %s"), mt.entry >= 0 ? msgs.e[mt.entry].to : "");
      ui_show_message(&ui, n2, now, 5000);
      LogTime lf = current_log_time(now);
      logger_log_event(&logger, "msg_failed", mt.entry >= 0 ? msgs.e[mt.entry].to : "", &lf, now);
      needDraw = true;
    } else if (mp == MSGP_SKIP_BUSY) {
      LogTime lf = current_log_time(now);
      logger_log_event(&logger, "tx_skip", "msg_channel_busy", &lf, now);
    }
  }

  // ビーコンの自動送信
  {
    static uint8_t lastAuto = 255;
    if (settings.cur.autoTx != lastAuto) {           // 設定が変わったら (起動直後も1回) 動作ログへ。数え直しは beacon_set_mode
      lastAuto = settings.cur.autoTx;
      char m[24]; uint16_t mi = auto_tx_interval_min(lastAuto);
      if (mi) snprintf(m, sizeof(m), "%umin", (unsigned)mi);
      else if (auto_tx_is_flex(lastAuto)) snprintf(m, sizeof(m), "%s", app_config_flex_ok(&g_cfg) ? "flex" : "flex_no_config");
      else snprintf(m, sizeof(m), "%s", lastAuto == AUTO_TX_ACK_ONLY ? "ack_only" : "off");
      LogTime la = current_log_time(now);
      logger_log_event(&logger, "auto_tx", m, &la, now);
      char dn[40]; snprintf(dn, sizeof(dn), "AUTO TX mode=%s", m); diag_note(&diagS, now, dn);
    }
    beacon_set_mode(&beacon, settings.cur.autoTx, now);
    // フレックス: config.txt の値と、今の動き (位置の取得元が GPS で、新しい測位に速度があるときだけ)
    beacon_set_flex(&beacon, app_config_flex_ok(&g_cfg) ? &g_cfg.flex : NULL);
    {
      GpsPosition gp; memset(&gp, 0, sizeof(gp));
      bool mv = settings.cur.positionSource == POSITION_SOURCE_GPS &&
                gps_get_status(&gps, now, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC) == GPS_FIX &&
                gps_get_position(&gps, now, FLEX_FIX_MAX_AGE_SEC, &gp) && gp.hasSpeed;
      beacon_set_motion(&beacon, mv, gp.speedKnots * 1.852, gp.hasCourse, gp.courseDeg, gp.latDeg, gp.lonDeg);
    }
    bool txNow = afsk_hw_tx_busy();
    bool channelBusy = afsk_hw_busy() && !txNow;        // 他局の信号あり
    bool canTx = hwOk && !txNow && !afsk_hw_calib_active() && !ui.calibOn;
    bool wasWaiting = beacon.waiting;
    BeaconDecision bd = beacon_poll(&beacon, now, channelBusy, canTx);
    if (bd == BEACON_SEND) do_send(now, true);
    else if (bd == BEACON_SKIP_BUSY) {
      LogTime lb = current_log_time(now);
      logger_log_event(&logger, "tx_skip", "auto_channel_busy", &lb, now);
      diag_note(&diagS, now, "AUTO TX skipped: channel busy");
      ui_show_message(&ui, L("自動送信: 他局の送信が続き見送り"), now, 5000);
    }
    if (beacon.waiting != wasWaiting) { needDraw = true; if (beacon.waiting) diag_note(&diagS, now, "AUTO TX waiting for clear channel"); }
  }

  // 設定の保存・ログの書き込みは、送信中・信号受信中でないときだけ
  bool busy = afsk_hw_busy();
  settings_commit(&settings, busy);
  {  // 「最後の位置」の保存 (現在のGPSの測位があるときだけ。頻繁には書かない。送信中・受信中は書かない)
    GpsPosition gp; double galt = 0;
    bool fix = (gps_get_status(&gps, now, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC) == GPS_FIX) &&
               gps_get_position(&gps, now, GPS_LAST_FIX_VALID_SEC, &gp);
    bool hasAlt = fix && gps_get_altitude(&gps, now, GPS_LAST_FIX_VALID_SEC, &galt);
    if (fix) last_pos_update(&lastPos, now, true, gp.latDeg, gp.lonDeg, hasAlt, galt, busy);
  }
  {  // 位置の取得元が変わったら動作ログに残す
    static uint8_t lastSrc = 255;
    if (settings.cur.positionSource != lastSrc) {
      lastSrc = settings.cur.positionSource;
      static const char* const names[] = { "gps", "last", "fixed" };
      LogTime lt0 = current_log_time(now);
      logger_log_event(&logger, "pos_source", names[lastSrc < 3 ? lastSrc : 0], &lt0, now);
    }
  }
  LogTime t = current_log_time(now);
  logger_note_gps_status(&logger, (int)gps_get_status(&gps, now, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC), &t, now);
  AfskHwStats st = afsk_hw_stats();
  logger_note_counters(&logger, st.rxOverruns, st.rxDiscards, st.queueDropped, &t, now);
  logger_service(&logger, now, busy, &t);
  {  // 受信一覧・WX・メッセージの保存。失敗は、失敗に変わったときと直ったときだけ記録する (SDなしで毎回記録しない)
    static bool stateErr = false;
    int sr = state_service(&stateSaver, &stateOps, &ui, &msgs, stateBuf, sizeof(stateBuf), now, busy);
    if (sr < 0 && !stateErr) { stateErr = true; logger_log_event(&logger, "state_save", "error", &t, now); }
    else if (sr > 0 && stateErr) { stateErr = false; logger_log_event(&logger, "state_save", "ok", &t, now); }
  }

  // 診断: 受信の状態の変化と、定期の状態 (50msごとに確認。計算が重いので毎回はしない)
  if (DIAG_SERIAL) {
    static uint32_t lastDiag = 0;
    if (now - lastDiag >= 50) {
      lastDiag = now;
      static AfskHwRxStatus drx; static DiagStatus ds;
      afsk_hw_rx_status(&drx);
      diag_rx_update(&diagS, &drx, now);
      ds.uptimeMs = now; ds.gps = (int)gps_get_status(&gps, now, GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC); ds.satellites = gps.satellites;
      ds.posSource = settings.cur.positionSource; ds.posValid = resolve_valid(now);
      ds.sdOk = logger_sd_ok(&logger); ds.sdPendingBytes = logger_pending_bytes(&logger); ds.freeHeap = ESP.getFreeHeap();
      ds.rx = drx; ds.st = st;
      diag_status(&diagS, &ds, busy, now);
    }
  }

  // 画面
  uint32_t since = now - lastDraw;
  bool rxActive = afsk_hw_busy() && !txBusy;
  static UiSnapshot snap; static UiView view;
  // 信号受信中は描き直しを見送る (最長 UI_REDRAW_MAX_DEFER)。受信状態画面を表示し続けているときは、信号が続く間ずっと見送る
  // (文字は止まるが、バーは下で約100msごとに更新する。長い調整トーンなどでサンプルを取りこぼさないように。0.13.3)
  uint32_t maxDefer = (ui.screen == UI_SCREEN_RXSTAT && view.screen == UI_SCREEN_RXSTAT) ? 0xFFFFFFFFu : (uint32_t)UI_REDRAW_MAX_DEFER;
  bool due = since >= UI_REDRAW_MS && (!rxActive || since >= maxDefer);
  if (needDraw && (!rxActive || since >= maxDefer)) due = true;
  if (due) {
    fill_snapshot(&snap, now);
    ui_render(&ui, &snap, &view, now);
    ui_m5_draw(&view);
    lastDraw = now; lastBarDraw = now;
    needDraw = false;
  } else if (rxActive && view.icons.act == UI_ACTICON_NONE && !ui.txBusy && !ui.calibOn) {
    // 受信が始まった: 本文の描き直しは見送るが、稲妻 (受信中) のアイコンだけはすぐ描く (小さいので受信を妨げない)
    UiIcons ic = view.icons; ic.act = UI_ACTICON_RX;
    ui_m5_draw_icons(&ic);
    view.icons = ic;
  } else if (rxActive && ui.screen == UI_SCREEN_RXSTAT && now - lastBarDraw >= UI_REDRAW_MS) {
    // 受信状態画面: 受信中は文字行を見送るが、バーだけは短時間で更新する (入力レベルを見ながら調整できるように)
    fill_snapshot(&snap, now);
    ui_render(&ui, &snap, &view, now);
    ui_m5_draw(&view, true);
    lastBarDraw = now;
  }
}
