// app_config_nvs.h
// 設定ファイル (config.txt) の中身の控えを、本体の不揮発領域 (Preferences) に置く。ESP32用。パソコンのテストには入れない。
// microSD が無い・読めないときは、前回読めたファイルの中身を使う。
#ifndef APP_CONFIG_NVS_H
#define APP_CONFIG_NVS_H

#include <stddef.h>

// 控えを buf に読む (NUL終端しない)。戻り値: バイト数 (無ければ 0)
size_t app_config_nvs_load(char* buf, size_t cap);
// 控えを書く (同じ中身なら書かない)。書いたら true
bool   app_config_nvs_save(const char* text, size_t len);

#endif
