// position_source.h
// 自局の位置を、設定(取得元)に従って決める。送信するパケット、受信一覧の距離・方位、画面表示のすべてが
// この結果を使う。取得元は自動では切り替えない (誤って古い位置を送らないため。手動で切り替える)。
//   GPS  : 現在のGPSの位置 (最終受信から GPS_LAST_FIX_VALID_SEC 以内)
//   最後 : 不揮発領域に保存された最後の測位位置
//   固定 : config.txt の固定位置 (g_cfg.fixed) から選んだ位置
// M5Stack/Arduinoの部品に依存しない(パソコンでテストできる)。
#ifndef POSITION_SOURCE_H
#define POSITION_SOURCE_H

#include <stdint.h>
#include "settings.h"
#include "nmea.h"
#include "last_pos.h"
#include "fixed_pos.h"

enum PosReason {
  POS_OK = 0,
  POS_GPS_NO_DATA,      // GPS: NMEAが届いていない
  POS_GPS_NO_FIX,       // GPS: 測位できていない (または最終測位から5分を超えた)
  POS_LAST_NONE,        // 最後: 保存された位置がない
  POS_FIXED_UNSET       // 固定: 選んだ欄が未設定
};

struct OwnPos {
  bool       valid;
  uint8_t    source;            // POSITION_SOURCE_*
  PosReason  reason;            // valid==false のときの理由
  double     latDeg, lonDeg;
  bool       hasAlt;  double altM;
  bool       hasCourse; double courseDeg;     // GPSのときだけ
  bool       hasSpeed;  double speedKnots;    // GPSのときだけ
  bool       stale;             // GPS: 測位が途切れていて、最終測位位置を使っている
  uint32_t   ageSec;            // GPS: 位置を受信してからの秒数
  char       label[16];         // 固定位置の名前 (画面表示用)。それ以外は ""
};

void position_resolve(const SettingsMgr* s, const GpsState* g, const LastPosMgr* lp, uint32_t nowMs, OwnPos* out);

#endif
