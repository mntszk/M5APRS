// logger_sd.h
// logger.h の保存先 (LogSink) を、M5Stack Basic のmicroSD (Arduino SDライブラリ) で実現する。
// ESP32のArduinoコア用。パソコンのテストには入れない。
//
// SDカードがない、書けない、抜かれたなどのときは append が false を返し、次の呼び出しで
// SD.begin からやり直す (logger側が一定時間おいて再試行する)。
//
// 使い方:
//   Logger logger;
//   logger_init(&logger, logger_sd_sink(), LOG_DIR);
// SPIバスはLCDと共有する。画面描画と重ならないよう、logger_set_bus_guard で排他を渡すこと
// (loopの中だけで画面とSDを順番に処理するなら、省略してよい)。

#ifndef LOGGER_SD_H
#define LOGGER_SD_H

#include "logger.h"

#ifndef LOGGER_SD_FREQ
#define LOGGER_SD_FREQ 20000000   // SPIのクロック。不安定なら下げる
#endif

const LogSink* logger_sd_sink();

// ファイルを少しずつ書いて置き換える (再起動後の復元用の state.bin など)。
// begin で dir/name.tmp を空にし、append で追記、commit で dir/name と置き換える。
// 書き込みの途中で電源が切れても、前のファイルか新しいファイルのどちらかが残る。
bool logger_sd_file_begin(const char* dir, const char* name);
bool logger_sd_file_append(const char* dir, const char* name, const uint8_t* data, size_t len);
bool logger_sd_file_commit(const char* dir, const char* name);
// ファイル全体を読む。戻り値: 読んだバイト数 (ない・読めない・cap より大きいときは 0)
size_t logger_sd_read_file(const char* dir, const char* name, uint8_t* buf, size_t cap);
// ファイルの大きさ。SDが使えなければ -1、ファイルが無ければ -2
long logger_sd_file_size(const char* dir, const char* name);

#endif // LOGGER_SD_H
