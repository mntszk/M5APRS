// ui_text.h
// 画面の文字の日本語/英語の切り替え。コードの中では日本語の文字をそのまま書き、L("…") で包む。
// 英語のときは ui_text.cpp の対応表から英語を引く (表に無ければ日本語のまま)。
// ボタン表示 (ソフトキー) は短い別の表 K("…") を使う (同じ日本語でも、ボタンでは短い英語にするため)。
// M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <stdint.h>

enum UiLang { UI_LANG_JA = 0, UI_LANG_EN = 1 };

void        ui_set_lang(uint8_t lang);     // UI_LANG_*
uint8_t     ui_lang();
const char* L(const char* ja);             // 画面の文字
const char* K(const char* ja);             // ボタン表示

// テスト・対応表の書き出し用
int         ui_text_count(bool keys);
const char* ui_text_ja(bool keys, int i);
const char* ui_text_en(bool keys, int i);

#endif // UI_TEXT_H
