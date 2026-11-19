// ui_m5.h
// ui_core が作った表示内容 (UiView) を、M5Unifiedで画面 (320x240) に描く。ESP32用。
// 前回と同じ行は描き直さない (SPIバスの使用と、受信処理を妨げる時間を減らすため)。
#ifndef UI_M5_H
#define UI_M5_H

#include "ui_core.h"

void ui_m5_begin();                    // M5.begin のあとに1回
// 変わった行だけ描く。画面が切り替わったときは全体を描く。
// barsOnly=true なら、横棒グラフだけを更新する (受信中でも短時間で済む)。ただし全体の描き直しが必要なときは全体を描く。
void ui_m5_draw(const UiView* view, bool barsOnly = false);
void ui_m5_draw_icons(const UiIcons* icons);   // ヘッダの状態アイコンだけ描く (受信中でもすぐ出したいとき。前と同じなら何もしない)
void ui_m5_invalidate();               // 次の ui_m5_draw で全体を描き直す
void ui_m5_set_backlight(uint8_t value);   // 画面の明るさ (1〜255)。同じ値なら何もしない

#endif
