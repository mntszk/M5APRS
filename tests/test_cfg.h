// test_cfg.h  (パソコン上のテスト用)
// テストでの設定 (config.txt の代わり)。main の最初に test_cfg_setup() を呼ぶ。
// コールサインは架空のもの。run_tests.sh fixed では固定位置もいくつか設定する (-DTEST_FIXED_POSITIONS)。
#ifndef TEST_CFG_H
#define TEST_CFG_H
#include <stdio.h>
#include "app_config.h"

#define MY_CALLSIGN        "N0CALL"
#define TX_AMP_MARK        (g_cfg.txAmpMark)
#define TX_AMP_SPACE       (g_cfg.txAmpSpace)
#define TX_PREAMBLE_FLAGS  (g_cfg.txPreambleFlags)
#define PTT_ACTIVE_HIGH    (g_cfg.pttActiveHigh)
#define PTT_ON_DELAY_MS    (g_cfg.pttOnDelayMs)
#define PTT_OFF_DELAY_MS   (g_cfg.pttOffDelayMs)
#define FIXED_POSITIONS    (g_cfg.fixed)
#define DIGIPATH_CHOICES   (g_cfg.digipath)
#define APRS_COMMENT       (g_cfg.comment)
#define MSG_PRESET_COUNT   (g_cfg.msgPresetCount)

static inline void test_cfg_setup() {
  app_config_defaults(&g_cfg);
  snprintf(g_cfg.callsign, sizeof(g_cfg.callsign), "%s", MY_CALLSIGN);
#ifdef TEST_FIXED_POSITIONS
  FixedPosition h = { "Home", 35.68123, 139.76712, true, 12.0 }, s2 = { "Spot2", 34.0, 135.5, false, 0.0 }, s4 = { "Spot4", -33.8688, 151.2093, true, 58.5 };
  g_cfg.fixed[0] = h; g_cfg.fixed[1] = s2; g_cfg.fixed[3] = s4;
#endif
}
#endif
