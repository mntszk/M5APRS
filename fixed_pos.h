// fixed_pos.h
// 緯度経度の妥当性 (ヘッダだけ。他に依存しない)。固定位置の欄が設定済みかは app_config.h の fixed_position_set
#ifndef FIXED_POS_H
#define FIXED_POS_H

#include "app_config.h"   // fixed_position_set (固定位置は config.txt)

// 緯度経度が範囲内で、両方0ではない (NaNは不可)
static inline bool pos_plausible(double lat, double lon) {
  if (!(lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)) return false;
  if (lat == 0.0 && lon == 0.0) return false;
  return true;
}

#endif
