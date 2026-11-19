// packet_parse.h
// 受信したAX.25フレームを読み解く: アドレス部、APRSの非圧縮位置、Mic-E、高度、
// 進行方向・速度、距離と方位の計算。M5Stack/Arduinoの部品に依存しない。
//
// 仕様: APRS101 第6〜8章(非圧縮位置)、第9章(圧縮位置。0.11.0)、第10章(Mic-E)。
// 位置を読み取れないパケットは APRS_KIND_OTHER として、情報フィールドをそのまま comment に入れる。
//
// 入力はFCSを除いたフレーム (afsk_rx_frame の出力) をそのまま渡す。

#ifndef PACKET_PARSE_H
#define PACKET_PARSE_H

#include <stdint.h>
#include <stddef.h>

#define PARSE_CALL_MAX     10    // "NOCALL-15" + NUL
#define PARSE_VIA_MAX      80    // "WIDE1-1*,WIDE2-1" など
#define PARSE_COMMENT_MAX  100   // コメント(超えた分は切り捨て)

enum AprsKind {
  APRS_KIND_OTHER = 0,     // 位置を読み取れないパケット(UI以外、オブジェクト、壊れたものなど)
  APRS_KIND_POSITION,      // 位置レポート ( ! = / @ )。非圧縮と圧縮 (compressed で区別)
  APRS_KIND_MICE,          // Mic-E
  APRS_KIND_WEATHER,       // 位置なしの気象レポート ( _ )。位置付きの気象レポートは POSITION で weather.present
  APRS_KIND_MESSAGE,       // メッセージ・Ack・Rej・一斉通知 ( : )
  APRS_KIND_STATUS,        // ステータス ( > )。comment は本文 (先頭の '>' と時刻を除く)
  APRS_KIND_TELEMETRY,     // テレメトリ ( T# )。comment は情報フィールドのまま
  APRS_KIND_CAPS,          // 機能の通知 ( < )。comment は本文 (先頭の '<' を除く)
  APRS_KIND_THIRDPARTY     // 第三者パケット ( } 。iGateがインターネット側から流したもの)。comment は中身
};
#define MICE_MODEL_MAX 20

// メッセージ (APRS仕様書 14章) ":ADDRESSEE:本文{番号"
#define APRS_MSG_TEXT_MAX  67
struct AprsMessage {
  bool present;
  char addressee[10];               // 宛先 (後ろの空白は除く)。一斉通知は "BLN1" など
  char text[APRS_MSG_TEXT_MAX + 1]; // 本文 (表示できるASCIIのみ。番号は除く)。Ack/Rej のときは ""
  char msgNo[6];                    // メッセージ番号 (なければ "")
  bool isAck, isRej;                // "ackNN" / "rejNN"
  char ackNo[6];                    // Ack/Rej の番号。返信Ack形式 "{MM}AA" の AA もここに入る
};

// 気象データ (APRS仕様書 12章)。単位はメートル法に直してある。has〜 が false の項目は送られていない(または不明)
struct AprsWeather {
  bool     present;                 // 気象データを含むパケット (シンボル '_' の位置レポート、またはデータ種別 '_')
  bool     hasWindDir;   uint16_t windDirDeg;     // 風向 (度、風が吹いてくる方向)
  bool     hasWindSpeed; double   windSpeedMs;    // 風速 (1分平均、m/s)
  bool     hasGust;      double   gustMs;         // 突風 (直近5分の最大、m/s)
  bool     hasTemp;      double   tempC;          // 気温 (℃)
  bool     hasRain1h;    double   rain1hMm;       // 雨量: 直近1時間 (mm)
  bool     hasRain24h;   double   rain24hMm;      // 雨量: 直近24時間 (mm)
  bool     hasRainMid;   double   rainMidMm;      // 雨量: 0時から (mm)
  bool     hasHumidity;  uint8_t  humidityPct;    // 湿度 (%)
  bool     hasPressure;  double   pressureHpa;    // 気圧 (hPa)
  bool     hasLuminosity; uint16_t luminosityWm2; // 日射 (W/m2)
};

struct AprsPacket {
  char     src[PARSE_CALL_MAX];     // 送信元 (SSIDが0でなければ "-N" 付き)
  char     dest[PARSE_CALL_MAX];    // 宛先
  char     via[PARSE_VIA_MAX];      // デジパス。使用済み(H bit)の最後のものに '*'。なければ ""
  AprsKind kind;
  char     dti;                     // データタイプ識別子 (情報フィールドの先頭)

  bool     hasPos;
  double   latDeg, lonDeg;          // 北緯・東経が正。曖昧化されているときは、その範囲の中央
  uint8_t  ambiguity;               // 0〜4
  char     symTable, symCode;

  bool     hasCourse;               // 進行方向あり(不明=0 は false)
  uint16_t courseDeg;               // 0〜359 (360は0として扱う)
  bool     hasSpeed;
  uint16_t speedKnots;              // ノット (表示はメートル法: aprs_knots_to_kmh)

  bool     compressed;              // 圧縮形式の位置 (APRS_KIND_POSITION のとき)
  bool     hasAltitude;
  double   altitudeM;               // メートル

  AprsWeather weather;              // 気象データ (weather.present のときだけ有効)
  AprsMessage msg;                  // メッセージ (msg.present のときだけ有効)

  const char* micEMessage;          // Mic-Eのメッセージ ("Off Duty" など。不明は "")。静的文字列
  char     model[MICE_MODEL_MAX];  // 機種。Mic-E はコメント末尾の印から ("FTM-300D" など。表にない印は "不明(_9)")、
                                    // それ以外は宛先 (TOCALL) から ("YAAC" など。0.14.0)。分からなければ ""
  char     comment[PARSE_COMMENT_MAX]; // コメント(表示できるASCIIのみ。拡張・高度の部分は除く)
};

// 宛先 (TOCALL、SSID は無視) から機種名を返す。表にない・汎用の "APRS" は NULL (Mic-E の宛先には使わない)
const char* aprs_tocall_model(const char* dest);
// フレーム(アドレス部〜情報フィールド。FCSなし)を読み解く。
// 戻り値: AX.25のアドレス部が読めたとき true。false なら out の中身は未定義。
bool aprs_parse_frame(const uint8_t* frame, size_t len, AprsPacket* out);

// TNC2形式の文字列 "SRC>DEST,VIA*:INFO" を out に作る (ログ用)。戻り値: 長さ。読めない/入らないときは 0。
// 情報フィールドの表示できないバイト(0x20未満、0x7F以上)は \xHH、バックスラッシュは \\ にする。
size_t aprs_tnc2_string(const uint8_t* frame, size_t len, char* out, size_t cap);

// ノットをkm/hに変換
double aprs_knots_to_kmh(double knots);

// 2点間の大圏距離(km)。地球半径 6371.0088km
double geo_distance_km(double lat1, double lon1, double lat2, double lon2);

// 点1から点2への初期方位(度、0以上360未満、真北が0、時計回り)
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);

#endif // PACKET_PARSE_H
