// app_config.h
// microSD の設定ファイル (/aprs/config.txt) で決める設定。
//   局ごとに変えるもの: コールサイン、コメント、固定位置、デジパスの候補、メッセージの定型文と宛先の候補
//   無線機・基板に合わせて調整するもの: 送信レベル、PTTの極性と待ち時間、プリアンブルの長さ
// ここはファイルの中身の読み取り (と見本の作成) だけ。M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
// SDからの読み込みと、本体 (NVS) への控えの保存は M5APRS.ino と app_config_nvs.cpp。
//
// ファイルの書き方 (1行に1つ。UTF-8):
//   キー = 値
//   行の先頭が # の行はコメント。空行は無視。値の途中の # は文字として扱う。
//   書き間違いのある行は無視して、その行を飛ばす (行番号を記録する)。書かなかった項目は初期値。
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include "fw_config.h"

#define CFG_FILE_NAME     "config.txt"   // LOG_DIR の下
#define CFG_FILE_MAX      6144           // 読む大きさの上限 (これより大きいファイルは読まない)。本体 (NVS、標準の区画は20KB) の控えは
                                         // 書き換えのとき新旧が一時的に並ぶので、8KB だと入りきらないおそれがある (0.13.2 で 4KB から 6KB に)
#define CFG_CALL_MAX      10             // "N0CALL-15" + NUL (6文字のコールサイン + "-15")
#define CFG_COMMENT_MAX   43             // 非圧縮形式の位置のコメントの上限
#define CFG_NAME_MAX      9              // 固定位置の名前 (画面の表示幅)
#define CFG_DIGIPATH_MAX  31
#define CFG_MSG_TEXT_MAX  67             // APRSのメッセージ本文の上限
#define CFG_ERROR_MAX     8              // 覚えておく書き間違いの数

// フレックス (スマートビーコニングと同じ方式) の設定値。単位は八重洲・ケンウッドと同じ
#define FLEX_KEYS_REQUIRED 0x7F                  // set のうち、そろっていないとフレックスを選べない7項目
struct FlexConfig {
  uint8_t  set;            // 書かれていた項目 (bit0〜6 = 下の7項目の順、bit7 = slowDistM)
  uint8_t  lowSpeed;       // km/h (2〜30)
  uint8_t  highSpeed;      // km/h (2〜90)
  uint8_t  slowRateMin;    // 分 (1〜100)
  uint8_t  fastRateSec;    // 秒 (10〜180)
  uint8_t  turnAngle;      // 度 (5〜90)
  uint8_t  turnSlope;      // 10度×km/h 単位 (1〜255)
  uint8_t  turnTimeSec;    // 秒 (5〜180)
  uint16_t slowDistM;      // m (0〜5000。0 = 使わない。書かなくてもよい)
};

// 固定位置の1件。緯度と経度が両方 0 の欄は「未設定」
struct FixedPosition {
  char   name[CFG_NAME_MAX + 1];
  double latDeg;
  double lonDeg;
  bool   hasAlt;
  double altM;
};

struct AppConfig {
  char          callsign[CFG_CALL_MAX];          // 自局のコールサイン (SSIDなし。SSIDは設定メニュー)。"NOCALL" は未設定
  char          comment[CFG_COMMENT_MAX + 1];    // 位置に付けるコメント
  FixedPosition fixed[FIXED_POSITION_COUNT];
  char          digipath[DIGIPATH_CHOICE_COUNT][CFG_DIGIPATH_MAX + 1];   // "" = なし
  char          msgPresets[MSG_PRESET_MAX][CFG_MSG_TEXT_MAX + 1];
  uint8_t       msgPresetCount;
  char          friends[MSG_FRIEND_MAX][CFG_CALL_MAX];
  uint8_t       friendCount;
  uint8_t       txAmpMark, txAmpSpace;           // 送信トーンの振幅 1〜127
  uint16_t      pttOnDelayMs, pttOffDelayMs;
  uint8_t       pttActiveHigh;                   // 1: HIGHでPTTオン
  uint8_t       txPreambleFlags;                 // プリアンブルのフラグ数
  FlexConfig    flex;                            // フレックス (初期値なし。config.txt に書いたものだけ)
};

// 今の設定 (起動時に読み込む。読む前は初期値)
extern AppConfig g_cfg;

enum CfgErrorCode {
  CFG_E_NO_EQUAL = 1,     // 「=」がない
  CFG_E_UNKNOWN_KEY,      // 知らないキー
  CFG_E_BAD_VALUE,        // 値が正しくない (範囲外、使えない文字、形が違う)
  CFG_E_TOO_LONG,         // 値が長すぎる
  CFG_E_LINE_TOO_LONG,    // 1行が長すぎる
};
struct CfgError { uint16_t line; uint8_t code; char key[16]; };

// 読み込みの結果 (画面と診断出力に出す)
enum CfgSource { CFG_SRC_DEFAULT = 0, CFG_SRC_FILE, CFG_SRC_CACHE };
struct CfgStatus {
  uint8_t  source;          // CfgSource: 初期値のまま / SDのファイル / 本体の控え (SDが無い・読めない)
  bool     createdFile;     // ファイルが無かったので書き出した (見本、または控えから)
  bool     tooBig;          // ファイルが CFG_FILE_MAX より大きくて読めなかった
  uint8_t  errorCount;      // 書き間違いの行の数 (CFG_ERROR_MAX を超えた分も数える。255まで)
  CfgError errors[CFG_ERROR_MAX];
};
extern CfgStatus g_cfgStatus;

void app_config_defaults(AppConfig* c);
// text (len バイト。NUL終端でなくてよい) を読んで c に入れる。c は初期値から始める。
// 書き間違いは st->errors に記録する (st は NULL でもよい)。戻り値: 書き間違いの行の数
int  app_config_parse(AppConfig* c, const char* text, size_t len, CfgStatus* st);
// コールサインが設定済みか (空・"NOCALL" でない)。未設定なら送信しない
bool app_config_callsign_ok(const AppConfig* c);
// フレックスの7項目がそろっていて、LOW SPEED < HIGH SPEED か。そうでなければフレックスは選べない
bool app_config_flex_ok(const AppConfig* c);
// 見本のファイル (初期値と説明のコメント入り) を out に作る。戻り値: バイト数 (cap が足りなければ 0)
size_t app_config_template(char* out, size_t cap);
// 書き間違いの説明 (日本語。画面では L() で英語にする) と、診断出力用の英語
const char* app_config_error_text(uint8_t code);
const char* app_config_error_text_en(uint8_t code);

// 固定位置の欄が設定済みか、設定済みの欄の数 (fixed_pos.h の判定を g_cfg に使う)
bool fixed_position_set(int i);
int  fixed_position_count_set();

#endif
