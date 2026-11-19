// rx_dedupe.h
// 同じパケットを何度も受けたとき (複数のデジピーター経由など) に、2回目以降を見分ける。
// 送信元・宛先・情報欄が同じものを RX_DUPE_MS 以内に受けたら「重複」とする (経路の違いは見ない)。
// 宛先も比べるのは、Mic-E では宛先に緯度が入っているため。
// 重複は、受信一覧・WX・メッセージの処理を省く (ログには残す)。M5Stack/Arduinoの部品に依存しない。
#ifndef RX_DEDUPE_H
#define RX_DEDUPE_H

#include <stdint.h>
#include <stddef.h>

#ifndef RX_DUPE_MS
#define RX_DUPE_MS     30000      // この時間内に同じ内容が来たら重複 (一般的なデジピーターと同じ30秒)
#endif
#define RX_DUPE_SLOTS  16         // 覚えておく件数

struct RxDupeEntry { bool used; uint32_t hash; uint16_t len; uint32_t ms; };
struct RxDedupe {
  RxDupeEntry e[RX_DUPE_SLOTS];
  uint8_t     next;
  uint32_t    dupes;              // 重複と判定した数
};

void rx_dedupe_init(RxDedupe* d);
// frame は FCSなしのフレーム。重複なら true (覚えている時刻は更新しない)。新しいものなら覚えて false。
// アドレス部が読めないフレームは、重複としない (false)
bool rx_dedupe_check(RxDedupe* d, const uint8_t* frame, size_t len, uint32_t nowMs);

#endif // RX_DEDUPE_H
