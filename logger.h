// logger.h
// microSDへの記録 (受信ログ=CSV、動作ログ)。
// 記録はまずRAMの待ち行列に入れ、logger_service() がSDへ書く。
//   - 書くのは、受信中(busy)でないときだけ。SD書き込みは数十msかかることがあり、受信のサンプルを取りこぼすため。
//   - SDが使えなくても、送受信と画面表示は続く。使えるようになったら、溜まった分を書く。
// 実際のSD操作は LogSink (logger_sd.cpp)、SPIバスの排他は LogBusGuard で外から与える。
// このファイル自体はM5Stack/Arduinoの部品に依存しない (パソコンでテストできる)。
//
// ファイル (LOG_DIR の下、日付はGPSのUTC。得られないときは nodate):
//   受信ログ  rx_YYYYMMDD.csv    ヘッダ: time_utc,src,via,type,lat,lon,alt_m,dist_km,bearing_deg,symbol,comment,raw
//   動作ログ  ops_YYYYMMDD.log   ヘッダ: time_utc,uptime_ms,event,detail
// 生パケット(raw)はTNC2形式。表示できないバイトは \xHH、バックスラッシュは \\ 。

#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include "packet_parse.h"

#define LOGGER_QUEUE_BYTES  8192    // 書き込み待ちを溜めるRAM
#define LOGGER_LINE_MAX     1400    // 1行の最大長 (超えたら生パケットを切り詰める)
#define LOGGER_BATCH_MAX    2048    // 1回のSD書き込みの最大バイト数
#define LOGGER_RETRY_MS     10000   // SDの書き込みに失敗したあと、再試行するまでの時間

struct LogTime {
  bool valid;
  uint16_t year; uint8_t month, day, hour, minute, second;
};

// UTCの日時を 1970-01-01 からの秒にする (再起動をまたいで経過時間を出すため)。無効・範囲外なら 0
static inline uint32_t log_time_epoch(const LogTime* t) {
  if (!t || !t->valid || t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31) return 0;
  int y = t->year, m = t->month;
  if (m <= 2) y--;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->day - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  return (uint32_t)(days * 86400L + t->hour * 3600L + t->minute * 60L + t->second);
}

struct LogOwnPos {            // 距離・方位の計算に使う自局の位置
  bool valid;
  double latDeg, lonDeg;
};

// SDへの追記。dir が無ければ作り、name のファイルが新規(または空)なら header を先頭に書いてから data を追記する。
// 成功したら true。
struct LogSink {
  void* ctx;
  bool (*append)(void* ctx, const char* dir, const char* name, const char* header, const char* data, size_t len);
};

// LCDとSDが同じSPIバスを使うための排他。lock が false を返したら、その回の書き込みは見送る。
struct LogBusGuard {
  bool (*lock)(void);
  void (*unlock)(void);
};

struct Logger {
  LogSink     sink;
  LogBusGuard guard;
  char        dir[32];
  uint8_t     q[LOGGER_QUEUE_BYTES];
  uint32_t    head, used;            // 待ち行列 (バイト単位の循環)
  char        batch[LOGGER_BATCH_MAX];
  bool        sdFailed;              // 直近の書き込みに失敗している
  uint32_t    lastFailMs;
  int         lastGps;               // 前回記録したGPS状態 (-1=まだ)
  uint32_t    seenOverruns, seenDiscards, seenQueueDrops, reportedDropped;
  // 統計
  uint32_t    entriesQueued, entriesWritten, entriesDropped, writeErrors;
};

void logger_init(Logger* lg, const LogSink* sink, const char* dir);
void logger_set_bus_guard(Logger* lg, const LogBusGuard* guard);

// 受信ログに1件追加。pkt は aprs_parse_frame の結果、frame/len は FCSなしのフレーム。
// ownPos が有効で pkt に位置があれば、距離・方位を入れる。待ち行列が満杯なら、古いものから捨てて入れる。
bool logger_log_rx(Logger* lg, const AprsPacket* pkt, const uint8_t* frame, size_t len,
                   const LogTime* t, const LogOwnPos* ownPos, uint32_t nowMs);

// 動作ログに1件追加 (event は英数字の短い名前、detail は自由な文字列)
bool logger_log_event(Logger* lg, const char* event, const char* detail, const LogTime* t, uint32_t nowMs);

// 送信したパケットを動作ログに記録
bool logger_log_tx(Logger* lg, const uint8_t* frame, size_t len, const LogTime* t, uint32_t nowMs);

// GPS状態 (GpsStatus の値) が変わったときだけ動作ログに記録
void logger_note_gps_status(Logger* lg, int status, const LogTime* t, uint32_t nowMs);

// 受信まわりの累積カウンタが増えていたら、動作ログに記録 (afsk_hw_stats の値をそのまま渡す)
void logger_note_counters(Logger* lg, uint32_t ringOverruns, uint32_t rxDiscards, uint32_t queueDropped,
                          const LogTime* t, uint32_t nowMs);

// SDへ書き込む。busy が true (送信中・受信中) のときは何もしない。
// 1回の呼び出しで書くのは、同じファイル向けの連続した分 (最大 LOGGER_BATCH_MAX バイト)。
// 戻り値: 書き込んだ件数。
int logger_service(Logger* lg, uint32_t nowMs, bool busy, const LogTime* t);

bool     logger_sd_ok(const Logger* lg);        // false なら画面に「SDエラー」を出す
uint32_t logger_pending_bytes(const Logger* lg);

// ---- 書式 (テスト用に公開) ----
// CSVの1項目を out に入れる。, " 改行を含むときは "..." で囲み、" は "" にする。戻り値: 長さ (入らないときは 0)
size_t logger_csv_field(char* out, size_t cap, const char* s);
// 受信ログの1行 (改行つき)
size_t logger_format_rx_line(char* out, size_t cap, const AprsPacket* pkt, const uint8_t* frame, size_t len,
                             const LogTime* t, const LogOwnPos* ownPos);
// 動作ログの1行 (改行つき)
size_t logger_format_event_line(char* out, size_t cap, const char* event, const char* detail,
                                const LogTime* t, uint32_t nowMs);

#endif // LOGGER_H
