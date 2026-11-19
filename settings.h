// settings.h
// 設定メニューで変える値の保持と、不揮発領域への保存の制御。
// 保存先(ESP32のPreferences)は SettingsBackend という小さな口で切り離してあり、
// このファイル自体はM5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
// ESP32用の口は settings_nvs.h / settings_nvs.cpp。
//
// 方針(仕様 4.6):
//   - fw_config.h の値が初期値。保存値があれば、それを使う。不正な保存値は使わず、初期値にする。
//   - 値を変えるたびに保存する。ただし「保存済みの値と違うものだけ」を書く。
//   - 送信中と受信の復調中は書き込まない。settings_commit() に busy を渡し、
//     終わってから(loopの中で)呼ぶ。割り込みの中では呼ばない。

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "app_config.h"
#include "beacon_sched.h"   // 自動送信の選択肢 (AUTO_TX_*)
#include "symbols.h"        // シンボルの選択肢

// 画面の明るさ。fw_config.h に無くても動くよう、ここで既定値を決める
#ifndef BRIGHTNESS_LEVEL_COUNT
#define BRIGHTNESS_LEVEL_COUNT    5
#define BRIGHTNESS_TABLE          { 1, 20, 60, 120, 255 }
#define DEFAULT_BRIGHTNESS_LEVEL  3
#endif
// 受信音の音量 0=オフ 〜 MONITOR_LEVEL_MAX (段階はモニター音量と同じ。別に保存する)
#ifndef DEFAULT_CHIME_LEVEL
#define DEFAULT_CHIME_LEVEL       2
#endif
// 画面の言語 0=日本語 1=English
#ifndef DEFAULT_LANGUAGE
#define DEFAULT_LANGUAGE          0
#endif
#define LANGUAGE_COUNT            2
// レーダー画面の距離と時間 (fw_config.h に無くても動くよう、ここで既定値を決める)
#ifndef RADAR_STATION_COUNT
#define RADAR_STATION_COUNT       40
#endif
#ifndef DEFAULT_RADAR_RANGE
#define DEFAULT_RADAR_RANGE       7
#endif
#ifndef DEFAULT_RADAR_TIME
#define DEFAULT_RADAR_TIME        2
#endif
#define RADAR_RANGE_COUNT         8        // 1/2/5/10/20/50/100km と 自動 (最後)
#define RADAR_RANGE_KM            { 1, 2, 5, 10, 20, 50, 100 }
#define RADAR_RANGE_AUTO          7
#define RADAR_TIME_COUNT          7
#define RADAR_TIME_MIN            { 10, 30, 60, 180, 360, 720, 1440 }
// 自局のシンボルとSSIDの初期値 (fw_config.h)
#ifndef DEFAULT_SYMBOL
#define DEFAULT_SYMBOL            "/>"
#endif
#ifndef DEFAULT_SSID
#define DEFAULT_SSID              9
#endif

struct Settings {
  uint8_t ambiguity;          // 0〜MAX_AMBIGUITY_LEVEL
  uint8_t digipathIndex;      // 0〜DIGIPATH_CHOICE_COUNT-1
  uint8_t symbolIndex;        // 0〜SYMBOL_CHOICE_COUNT-1 (SYMBOL_CHOICES の番号)
  uint8_t ssid;               // 自局のSSID 0〜15 (0=SSIDなし)
  bool    sendCourseSpeed;
  bool    sendAltitude;
  uint8_t monitorLevel;       // 0〜MONITOR_LEVEL_MAX (0=ミュート)
  uint8_t positionSource;     // POSITION_SOURCE_GPS / LAST / FIXED
  uint8_t fixedIndex;         // 0〜FIXED_POSITION_COUNT-1 (固定位置の欄)
  uint8_t autoTx;             // 自動送信 0=オフ 1=Ackのみ 2〜=間隔 (AUTO_TX_CHOICES 未満)
  uint8_t brightness;         // 画面の明るさ 0〜BRIGHTNESS_LEVEL_COUNT-1 (暗い順)
  uint8_t language;           // 画面の言語 0=日本語 1=English
  uint8_t radarRange;         // レーダーの距離 0〜RADAR_RANGE_COUNT-1 (最後は自動)
  uint8_t radarTime;          // レーダーの時間 0〜RADAR_TIME_COUNT-1
  uint8_t chimeLevel;         // 受信音の音量 0〜MONITOR_LEVEL_MAX (0=鳴らさない)
};

// 保存先の口。値は1バイトずつ、キー名で読み書きする。
struct SettingsBackend {
  void* ctx;
  bool (*getU8)(void* ctx, const char* key, uint8_t* value);   // 保存値があれば true
  bool (*putU8)(void* ctx, const char* key, uint8_t value);    // 書けたら true
};

#define SETTINGS_KEY_COUNT 15

struct SettingsMgr {
  Settings        cur;                       // 現在の設定 (ここを直接読んでよい)
  Settings        saved;                     // 保存先に入っている(とみなす)値。保存値なし・不正なら初期値
  SettingsBackend be;
  uint32_t        writeCount;                // 書き込み回数 (確認用)
  uint32_t        writeErrors;
};

// fw_config.h の初期値を入れる
void settings_defaults(Settings* s);

// 範囲外の値を初期値に直す。直したものがあれば true
bool settings_sanitize(Settings* s);

// 初期化: 初期値を入れ、保存値があれば読む。be が NULL でも動く(保存しない)。
// 0.7.0 までの「シンボル・SSID」のプリセット (キー preset) だけが保存されていれば、シンボルとSSIDに直して使う。
void settings_init(SettingsMgr* m, const SettingsBackend* be);

// 値の変更。範囲外は丸める(または無視)。実際に値が変わったら true。保存は settings_commit が行う。
bool settings_set_ambiguity(SettingsMgr* m, uint8_t level);
bool settings_set_digipath(SettingsMgr* m, uint8_t index);
bool settings_set_symbol(SettingsMgr* m, uint8_t index);
bool settings_set_ssid(SettingsMgr* m, uint8_t ssid);
bool settings_set_course_speed(SettingsMgr* m, bool on);
bool settings_set_altitude(SettingsMgr* m, bool on);
bool settings_set_monitor(SettingsMgr* m, uint8_t level);
bool settings_set_chime(SettingsMgr* m, uint8_t level);
bool settings_set_position_source(SettingsMgr* m, uint8_t source);
bool settings_set_fixed_index(SettingsMgr* m, uint8_t index);
bool settings_set_auto_tx(SettingsMgr* m, uint8_t mode);
bool settings_set_brightness(SettingsMgr* m, uint8_t level);
bool settings_set_language(SettingsMgr* m, uint8_t lang);
void settings_next_language(SettingsMgr* m);
bool settings_set_radar_range(SettingsMgr* m, uint8_t index);
bool settings_set_radar_time(SettingsMgr* m, uint8_t index);
void settings_next_radar_range(SettingsMgr* m);
void settings_next_radar_time(SettingsMgr* m);
// レーダーの距離 (km。自動なら 0) と時間 (分)
uint16_t settings_radar_range_km(const SettingsMgr* m);
uint16_t settings_radar_time_min(const SettingsMgr* m);

// メニュー用: 次の値に進める(最後の次は最初に戻る)/ 前に戻す / オンオフの反転
void settings_next_ambiguity(SettingsMgr* m);
void settings_next_digipath(SettingsMgr* m);
void settings_next_symbol(SettingsMgr* m);
void settings_next_ssid(SettingsMgr* m);
void settings_toggle_course_speed(SettingsMgr* m);
void settings_toggle_altitude(SettingsMgr* m);
void settings_next_monitor(SettingsMgr* m);
void settings_next_chime(SettingsMgr* m);
// 位置の取得元を次へ (GPS → 最後 → 固定 → GPS)。固定位置が1件も設定されていなければ「固定」は飛ばす。
void settings_next_position_source(SettingsMgr* m);
// 固定位置を、次の「設定済み」の欄へ。設定済みの欄が1件もなければ何もしない。
void settings_next_fixed(SettingsMgr* m);
// 自動送信を次の選択肢へ (オフ → Ackのみ → 1分 → … → 60分 → オフ)
void settings_next_auto_tx(SettingsMgr* m);
// 画面の明るさを次の段階へ (最も明るい → 最も暗い に戻る)
void settings_next_brightness(SettingsMgr* m);
// 今の明るさのバックライトの値 (1〜255)
uint8_t settings_backlight(const SettingsMgr* m);

// 現在の選択の中身
const SymbolChoice*  settings_symbol(const SettingsMgr* m);
uint8_t              settings_ssid(const SettingsMgr* m);
const char*          settings_digipath(const SettingsMgr* m);

// 保存済みと違うキーだけ書く。busy が true なら何もしない(0を返す)。
// 戻り値: 書き込んだキーの数。書けなかったキーは次回また試す。
int settings_commit(SettingsMgr* m, bool busy);

// 書き込み待ちの変更があるか
bool settings_dirty(const SettingsMgr* m);

#endif // SETTINGS_H
