// tx_compose.h
// 設定(settings)と自局の位置(position_source で決めたもの)から、送信するAX.25フレームを組み立てる。
// 画面のプレビューと実際の送信で同じ関数を使うので、プレビューと送信内容が食い違わない。
// M5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
#ifndef TX_COMPOSE_H
#define TX_COMPOSE_H

#include <stdint.h>
#include <stddef.h>
#include "settings.h"
#include "position_source.h"

enum TxComposeResult {
  TXC_OK = 0,
  TXC_NO_POSITION,   // 現在の測位も、GPS_LAST_FIX_VALID_SEC 以内の最終測位位置もない
  TXC_NO_CALLSIGN,   // コールサインが未設定 (config.txt の callsign が NOCALL のまま)。送信しない
  TXC_BUILD_ERROR    // 組み立てに失敗(バッファ不足など。通常は起きない)
};

struct TxComposed {
  uint8_t frame[256];       // AX.25フレーム (FCS込み。afsk_hw_send にそのまま渡す)
  size_t  frameLen;
  char    header[80];       // "NOCALL-9>APRS,WIDE1-1:" (プレビュー用)
  char    info[128];        // 情報フィールド(プレビュー用。NUL終端)
  uint8_t source;           // 位置の取得元 (POSITION_SOURCE_*)
  bool    usedStaleFix;     // GPS: 最終測位位置(測位が途切れている間の位置)を使った
  uint32_t posAgeSec;
};

// コールサインが未設定なら TXC_NO_CALLSIGN (位置より先に調べる)。pos は position_resolve の結果。pos->valid が false なら TXC_NO_POSITION。
// 進行方向・速度は、取得元がGPSのときだけ付ける (最後の位置・固定位置は動いていないものとして扱う)。
TxComposeResult tx_compose(const SettingsMgr* s, const OwnPos* pos, TxComposed* out);

// 情報フィールドをそのまま送るフレーム (メッセージ・Ack用)。局 (SSID) とデジパスは位置の送信と同じ設定を使う
TxComposeResult tx_compose_info(const SettingsMgr* s, const char* info, TxComposed* out);

#endif
