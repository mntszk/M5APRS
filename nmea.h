// nmea.h
// GPSモジュールのNMEA0183文から、位置・速度・進行方向・高度・時刻を取り出す。
// M5Stack/Arduinoの部品に依存しない(時刻は呼び出し側が millis() の値を渡す)。
//
// 使うセンテンス: RMC(位置・速度・進行方向・日時) と GGA(高度)。日時は測位前 (V) のRMCでも、日付があれば使う。
// 先頭2文字(GP/GN/BD/GL など)の違いは区別せず、末尾が "RMC" / "GGA" のものを使う。
// チェックサムが合わない、または無いものは捨てる。RMCのステータスが V(無効)のものは位置に使わない。
//
// 使い方:
//   GpsState gps; gps_init(&gps);
//   loop() で GPSSerial から届いた文字を1つずつ: gps_feed_char(&gps, c, millis());
//   送信時: GpsPosition pos; if (gps_get_position(&gps, millis(), GPS_LAST_FIX_VALID_SEC, &pos)) {...}
//   画面表示: gps_get_status(&gps, millis(), GPS_NO_DATA_TIMEOUT_MS, GPS_LAST_FIX_VALID_SEC)

#ifndef NMEA_H
#define NMEA_H

#include <stdint.h>
#include <stddef.h>

#define NMEA_LINE_MAX  100   // 1行の最大長 (NMEAは最大82文字)
// 測位前 (RMCのステータス V) の日時は、この範囲の年のときだけ使う (時刻合わせ前の既定の日付を除くため)
#define NMEA_V_MIN_YEAR  2024
#define NMEA_V_MAX_YEAR  2079

enum GpsStatus {
  GPS_NO_DATA = 0,   // NMEAが一定時間届いていない (「GPS受信なし」)
  GPS_NO_FIX,        // NMEAは届くが、使える位置がない (測位待ち)
  GPS_STALE,         // 測位が途切れているが、直前の位置を使える期間内
  GPS_FIX            // 測位中
};

struct GpsPosition {
  double   latDeg, lonDeg;     // 北緯・東経が正
  bool     hasSpeed;
  double   speedKnots;
  bool     hasCourse;
  double   courseDeg;          // 0以上360未満
  uint32_t ageSec;             // その位置を受信してからの秒数
};

struct GpsState {
  // 行バッファ
  char     line[NMEA_LINE_MAX];
  uint8_t  len;
  bool     inLine, overflow;
  // 最後の有効な測位 (RMC, status=A)
  bool     hasPos;
  double   latDeg, lonDeg;
  bool     hasSpeed, hasCourse;
  double   speedKnots, courseDeg;
  uint32_t posMs;              // 受信時刻 (millis)
  bool     lastRmcValid;       // 直近のRMCが有効(A)だったか
  // 日時 (RMCのUTC。測位前のものも含む)
  bool     hasTime;
  uint16_t year; uint8_t month, day, hour, minute, second;
  uint32_t timeMs;             // 上の日時を受信した時刻 (millis)
  // 高度 (GGAでfix品質が1以上のもの)
  bool     hasAlt;
  double   altM;
  uint32_t altMs;
  uint8_t  satellites;         // 直近のGGAの衛星数
  // 受信状況
  bool     anyData;
  uint32_t lastDataMs;         // 有効なセンテンスを最後に受信した時刻
  uint32_t sentencesOk, checksumErrors, overflows;
};

void gps_init(GpsState* g);

// 1文字入力。センテンスを1つ処理したら true (チェックサムが合っていたもの)。
bool gps_feed_char(GpsState* g, char c, uint32_t nowMs);

// まとめて入力。処理したセンテンスの数を返す。
size_t gps_feed(GpsState* g, const char* data, size_t n, uint32_t nowMs);

// 送信に使える位置を取り出す。最後の測位から maxAgeSec 秒以内なら true。
bool gps_get_position(const GpsState* g, uint32_t nowMs, uint32_t maxAgeSec, GpsPosition* out);

// 現在のUTC日時 (最後に日時を得たRMCの日時に、経過時間を足したもの)。まだ得られていなければ false。
struct GpsUtc { uint16_t year; uint8_t month, day, hour, minute, second; };
bool gps_get_utc(const GpsState* g, uint32_t nowMs, GpsUtc* out);

// 高度(m)を取り出す。GGAを最後に受信してから maxAgeSec 秒以内なら true。
bool gps_get_altitude(const GpsState* g, uint32_t nowMs, uint32_t maxAgeSec, double* altM);

// 画面表示用の状態。noDataMs 以上NMEAが届かなければ GPS_NO_DATA。
GpsStatus gps_get_status(const GpsState* g, uint32_t nowMs, uint32_t noDataMs, uint32_t maxAgeSec);

// NMEA文のチェックサム検査 ("$....*hh")。行は '$' から '*hh' まで (改行を除く)。
bool nmea_checksum_ok(const char* sentence);

#endif // NMEA_H
