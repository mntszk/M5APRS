// symbols.h
// 自局のシンボルの選択肢と、画面に出すシンボルのアイコン (16x16、単色)。
// M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stdint.h>
#include <stddef.h>

#define SYMBOL_CHOICE_COUNT 21   // 設定で選べるシンボルの数
#define SYMBOL_ICON_COUNT   22   // アイコンの数 (選べる21個 + iGate)
#define SYMBOL_ICON_DIGI    21
#define SYMBOL_ICON_IGATE   22

// アイコンの色: 移動する局=白、常置場所=水色、気象=黄、機能のあるもの (デジピーター・iGate)=緑
enum SymbolColor { SYM_COL_WHITE = 0, SYM_COL_CYAN, SYM_COL_YELLOW, SYM_COL_GREEN };

struct SymbolChoice {
  const char* name;      // 画面の名前 (日本語。L() で英語にする)
  char        table;     // シンボルテーブル ('/' = プライマリ)
  char        code;      // シンボルコード
  uint8_t     icon;      // アイコンの番号 (1〜SYMBOL_ICON_COUNT)
  uint8_t     ssid;      // よく合わせるSSID (目安。表示用)
};
extern const SymbolChoice SYMBOL_CHOICES[SYMBOL_CHOICE_COUNT];

// 選択肢の中の番号。無ければ -1
int symbol_find(char table, char code);
// 受信したシンボルのアイコン番号 (1〜)。アイコンが無ければ 0。代替表 (オーバーレイ付きを含む) は & # と、意味の近い記号だけ当てる
int symbol_icon(char table, char code);
// アイコンの絵 (32バイト: 1行2バイト、左のドットが上位ビット) と色。番号が範囲外なら NULL / 白
const uint8_t* symbol_icon_bitmap(int icon);
uint8_t symbol_icon_color(int icon);
// SSID の用途の説明 (日本語。L() で英語にする)
const char* ssid_use(uint8_t ssid);

#endif
