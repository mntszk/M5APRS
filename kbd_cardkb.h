// kbd_cardkb.h
// M5Stack CardKB2 / CardKB (I2C キーボード) の読み取り。本体の Groveポート (ポートA、GPIO21/22) につなぐ。
// I2C の読み方は CardKB2 と CardKB で同じ: アドレス 0x5F から1バイト読むと、押したキーの文字 (ASCII)。なければ 0。
// 接続の検知はしない: 一定間隔で読みに行き、読めなければ (つながっていなければ) 何もしない。
// キーボードは「あれば文字を入力できる」補助の位置づけ。画面や動作はキーボードの有無で変えない。

#ifndef KBD_CARDKB_H
#define KBD_CARDKB_H

#include <stdint.h>
#include "app_config.h"

#ifndef CARDKB_I2C_ADDR
#define CARDKB_I2C_ADDR      0x5F
#endif
#ifndef CARDKB_I2C_FREQ
#define CARDKB_I2C_FREQ      50000    // I2Cの速度 (Hz)。CardKB2は50kHzで安定 (100kHzだと入力が途切れることがある)
#endif
#ifndef CARDKB_POLL_MS
#define CARDKB_POLL_MS       30       // キーを読む間隔 (押しっぱなしの繰り返しは50msごとに来る)
#endif

void kbd_begin();
// loop から毎回呼ぶ。キーが押されていたら *c に入れて true
bool kbd_poll(uint32_t nowMs, char* c);

#endif
