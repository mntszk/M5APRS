// state_store.h
// 受信一覧・WX局・メッセージを、再起動しても残るようにmicroSDへ保存する (仕様 4.7.1)。
// ここはデータの組み立てと読み込みだけ。SDの読み書きは logger_sd.cpp (logger_sd_write_file / read_file)。
// M5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
//
// 形式 (1つのファイル。リトルエンディアン、構造体をそのまま並べる):
//   ヘッダ 32バイト: "M5ST", 形式の版, 各構造体の大きさと個数, データの長さ, CRC32
//   データ: 受信一覧 (head, count, total, UiRxEntry×RX_LIST_COUNT)、UiWxEntry×WX_STATION_COUNT、
//           メッセージ (seq, MsgEntry×MSG_STORE_COUNT)、レーダーの局 (UiStation×RADAR_STATION_COUNT)
// ファームウェアを変えて構造体の大きさや個数が変わったときは、読み込まずに空で始める。
//
// 書き込み (state_service を loop から毎回呼ぶ): STATE_SAVE_INTERVAL_MS ごとに内容を作り、前回保存したものと
// CRCが違うときだけ書く。受信のサンプルを取りこぼさないよう、1回に書くのは STATE_WRITE_CHUNK バイトまでで、
// 送信中・受信中 (busy) は止める。いったん一時ファイルに書いてから置き換える。
#ifndef STATE_STORE_H
#define STATE_STORE_H

#include <stdint.h>
#include <stddef.h>
#include "ui_core.h"
#include "msg_core.h"

#define STATE_MAGIC          "M5ST"
#define STATE_FORMAT_VERSION 3   // 2: 受信一覧の1件に位置・経路などを追加 (0.5.0)。3: レーダーの局の表を追加 (0.10.0)
#define STATE_FILE_NAME      "state.bin"     // LOG_DIR の下
#ifndef STATE_WRITE_CHUNK
#define STATE_WRITE_CHUNK    2048             // 1回の書き込みの最大バイト数 (logger と同じ)
#endif
#ifndef STATE_SAVE_INTERVAL_MS
#define STATE_SAVE_INTERVAL_MS 30000          // 内容が変わっていたら、これくらいの間隔でまとめて書く
#endif

#define STATE_HEADER_BYTES   32
#define STATE_PAYLOAD_BYTES  (2 + 2 + 4 + sizeof(UiRxEntry) * RX_LIST_COUNT + sizeof(UiWxEntry) * WX_STATION_COUNT + 4 + sizeof(MsgEntry) * MSG_STORE_COUNT + sizeof(UiStation) * RADAR_STATION_COUNT)
#define STATE_MAX_BYTES      (STATE_HEADER_BYTES + STATE_PAYLOAD_BYTES)

enum StateLoadResult { STATE_LOAD_OK = 0, STATE_LOAD_EMPTY, STATE_LOAD_BAD_HEADER, STATE_LOAD_MISMATCH, STATE_LOAD_BAD_CRC };

uint32_t state_crc32(const uint8_t* d, size_t n);

// 保存する内容を out に作る。戻り値: バイト数 (cap が足りなければ 0)
size_t state_serialize(const UiState* ui, const MsgCore* m, uint8_t* out, size_t cap);
// 作った内容のCRC (ヘッダに入っている値)
uint32_t state_crc_of(const uint8_t* buf, size_t len);

// 読み込む。成功したときだけ ui と m を書き換え、ui_after_restore / msg_after_restore まで行う
StateLoadResult state_restore(UiState* ui, MsgCore* m, const uint8_t* buf, size_t len, uint32_t nowMs);

// ファイルの操作 (実機は logger_sd.cpp。テストではメモリ上)
struct StateFileOps {
  bool (*begin)(void);                              // 一時ファイルを空にして書き始める
  bool (*append)(const uint8_t* data, size_t len);  // 一時ファイルに追記
  bool (*commit)(void);                             // 一時ファイルで本物を置き換える
};

struct StateSaver {
  bool     haveSaved;  uint32_t savedCrc;   // 最後に保存した内容のCRC
  uint32_t lastCheckMs;
  bool     writing;    size_t len, pos;  uint32_t pendingCrc;   // 書き込み中
  uint32_t writes, writeErrors;
};
void state_saver_init(StateSaver* s, uint32_t nowMs);
// loop から毎回呼ぶ。buf は書き終わるまで中身を保つこと (STATE_MAX_BYTES 以上)。
// 戻り値: 書き終えたら 1、失敗したら -1、それ以外 0
int state_service(StateSaver* s, const StateFileOps* ops, const UiState* ui, const MsgCore* m,
                  uint8_t* buf, size_t cap, uint32_t nowMs, bool busy);

#endif // STATE_STORE_H
