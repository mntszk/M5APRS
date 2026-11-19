// last_pos.h
// 「最後の位置」: 最後に測位した位置を不揮発領域に保存しておき、次回起動後も使えるようにする。
// 保存先は LastPosBackend という小さな口で切り離してあり、このファイル自体はM5Stack/Arduinoの部品に依存しない
// (パソコンでテストできる)。ESP32用の口は last_pos_nvs.h / last_pos_nvs.cpp。
//
// 保存のタイミング (フラッシュの書き込み回数を減らすため、頻繁には書かない):
//   1. 起動後、現在のGPS測位が最初に得られたとき1回。ただし保存済みの位置と50m以内なら書かない。
//      (高度が分かるまで最大5秒待つ)
//   2. その後は、前回の保存位置から LAST_POS_SAVE_DISTANCE_M 以上動き、かつ前回の保存から
//      LAST_POS_SAVE_INTERVAL_SEC 以上経ったとき。
//   - 送信中・受信中 (busy) は書かない。書けなかったら10秒おいて再試行する。
//   - 保存するのは現在のGPSの測位 (最終受信から5分以内の古い位置は保存しない)。

#ifndef LAST_POS_H
#define LAST_POS_H

#include <stdint.h>
#include "app_config.h"
#include "fixed_pos.h"

struct LastPos {
  bool   valid;
  double latDeg, lonDeg;
  bool   hasAlt;
  double altM;
};

struct LastPosBackend {
  void* ctx;
  bool (*load)(void* ctx, LastPos* out);          // 保存値があって読めたら true
  bool (*save)(void* ctx, const LastPos* pos);    // 書けたら true
};

struct LastPosMgr {
  LastPos        cur;               // 保存されている(とみなす)位置
  LastPosBackend be;
  bool           savedThisBoot;     // 起動後に、保存の判断(保存、または保存済みと近いので不要)をした
  bool           haveFirstFix; uint32_t firstFixMs;
  uint32_t       lastSaveMs;
  uint32_t       failMs; bool failed;
  uint32_t       writeCount, writeErrors;
};


// 初期化: 保存値を読む (口が NULL でも動く=保存しない)。不正な保存値は使わない。
void last_pos_init(LastPosMgr* m, const LastPosBackend* be);

// loopから毎回呼ぶ。
//   fixValid: 現在のGPSの測位があるか (gps_get_status が GPS_FIX)
//   busy:     送信中・受信中なら true (その間は書かない)
// 戻り値: 書き込んだら true
bool last_pos_update(LastPosMgr* m, uint32_t nowMs, bool fixValid, double latDeg, double lonDeg,
                     bool hasAlt, double altM, bool busy);

#endif
