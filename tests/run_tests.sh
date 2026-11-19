#!/bin/bash
# パソコン上のテスト (ESP32は不要。g++ が必要)。
#   ./tests/run_tests.sh          初期値でテスト
#   ./tests/run_tests.sh fixed    固定位置をいくつか設定してテスト
# 設定 (config.txt の代わり) は tests/test_cfg.h。
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$ROOT"/*.h "$ROOT"/*.cpp "$W"/ 2>/dev/null
cp "$ROOT"/tests/test_*.cpp "$ROOT"/tests/test_cfg.h "$W"/; cp -r "$ROOT/tests/stubs" "$W"/
# fixed: 固定位置をいくつか設定した状態でテストする (tests/test_cfg.h)
EXTRA=""; [ "$1" = "fixed" ] && EXTRA="-DTEST_FIXED_POSITIONS"
cd "$W"; FAIL=0
run(){ n=$1; shift
  g++ -std=gnu++11 -Wall -Wextra -pedantic -O1 -fsanitize=address,undefined -Istubs -I. $EXTRA "$@" app_config.cpp -o "t_$n" 2>&1 | grep -E "error|warning|undefined" | head -5
  printf "%-10s" "$n"; out=$(./t_$n); r=$(echo "$out" | tail -1); echo "$r"; echo "$out" | grep "^FAIL" | head -10
  case "$r" in *" 0 failures") ;; *) FAIL=1;; esac; }
run tx test_afsk_tx_core.cpp afsk_tx_core.cpp packet_build.cpp
run rx test_afsk_rx_core.cpp afsk_rx_core.cpp afsk_tx_core.cpp packet_build.cpp
run parse test_packet_parse.cpp packet_parse.cpp packet_build.cpp
run nmea test_nmea.cpp nmea.cpp
run settings test_settings.cpp settings.cpp symbols.cpp settings_nvs.cpp beacon_sched.cpp
run appcfg test_app_config.cpp
run beacon test_beacon.cpp beacon_sched.cpp
run msg test_msg.cpp msg_core.cpp packet_parse.cpp packet_build.cpp
run ringq test_ring_queue.cpp packet_queue.cpp
run dedupe test_rx_dedupe.cpp rx_dedupe.cpp packet_build.cpp
run hw test_afsk_hw.cpp afsk_hw.cpp afsk_tx_core.cpp afsk_rx_core.cpp packet_queue.cpp packet_build.cpp packet_parse.cpp
run logger test_logger.cpp logger.cpp packet_parse.cpp packet_build.cpp nmea.cpp
run logger_sd test_logger_sd.cpp logger_sd.cpp logger.cpp packet_parse.cpp packet_build.cpp
run position test_position.cpp position_source.cpp last_pos.cpp last_pos_nvs.cpp tx_compose.cpp settings.cpp symbols.cpp settings_nvs.cpp beacon_sched.cpp nmea.cpp packet_build.cpp packet_parse.cpp
run diag test_diag.cpp diag.cpp packet_parse.cpp packet_build.cpp
run state test_state.cpp state_store.cpp ui_core.cpp ui_text.cpp msg_core.cpp position_source.cpp last_pos.cpp tx_compose.cpp settings.cpp symbols.cpp beacon_sched.cpp nmea.cpp packet_build.cpp packet_parse.cpp
run ui test_ui_core.cpp ui_core.cpp ui_text.cpp msg_core.cpp position_source.cpp last_pos.cpp tx_compose.cpp settings.cpp symbols.cpp beacon_sched.cpp nmea.cpp packet_build.cpp packet_parse.cpp
# 英語の対応表: 画面の日本語がすべて表にあるか、書式 (%s など) が左右で同じか、ボタンの文字が短いか
printf "%-10s" "i18n"
if python3 "$ROOT/tests/check_i18n.py" "$ROOT"; then :; else FAIL=1; fi
# 実機用のファイル (ui_m5.cpp と .ino) は、代用品で文法だけ確かめる
cp "$ROOT/M5APRS.ino" ino_main.cpp
printf "%-10s" "syntax"
if g++ -std=gnu++11 -Wall -Wextra -fsyntax-only -Istubs -I. ui_m5.cpp app_config_nvs.cpp logger_sd.cpp 2>&1 | grep -E "error|warning" | head -5 | grep . ||
   g++ -std=gnu++11 -fsyntax-only -Istubs -I. ino_main.cpp 2>&1 | grep -E "error" | head -5 | grep .; then FAIL=1; else echo "ui_m5.cpp, M5APRS.ino OK"; fi
[ $FAIL = 0 ] && echo "ALL PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
