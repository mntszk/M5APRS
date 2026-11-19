// packet_build.h
// AX.25 UIフレームと、APRS非圧縮位置レポートの生成。
// M5Stack/Arduinoの部品に依存しない(パソコン上でも同じコードをテストできる)。
//
// 仕様: APRS101 第6章(位置・曖昧化)、第7章(データ拡張)、AX.25 UIフレーム
// 設定値(コールサイン等)は呼び出し側が設定 (app_config.h の g_cfg など) から渡す。

#ifndef PACKET_BUILD_H
#define PACKET_BUILD_H

#include <stdint.h>
#include <stddef.h>

#define APRS_INFO_MAX      128   // 情報フィールドの最大長(非圧縮位置レポートは約70バイト)
#define AX25_MAX_DIGIS     8     // デジピーターの最大数
#define AX25_FRAME_MAX     256   // フレーム全体(FCS込み)の最大長

// 位置レポートの内容
struct PositionReport {
  double      latDeg;          // 緯度(度)。北が正
  double      lonDeg;          // 経度(度)。東が正
  uint8_t     ambiguity;       // 位置の曖昧化レベル 0〜4 (0=オフ)
  char        symTable;        // シンボルテーブルID ('/' など)
  char        symCode;         // シンボルコード
  bool        hasCourseSpeed;  // true なら CSE/SPD を付ける
  uint16_t    courseDeg;       // 進行方向(度, 0〜359)
  uint16_t    speedKnots;      // 速度(ノット)
  bool        hasAltitude;     // true なら /A=aaaaaa を付ける
  double      altitudeM;       // 高度(メートル)
  const char* comment;         // コメント(NULLなら空)
};

// AX.25のアドレス部の指定
struct Ax25Params {
  const char* srcCall;         // 送信元コールサイン(SSIDなし、例 "NOCALL")
  uint8_t     srcSsid;         // 送信元SSID 0〜15
  const char* destCall;        // 宛先コールサイン(例 "APRS")
  uint8_t     destSsid;        // 宛先SSID 0〜15
  const char* digipath;        // デジパス。""=なし、"WIDE1-1"、"WIDE1-1,WIDE2-1" のようにカンマ区切り
};

// 非圧縮位置レポートの情報フィールドを out に作る(終端のNULは付けない)。
//   dti: データタイプ識別子('!' など)
// 戻り値: 作ったバイト数。範囲外の緯度経度、不正な引数、バッファ不足のときは 0。
size_t aprs_build_position_info(const PositionReport& rpt, char dti,
                                uint8_t* out, size_t outMax);

// AX.25 UIフレーム(アドレス部 + 制御0x03 + PID 0xF0 + 情報フィールド + FCS)を out に作る。
// フラグ(0x7E)、ビットスタッフィング、NRZIは含まない(afsk_tx側で行う)。
// FCSは、下位バイト、上位バイトの順に末尾へ付ける。
// 戻り値: 作ったバイト数。コールサインやデジパスが不正、バッファ不足のときは 0。
size_t ax25_build_ui_frame(const Ax25Params& p, const uint8_t* info, size_t infoLen,
                           uint8_t* out, size_t outMax);

// FCS (CRC-16/X.25 = CRC-16-CCITT の反転版。初期値0xFFFF、最後に0xFFFFとXOR)
uint16_t ax25_fcs(const uint8_t* data, size_t len);

#endif // PACKET_BUILD_H
