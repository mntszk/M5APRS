// beacon_sched.h
// ビーコンの自動送信の時刻を決める部分 (仕様 4.3.3)。M5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
//
// 動き:
//   - 間隔のモード (オフ / Ackのみ / 1〜60分) を選ぶと、その時点から数え始め、間隔が過ぎたら送る
//   - 送ったら(自動・手動とも) beacon_note_tx で数え直す
//   - 送る時刻に他局が送信中(信号あり)なら、止むまで待つ。最大 BEACON_BUSY_WAIT_MS 待っても止まなければ、その回は見送る
//   - 自局が送信中・調整トーン中 (canTx=false) は、送れるようになるまで待つ (見送りにはしない)
//   - フレックス (0.12.0): 八重洲・ケンウッドのスマートビーコニングと同じ方式。設定値は config.txt (AppConfig.flex)
//       速度 < LOW SPEED          : SLOW RATE ごと
//       速度 > HIGH SPEED         : FAST RATE ごと
//       その間                    : FAST RATE × HIGH SPEED ÷ 速度 ごと
//       曲がったとき              : 前回送ったときの進行方向から「TURN ANGLE + TURN SLOPE×10 ÷ 速度」度より大きく変わり、
//                                   前回から TURN TIME 以上たっていれば送る (速度 LOW SPEED 以上のときだけ)
//       遅いときの距離 (独自)     : 速度 < LOW SPEED で、前回送った位置から flex_slow_dist (m) 以上離れ、
//                                   TURN TIME 以上たっていれば送る (0 なら使わない)
//       GPSが使えないとき          : SLOW RATE ごと (位置の取得元が GPS 以外、測位なし、速度が無いとき)
//     config.txt にフレックスの値がそろっていなければ (beacon_set_flex に NULL)、ビーコンは送らない
//
// 使い方 (loop から):
//   beacon_set_mode(&b, settings.cur.autoTx, now);   // 設定が変わったら (同じなら何もしない)
//   beacon_set_flex(&b, ok ? &g_cfg.flex : NULL);   beacon_set_motion(&b, gpsOk, kmh, hasCourse, course, lat, lon);   // フレックス用
//   switch (beacon_poll(&b, now, channelBusy, canTx)) { case BEACON_SEND: ... 送信 ...; case BEACON_SKIP_BUSY: ... }

#ifndef BEACON_SCHED_H
#define BEACON_SCHED_H

#include <stdint.h>
#include "app_config.h"

// 自動送信の選択肢: 0=オフ、1=Ackのみ、2〜=間隔 (分)、最後=フレックス。fw_config.h に無ければ既定値
#ifndef AUTO_TX_INTERVALS_MIN
#define AUTO_TX_INTERVALS_MIN   { 1, 2, 3, 5, 10, 15, 30, 60 }
#define AUTO_TX_INTERVAL_COUNT  8
#endif
#ifndef DEFAULT_AUTO_TX
#define DEFAULT_AUTO_TX         0          // 0=オフ
#endif
#ifndef BEACON_BUSY_WAIT_MS
#define BEACON_BUSY_WAIT_MS     30000      // 他局の送信が止むのを待つ最長時間
#endif

#define AUTO_TX_OFF       0
#define AUTO_TX_ACK_ONLY  1
#define AUTO_TX_FLEX      (2 + AUTO_TX_INTERVAL_COUNT)
#define AUTO_TX_CHOICES   (3 + AUTO_TX_INTERVAL_COUNT)

// 選択肢の間隔 (ms)。オフ/Ackのみ/範囲外は 0
uint32_t auto_tx_interval_ms(uint8_t mode);
// 選択肢の間隔 (分)。オフ/Ackのみ/範囲外は 0
uint16_t auto_tx_interval_min(uint8_t mode);
// Ackを自動で返すか (オフ以外)
bool auto_tx_ack_enabled(uint8_t mode);
// フレックスか
bool auto_tx_is_flex(uint8_t mode);
// ビーコンを自動で送る選択か (間隔またはフレックス)
bool auto_tx_beacon(uint8_t mode);

enum BeaconDecision { BEACON_IDLE = 0, BEACON_SEND, BEACON_SKIP_BUSY };

struct BeaconSched {
  uint8_t  mode;          // 自動送信の選択 (AUTO_TX_*)
  uint32_t lastMs;        // 数え始めた時刻 (モードを選んだ時刻、または最後に送った/見送った時刻)
  bool     waiting;       // 送る時刻を過ぎて、他局の送信が止むのを待っている
  uint32_t waitSinceMs;
  uint32_t sent, skipped; // 自動で送った回数・見送った回数
  // フレックス
  const FlexConfig* flex; // NULL = 設定がそろっていない (送らない)
  bool     mv;            // GPSの速度が使える
  double   kmh;           // 今の速度
  bool     hasCourse;  double course;   // 今の進行方向
  double   lat, lon;      // 今の位置 (mv のとき)
  bool     refCourseOk; double refCourse;   // 前回送ったときの進行方向
  bool     refPosOk;    double refLat, refLon;   // 前回送ったときの位置
};

void beacon_init(BeaconSched* b, uint8_t mode, uint32_t nowMs);
// モードを変える。同じモードなら何もしない (数え直さない)
void beacon_set_mode(BeaconSched* b, uint8_t mode, uint32_t nowMs);
// 送信した (自動・手動とも)。ここから数え直す
void beacon_note_tx(BeaconSched* b, uint32_t nowMs);
// 毎回 loop から呼ぶ。channelBusy=他局の信号あり、canTx=自局が送信できる (送信中・調整トーン中でない)
BeaconDecision beacon_poll(BeaconSched* b, uint32_t nowMs, bool channelBusy, bool canTx);
// 次の送信までの残り時間 (ms)。自動で送らないモードでは 0。送る時刻を過ぎていれば 0。
// フレックスでは今の速度での間隔から出す (曲がったときはこれより早く送ることがある)
uint32_t beacon_remaining_ms(const BeaconSched* b, uint32_t nowMs);
// フレックスの設定値 (NULL = そろっていない)
void beacon_set_flex(BeaconSched* b, const FlexConfig* f);
// 今の動き。valid=false は GPS が使えない (SLOW RATE で送る)。毎回 poll の前に呼ぶ
void beacon_set_motion(BeaconSched* b, bool valid, double kmh, bool hasCourse, double courseDeg, double latDeg, double lonDeg);
// フレックスの今の送信間隔 (ms)。フレックスでない・設定がないときは 0
uint32_t beacon_flex_interval_ms(const BeaconSched* b);

#endif
