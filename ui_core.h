// ui_core.h
// 画面とボタンの動作(状態遷移)と、表示する文字列の組み立て。描画そのものは ui_m5.cpp。
// M5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
//
// ボタン (仕様4.5):
//   短押し = その画面の中の操作   A=前  B=決定(開く/切り替え/開始)  C=次
//   長押し = どこでも同じ働き     A=ビーコン送信  B=戻る(1つ上へ)  C=設定メニュー(開く/閉じる)
//   通常画面  A短=前の画面  C短=次の画面  A長=送信  C長=設定メニュー
//   設定メニュー  A=前の項目  C=次の項目  B短=値の変更  B長/C長=閉じる
//   調整トーン  A/C=モード  B短=開始  B長=メニューへ戻る (出力中はどのボタンでも停止)
// 送信中 (ui_set_tx_busy(true)) は、ボタンを無視する。
//
// 表示: 1行16ピクセルの日本語フォント(半角8px)で、幅320pxなら半角40桁。
//   UiView の lines[0] はヘッダ(画面名)、icons はヘッダ右側の状態アイコン、
//   keyShort/keyLong は画面最下段の枠で囲んだボタン表示 (上段=短押し、下段=長押し。空なら「-」)。

#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "app_config.h"
#include "settings.h"
#include "nmea.h"
#include "packet_parse.h"
#include "logger.h"
#include "position_source.h"
#include "afsk_tx_core.h"  // 調整トーンのモード (AFSK_TONE_*)
#include "version.h"      // ファームウェアのバージョン
#include "msg_core.h"     // メッセージ
#include "afsk_hw.h"      // 受信状態の型 (AfskHwRxStatus)。ヘッダだけで、ESP32の部品は不要
#include "ui_text.h"      // 画面の文字の日本語/英語 (L)

#define UI_COLS        38     // 1行の最大桁数(半角=1、全角=2)
#define UI_MAX_LINES   13     // ヘッダ1行+本文12行 (最下段の2行はボタン表示)
#define UI_TITLE_COLS  21     // ヘッダの画面名の最大桁数 (右側はアイコン8個)
#define UI_TEXT_MAX    128    // UTF-8の最大バイト数
#define UI_SOFTKEY_MAX 32     // ボタン表示1つの最大バイト数 (UTF-8)。幅は半角13桁まで

// 画面: 1=自局 2=受信一覧 3=レーダー 4=メッセージ 5=WX(気象) 6=受信状態 7=送信・設定
enum UiScreen { UI_SCREEN_OWN = 0, UI_SCREEN_RX, UI_SCREEN_RADAR, UI_SCREEN_MSG, UI_SCREEN_WX, UI_SCREEN_RXSTAT, UI_SCREEN_TXSTAT, UI_SCREEN_COUNT, UI_SCREEN_MENU = UI_SCREEN_COUNT, UI_SCREEN_CALIB, UI_SCREEN_CLEAR };   // CALIB: 調整トーン、CLEAR: 記録の消去 (メニューから開く)
enum UiAction { UI_ACT_NONE = 0, UI_ACT_SEND, UI_ACT_CALIB_TOGGLE, UI_ACT_MSG_SEND, UI_ACT_CLEARED };   // CLEARED: 記録を消した (ui->clearDone に対象)   // MSG_SEND: 作成したメッセージ (ui->cmp.to / text) を送る   // CALIB_TOGGLE: 調整トーンの開始 (停止中) / 停止 (出力中)   // loop側が行う処理
enum UiColor  { UI_COL_NORMAL = 0, UI_COL_DIM, UI_COL_GOOD, UI_COL_WARN, UI_COL_BAD, UI_COL_HEAD, UI_COL_HILITE, UI_COL_SAKURA };
enum UiMenuItem { UI_MENU_AUTOTX = 0, UI_MENU_AMBIGUITY, UI_MENU_DIGIPATH, UI_MENU_SYMBOL, UI_MENU_SSID, UI_MENU_COURSE_SPEED, UI_MENU_ALTITUDE, UI_MENU_MONITOR, UI_MENU_CHIME, UI_MENU_BRIGHTNESS, UI_MENU_POSSRC, UI_MENU_FIXED, UI_MENU_CALIB, UI_MENU_CLEAR, UI_MENU_LANGUAGE, UI_MENU_VERSION, UI_MENU_COUNT };   // UI_MENU_FIXED は取得元=固定位置のときだけ表示

// icon: シンボルのアイコン (symbols.h の番号。0=なし)。iconCol 桁目から半角2桁 (16px) の所に描く (文字はそこを空白にしておく)。
// iconBig: 32x32 で、この行と次の行の右端に描く (この2行は UI_COLS - UI_BIGICON_COLS 桁までにする)
struct UiLine { char text[UI_TEXT_MAX]; uint8_t color; uint8_t icon; uint8_t iconCol; uint8_t iconBig; };
#define UI_BIGICON_COLS 5     // 32x32 のアイコンが使う幅 (半角5桁 = 40px。すき間を含む)

// 横棒グラフ。row 行目の、文字(ラベル)の右側に描く。pct は 0〜100
struct UiBar { int8_t row; uint8_t pct; uint8_t color; };
#define UI_MAX_BARS 4

// ヘッダ右側の状態アイコン。右から 電池・SD・音量・GPS/位置・自動送信・送受信・メッセージ の定位置
enum UiPosIcon  { UI_POSICON_GPS_FIX = 0, UI_POSICON_GPS_WAIT, UI_POSICON_GPS_STALE, UI_POSICON_GPS_NONE, UI_POSICON_LAST, UI_POSICON_FIXED };
enum UiAutoIcon { UI_AUTOICON_OFF = 0, UI_AUTOICON_ACK, UI_AUTOICON_ON };
enum UiActIcon  { UI_ACTICON_NONE = 0, UI_ACTICON_TX, UI_ACTICON_RX };
#define UI_ICON_SLOTS  8
struct UiIcons {
  int8_t  batt;               // 電池の段数 0〜4。-1 = 不明
  uint8_t charging;           // 充電中
  uint8_t sdOk;
  uint8_t vol;                // 音量の弧の数 0〜4 (0=ミュート)
  uint8_t bell;               // 受信音の音量 0〜4 (0=鳴らさない)
  uint8_t pos;                // UiPosIcon
  uint8_t autoTx;             // UiAutoIcon
  uint8_t act;                // UiActIcon
  uint8_t unread;             // 未読あり
};

// レーダー画面の描画 (ui_m5 が描く)。位置は画面の座標 (ドット)
#define UI_RADAR_PTS   RADAR_STATION_COUNT
#define UI_RADAR_INFO  8
#define UI_RADAR_INFO_COLS 13      // 右側の1行の最大桁数 (英字は幅が広い字があるので余裕を持たせる)
struct UiRadarPt { int16_t x, y; uint8_t icon; uint8_t color; char label[10]; };   // icon=0 は点。label はコールサイン
struct UiRadar {
  bool    on;                 // レーダー画面
  bool    hasOwn;             // 自局の位置がある (無ければ円だけ描かない)
  int16_t cx, cy, r;          // 中心と外側の円の半径
  char    ringLabel[2][6];    // 内側・外側の円の距離 ("5" "10")
  uint8_t count;              // 円の中の局 (古い順。新しい局を上に描く)
  UiRadarPt pts[UI_RADAR_PTS];
  char    info[UI_RADAR_INFO][32];   // 右側の文字 (UTF-8)
  uint8_t infoColor[UI_RADAR_INFO];
};

struct UiView {
  UiLine lines[UI_MAX_LINES];
  int    count;
  UiBar  bars[UI_MAX_BARS];
  int    barCount;
  UiIcons icons;
  char   keyShort[3][UI_SOFTKEY_MAX];   // A, B, C の短押し (空 = 機能なし)
  char   keyLong[3][UI_SOFTKEY_MAX];    // A, B, C の長押し (空 = 機能なし)。「長:」は描画側が付ける
  uint8_t screen;             // UiScreen (UI_SCREEN_MENU 含む)
  UiRadar radar;
};

// 受信一覧の1件 (一覧と詳細に出す分)
struct UiRxEntry {
  uint32_t seq;               // 受信の通し番号 (詳細で選んでいる1件を見分ける。0は使わない)
  char     src[PARSE_CALL_MAX];
  char     time[9];           // "HH:MM:SS" (UTC)。時刻不明なら "--:--:--"
  uint32_t ms;                // 受信した時刻 (millis)。restored のときは使わない
  uint32_t utc;               // 受信した時刻 (UTC、1970年からの秒)。不明なら 0
  bool     restored;          // 再起動の前に受信した (SDから読み込んだ)
  bool     hasPos;
  uint8_t  kind;              // AprsKind
  bool     weather;           // 気象データを含む
  double   latDeg, lonDeg;
  bool     hasAlt;  int32_t altM;
  bool     hasSpeed; int32_t speedKmh;   // km/h (パケットにあるときだけ)
  bool     hasCourse; uint16_t courseDeg;
  bool     hasDist; double distKm, bearingDeg;
  char     symbol[3];
  char     via[PARSE_VIA_MAX];          // 経路 ("DIGI1-1*,WIDE2-1")。直接なら ""
  char     micEMsg[12];               // Mic-Eの状態 ("En Route" など)
  char     model[MICE_MODEL_MAX];     // Mic-Eの機種 (詳細だけに出す)
  char     to[10];                    // メッセージの宛先 (他局どうしのメッセージ)
  char     comment[PARSE_COMMENT_MAX]; // コメント・本文 (種類ごとに整えたもの)
};

// 入力レベルの目盛り: この振幅(ADC値)を100%とする。ADCの振り切れは約2048
#define UI_LEVEL_FULL      2000
// 受信中に入力がこれより小さいと「小さい」の警告 (ADC値の振幅。約80mV)
#define UI_LEVEL_LOW       100
// 直流分が中心からこれ以上ずれていたら「バイアスを確認」の警告 (ADC値。約0.8V)
#define UI_DC_WARN         1000
// MARK/SPACE の強さの比。この範囲なら良好 / 受信できる限界 (実測 0.37〜3.5倍)
#define UI_RATIO_GOOD_LO   0.5
#define UI_RATIO_GOOD_HI   2.0
#define UI_RATIO_LIMIT_LO  0.37
#define UI_RATIO_LIMIT_HI  3.5
#ifndef RX_RESULT_SHOW_MS
#define RX_RESULT_SHOW_MS  2000
#endif

// 画面に出すための現在の状況 (loopが毎回埋める)
struct UiSnapshot {
  GpsStatus gps;
  bool      hasPos;  double latDeg, lonDeg;   // 自局の位置 (現在、または最終測位位置)
  uint32_t  posAgeSec;
  uint8_t   posSource;                         // 位置の取得元 (POSITION_SOURCE_*)
  PosReason posReason;                         // 位置が使えない理由
  char      posLabel[16];                      // 固定位置の名前
  bool      lastPosValid; double lastPosLat, lastPosLon;   // 保存されている最後の位置 (メニューで表示)
  bool      hasUtc;  GpsUtc utc;
  uint8_t   satellites;
  bool      sdOk;    uint32_t sdPendingBytes;
  int8_t    battLevel;                         // 電池残量 0〜100(%)。-1 = 不明
  bool      battCharging;
  bool      rxCarrier;
  bool      previewValid;                     // 送信できる(位置がある)か
  bool      previewStale;                     // 最終測位位置で送る
  char      previewHeader[80];
  char      previewInfo[128];
  bool      ptt;                               // PTT出力がオン
  uint32_t  pttTimeouts;                       // PTT最大時間超過で送信を中止した回数
  uint32_t  rxFramesOk, rxFcsErrors, rxFragments, rxOverruns, rxDiscards, queueDropped, rxFlags, txCount;
  AfskHwRxStatus rx;                           // 受信の状態 (afsk_hw_rx_status)
  bool      hasLastTx; uint32_t lastTxMs;      // 最後に送信した時刻 (millis)
  uint32_t  uptimeMs;
  uint8_t   autoTx;                            // 自動送信の選択 (settings の autoTx と同じ)
  uint32_t  autoRemainMs;                      // 次の自動送信までの残り (ms)
  bool      autoWaiting;                       // 自動送信の時刻を過ぎ、他局の送信が止むのを待っている
  char      sdkVersion[24];                    // ESP32 SDK のバージョン (ESP.getSdkVersion)。バージョン情報で表示
  uint8_t   chipRev;                           // ESP32 チップのリビジョン
  uint32_t  freeHeap;                          // 空きメモリ (バイト)
  const SettingsMgr* settings;
};

// WX(気象)局。局ごとに最新の値を持つ (仕様 4.5.1)
#ifndef WX_STATION_COUNT
#define WX_STATION_COUNT  8          // 覚えておく局数。超えたら受信がいちばん古い局と入れ替える
#endif
#ifndef WX_STALE_MS
#define WX_STALE_MS       3600000UL  // これより長く受信がない局は灰色で表示する (60分)
#endif
struct UiWxEntry {
  bool     used;
  char     src[PARSE_CALL_MAX];
  char     time[9];           // 受信時刻 "HH:MM:SS" (UTC)。不明なら "--:--:--"
  uint32_t lastMs;            // 最後に受信した時刻 (millis)。restored のときは並び順にだけ使う
  uint32_t utc;               // 最後に受信した時刻 (UTC、1970年からの秒)。不明なら 0
  bool     restored;          // 再起動の前に受信した (SDから読み込んだ)
  uint32_t count;             // 受信回数
  bool     hasPos; double latDeg, lonDeg;
  bool     hasDist; double distKm, bearingDeg;   // 受信したときの自局からの距離と方位
  AprsWeather w;
  char     comment[48];
};

#define UI_CMP_ITEMS_MAX  16

enum UiCmpStep { UI_CMP_TO = 0, UI_CMP_TEXT, UI_CMP_CONFIRM };
// レーダー用に覚えておく局 (局ごとに最後の位置)。自局は入れない
struct UiStation {
  bool     used;
  bool     restored;          // 再起動の前に受信した
  char     call[PARSE_CALL_MAX];
  char     symbol[3];
  double   latDeg, lonDeg;
  uint32_t ms;                // 最後に受信した時刻 (millis)
  uint32_t utc;               // 同 (UTC、1970年からの秒)。不明なら 0
};

struct UiCompose {
  bool    active;
  uint8_t step;                       // UiCmpStep
  int     sel;                        // 一覧で選んでいる項目
  char    to[MSG_CALL_MAX];
  char    text[APRS_MSG_TEXT_MAX + 1];
  char    input[APRS_MSG_TEXT_MAX + 1];   // キーボードで入力中の文字 (宛先・本文の選択中)
  char    replyTo[MSG_CALL_MAX];      // 返信のときの相手 (宛先の候補の先頭に出す)
};

struct UiState {
  uint8_t  screen;            // UiScreen (UI_SCREEN_MENU 含む)
  uint8_t  returnScreen;      // メニューを閉じたときに戻る画面
  uint8_t  menuItem;
  bool     txBusy;
  bool     calibOn;           // 調整トーンを出している (ui_set_calib_on)
  uint8_t  calibMode;         // 調整トーンのモード (AFSK_TONE_MARK/SPACE/ALT)。画面で選ぶ
  uint32_t calibSinceMs;      // 調整トーンを始めた時刻
  char     msg[64];
  uint32_t msgUntilMs;
  // ボタン
  bool     down[3];
  uint32_t downSince[3];
  bool     consumed[3];       // 長押しの処理済み、または同時押しで無効にした
  // 受信一覧 (新しいものが head。最大 RX_LIST_COUNT)
  UiRxEntry rx[RX_LIST_COUNT];
  uint16_t rxHead, rxCount;
  uint32_t rxTotal;
  bool     hasLastRx; uint32_t lastRxMs;   // 最後に受信したパケットの時刻 (millis)
  bool     rxDetail;          // 受信一覧で1件の詳細を表示中
  uint32_t rxSel;             // 詳細を表示中の受信の seq
  // WX(気象)局
  UiWxEntry wx[WX_STATION_COUNT];
  bool     wxDetail;          // WX画面で1局の詳細を表示中
  char     wxSel[PARSE_CALL_MAX];   // 詳細を表示中の局
  // メッセージ
  MsgCore* msgs;              // ui_set_msgs で渡す (NULLならメッセージ画面は空)
  bool     msgDetail;         // メッセージ画面で1件を表示中
  uint32_t msgSel;            // 表示中のメッセージの seq。0 = 「新しいメッセージを作成」
  UiCompose cmp;              // メッセージ作成中
  char     myCall[MSG_CALL_MAX];   // 今の自局 (SSID付き)。宛先の候補から除く (ui_set_my_call)
  // レーダー
  UiStation st[RADAR_STATION_COUNT];
  bool     radarMode;         // 切り替えモード (A=距離 C=時間 B=終了)
  // 記録の消去 (メニューから開く画面)
  uint8_t  clearTarget;       // UiClearTarget
  bool     clearConfirm;      // 確認中 (もう一度 B で消す)
  uint8_t  clearDone;         // 最後に消した対象 (UI_ACT_CLEARED のとき)
};

// 記録の消去の対象
enum UiClearTarget { UI_CLEAR_RX = 0, UI_CLEAR_WX, UI_CLEAR_MSG, UI_CLEAR_RADAR, UI_CLEAR_ALL, UI_CLEAR_COUNT };
// 記録を消す (受信一覧 / WX局 / メッセージ / レーダーの局 / すべて)。保存ファイルには次の保存で反映される
void ui_clear(UiState* ui, uint8_t target);

void ui_set_msgs(UiState* ui, MsgCore* m);
// 今の自局のコールサイン (SSID付き)。メッセージの宛先の候補に自局を出さないため
void ui_set_my_call(UiState* ui, const char* call);
// キーボードの1文字 (CardKB の値)。メッセージ作成中だけ使う。Enter=決定/送信、BS=削除、Esc=戻る
UiAction ui_key(UiState* ui, char c, uint32_t nowMs);
// メッセージを送ったあと (UI_ACT_MSG_SEND の処理が終わったら) 作成画面を閉じ、そのメッセージを表示する
void ui_compose_done(UiState* ui, int msgIdx);
// 作成の候補 (宛先 / 本文)。戻り値: 個数
int ui_compose_items(const UiState* ui, char out[UI_CMP_ITEMS_MAX][APRS_MSG_TEXT_MAX + 1]);

// SDから読み込んだあとの後始末 (WX局の時刻を「再起動前」にする)。メッセージは msg_after_restore
void ui_after_restore(UiState* ui, uint32_t nowMs);

// WX局を新しい順に並べた添字を out に入れる。戻り値: 局数
int ui_wx_order(const UiState* ui, uint8_t out[WX_STATION_COUNT]);

void ui_init(UiState* ui);

// ボタンの状態 (押されていれば true) を毎回 loop から渡す。UI_ACT_SEND が返ったら送信を始める。
UiAction ui_buttons(UiState* ui, SettingsMgr* settings, bool a, bool b, bool c, uint32_t nowMs);

// 送信の開始/終了を知らせる。送信中はボタンを無視し、画面に「送信中」を出す。
void ui_set_tx_busy(UiState* ui, bool busy);

// 調整トーンの開始/終了を知らせる。出力中は、どのボタンを押しても UI_ACT_CALIB_TOGGLE (停止) を返す。
void ui_set_calib_on(UiState* ui, bool on, uint32_t nowMs);

// 画面の下の方に、一時的なメッセージ (例: 「未測位のため送信しません」) を出す。
void ui_show_message(UiState* ui, const char* text, uint32_t nowMs, uint32_t durationMs);

// コンパイラの日付 (__DATE__ の "Oct 18 2026") を "2026-10-18" にする。バージョン情報の表示用
void ui_build_date(char* out, size_t n, const char* date);

// 受信したパケットを一覧に加える。ownValid なら距離と方位を計算する。テレメトリは一覧に入れない。
void ui_rx_add(UiState* ui, const AprsPacket* pkt, bool ownValid, double ownLat, double ownLon, const LogTime* t, uint32_t nowMs);
// 気象データを含むパケットなら、WX局の一覧を更新する (ui_rx_add からも呼ばれる)
void ui_wx_add(UiState* ui, const AprsPacket* pkt, bool ownValid, double ownLat, double ownLon, const LogTime* t, uint32_t nowMs);

// 現在の画面の表示内容を作る。
void ui_render(const UiState* ui, const UiSnapshot* snap, UiView* view, uint32_t nowMs);

// 文字列を表示桁数 cols (半角1/全角2) に収まるよう、UTF-8の文字の切れ目で切る。
// 表示不能なバイト(制御文字、不正なUTF-8)は '?' にする。戻り値: 表示桁数
int ui_fit(char* s, int cols);

// 位置が使えないとき、送信を断るメッセージ (画面に数秒出す)
const char* ui_send_refused_text(PosReason reason);

// 同じ内容か (描画の差分更新用)
bool ui_line_equal(const UiLine* a, const UiLine* b);
bool ui_icons_equal(const UiIcons* a, const UiIcons* b);

#endif
