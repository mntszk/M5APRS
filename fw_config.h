// fw_config.h
// ファームウェアに固定の設定 (ピン割り当て、AFSKの値、バッファの大きさ、待ち時間、各種の初期値)。
// 局ごとに変える設定 (コールサイン、コメント、固定位置、デジパス、定型文、宛先の候補) と、
// 無線機・基板に合わせて調整する値 (送信レベル、PTTのタイミングなど) は、microSD の /aprs/config.txt に書く
// (app_config.h。0.8.0 までの config.h は使わない)。
// 送受信まわりの値は、ID-31 / ID-52 / C601 / C701 と IF v1 基板の組み合わせで動作を確認した値。

#ifndef FW_CONFIG_H
#define FW_CONFIG_H

#include <stdint.h>

// ===== APRSパケット設定 =====
#define APRS_DTI           '='        // データタイプ識別子: 位置(時刻なし・メッセージ機能あり)。メッセージを使わないなら '!'
#define DEST_CALLSIGN      "APM5AP"   // 宛先 (TOCALL)。aprs-deviceid で M5APRS に割り当て済み (2026-11)。改造版はそれぞれ取得すること
#define DEST_SSID          0

// 位置情報の曖昧化の初期値 (0=オフ, 1〜4: 緯度経度の下位の桁を順にスペース化)
#define DEFAULT_AMBIGUITY_LEVEL  0
#define MAX_AMBIGUITY_LEVEL      4

// 進行方向・速度、高度を送信に付けるかの初期値 (設定メニューでオンオフ。初期値はすべてオフ)
#define DEFAULT_SEND_COURSE_SPEED  false   // 進行方向・速度 (CSE/SPD)
#define DEFAULT_SEND_ALTITUDE      false   // 高度 (/A=aaaaaa)

// ===== デジパスの候補 (設定メニューから選択。中身は config.txt の digipath1〜3) =====
#define DIGIPATH_CHOICE_COUNT    3
#define DEFAULT_DIGIPATH_INDEX   1    // 初期値: WIDE1-1

// ===== 自局のシンボルとSSID (設定メニューで別々に変えられる。これは初期値) =====
// シンボルは symbols.cpp の SYMBOL_CHOICES にあるもの (テーブル文字+コード)。無いものを書くと Car "/>" になる。
// SSID は 0〜15 (0 = SSIDなし。9 = モービル、7 = ハンディ機 などがよく使われる)。
#define DEFAULT_SYMBOL   "/>"   // Car
#define DEFAULT_SSID     9


// ===== 位置の取得元 (設定メニューで選択) =====
//   GPS    : 現在のGPSの位置 (最終受信から GPS_LAST_FIX_VALID_SEC 以内)。測位が切れても、自動では切り替わらない。
//   最後   : 最後に測位した位置 (不揮発領域に保存。保存のしかたは下の LAST_POS_*)
//   固定   : config.txt の fixed1〜8 から選んだ位置
#define POSITION_SOURCE_GPS    0
#define POSITION_SOURCE_LAST   1
#define POSITION_SOURCE_FIXED  2
#define DEFAULT_POSITION_SOURCE  POSITION_SOURCE_GPS

// 固定位置の欄の数 (中身は config.txt の fixed1〜8)
#define FIXED_POSITION_COUNT   8
#define DEFAULT_FIXED_INDEX    0

// 「最後の位置」を不揮発領域に保存するタイミング: 起動後の最初の測位で1回 (保存済みと50m以内なら書かない)、
// その後は「前回の保存位置から LAST_POS_SAVE_DISTANCE_M 以上動き、かつ LAST_POS_SAVE_INTERVAL_SEC 以上経った」とき。
// フラッシュの書き込み回数に限りがあるため、頻繁には書かない。
#define LAST_POS_SAVE_DISTANCE_M     500
#define LAST_POS_SAVE_INTERVAL_SEC   600

// ===== 設定値の保存 (不揮発領域 / Preferences) =====
#define NVS_NAMESPACE      "aprs_cfg"

// ===== ピン割り当て =====
// GPS (UART2) ※動作実績のある設定
#define GPS_BAUD           115200     // AT6668の初期設定
#define GPS_RX_PIN         16
#define GPS_TX_PIN         17

// GPSが途絶えた場合の扱い
#define GPS_LAST_FIX_VALID_SEC   300    // 最終測位位置を使い続ける時間(5分)。超えたら未測位として送信しない
#define FLEX_FIX_MAX_AGE_SEC     5      // フレックスで速度・進行方向に使う測位の古さの上限 (これより古ければ GPS が使えないとして SLOW RATE)
#define GPS_NO_DATA_TIMEOUT_MS   5000   // NMEAがこの時間届かなければ「GPS受信なし」と表示する

// 送信音声出力 (DAC)。無線機のマイク端子へ。ESP32のDACは GPIO25 (チャネル1) と GPIO26 (チャネル2)。
#define AFSK_TX_DAC_PIN    26

// 送信音のモニター出力 (DAC)。M5Stack Basic の内蔵スピーカーは GPIO25 につながっている。
// 0 にするとモニターなし。AFSK_TX_DAC_PIN と同じピンにはできない (その場合はモニターなし)。
#define AFSK_TX_MONITOR_PIN  25

// モニター音量 (設定メニューで切り替え)。0=ミュート、1〜MONITOR_LEVEL_MAX。
// 値は、無線機へ出す波形に対する大きさ (256 = 同じ大きさ)。段階ごとに書く。
#define MONITOR_LEVEL_MAX      4
#define DEFAULT_MONITOR_LEVEL  2
#define DEFAULT_CHIME_LEVEL    2      // 受信音の音量 (0=鳴らさない〜4。設定メニューで変える)
#define CHIME_AMP_TABLE        { 0, 1, 4, 9, 20 }   // 受信音の音量ごとの振れ幅 (DACの中心128から±。0.13.0 は ±4/11/26/60)
#define MONITOR_SCALE_TABLE    { 0, 20, 51, 115, 256 }   // 要素数は MONITOR_LEVEL_MAX + 1

// 画面の明るさ (設定メニューで切り替え)。バックライトの値 (1〜255) を暗い順に5段階
//   1: 夜間 … 4: 日中 (0.4.x までの明るさ) 5: 直射日光の下
#define BRIGHTNESS_LEVEL_COUNT    5
#define BRIGHTNESS_TABLE          { 1, 20, 60, 120, 255 }
#define DEFAULT_BRIGHTNESS_LEVEL  3        // 0から数えた番号 (3 = 4段目)

// 画面の言語 (設定メニュー「言語 / Language」で切り替え)。0=日本語 1=English
#define DEFAULT_LANGUAGE          0

// レーダー画面 (受信した局を自局の周りに描く)。覚えておく局の数と、距離・時間の初期値 (画面で切り替え、本体に保存)
#define RADAR_STATION_COUNT       40
#define DEFAULT_RADAR_RANGE       7        // 0〜6 = 1/2/5/10/20/50/100km、7 = 自動
#define DEFAULT_RADAR_TIME        2        // 0〜6 = 10分/30分/1時間/3時間/6時間/12時間/24時間

// PTT出力 (トランジスタ 2SC1815 のベースへ。LED+抵抗などを介して無線機のマイク線をGNDへ落とす)。
// 送信するとき: PTTオン → PTT_ON_DELAY_MS 待つ → AFSK送出 → PTT_OFF_DELAY_MS 待つ → PTTオフ。
// -1 にするとPTT出力なし (従来どおり。待ち時間も入れない)。起動時は必ずオフ。
#define PTT_PIN              5
// PTTの極性と待ち時間は config.txt (ptt_active_high, ptt_on_delay, ptt_off_delay)
#define PTT_MAX_ON_MS        10000    // PTTを入れ続けてよい最長時間。超えたら送信を中止して強制オフ
#define CALIB_MAX_ON_MS      30000    // 調整トーン(設定メニューの「調整トーン」)を出し続ける最長時間。超えたら自動で止める

// シリアルモニタ (115200bps) への診断出力。PTTのタイミング、送信・受信したパケット、受信の状態、定期的な状態の行を出す。
#define DIAG_SERIAL            1        // 1: 出す。0: 出さない
#define DIAG_STATUS_INTERVAL_MS  5000   // 状態の行を出す間隔 (ms)。0 なら出さない

// 受信音声入力 (ADC1_CH0、入力専用ピン)
#define AFSK_RX_ADC_PIN    36

// microSD (M5Stack Basic内蔵スロット。LCDとSPIバスを共有)
#define SD_CS_PIN          4

// ===== 受信一覧・ログ =====
#define RX_LIST_COUNT      20         // 画面に出す受信一覧の件数(RAM上の直近分)。全件はmicroSDに保存する
#define LOG_DIR            "/aprs"    // ログの保存先ディレクトリ

// ===== AFSK基本パラメータ (Bell 202) =====
#define AFSK_BAUD          1200       // ボーレート
#define AFSK_MARK_HZ       1200       // MARK周波数
#define AFSK_SPACE_HZ      2200       // SPACE周波数

// ===== AFSK =====
// 送信サンプリング: 26400Hz → 1ビット=22サンプル、1200Hz=22サンプル/周期、2200Hz=12サンプル/周期(いずれも整数)
#define TX_SAMPLE_RATE_HZ  26400
// 受信サンプリング: 13200Hz → 1ビット=11サンプル
#define RX_SAMPLE_RATE_HZ  13200

// 受信の復調(直交相関検波)
#define RX_TONE_LEVEL_INIT     1000   // トーンごとの強さの平均の初期値(補正の初期値)

// 画面の受信状態「データ」(AFSKのパケットが来ている) とみなす、連続フラグ数。ノイズでの誤検出を避けるため3以上にする
#define RX_DATA_SYNC_FLAGS     3
// 受信結果(受信OK/FCS不良)を画面に出しておく時間(ms)
#define RX_RESULT_SHOW_MS      2000

// 受信バッファ
#define RX_RINGBUF_MS          100    // サンプルのリングバッファの長さ(ms)。SD書き込みなどの遅れを吸収する
#define RX_PACKET_QUEUE_COUNT  16     // 受信パケットの待ち行列の件数。満杯なら新しいパケットを破棄して件数を記録

// プリアンブル(パケット先頭のフラグ数)は config.txt (tx_preamble)
#define TX_TAIL_FLAGS      4          // パケット後尾のフラグ数

// 送信トーンの振幅は config.txt (tx_amp_mark, tx_amp_space)

// ===== ボタン操作 =====
// 短押し(画面の中の操作): A=前, B=決定(開く/切り替え/開始), C=次
// 長押し(どこでも同じ):   A=ビーコン送信, B=戻る(1つ上へ), C=設定メニュー(開く/閉じる)
#define LONG_PRESS_MS      1000       // 長押しと判定する時間

// ---- ビーコンの自動送信 (設定メニューで選び、本体に保存。電源を入れ直しても引き継ぐ) ----
// 選択肢: オフ / Ackのみ (ビーコンは出さず、自分宛てメッセージのAckだけ返す) / 下の間隔 (分)
#define AUTO_TX_INTERVALS_MIN   { 1, 2, 3, 5, 10, 15, 30, 60 }
#define AUTO_TX_INTERVAL_COUNT  8          // 上の個数
#define DEFAULT_AUTO_TX         0          // 保存値がないときの初期値 0=オフ 1=Ackのみ 2〜=上の間隔の順番+2
// 送る時刻に他局が送信中なら、止むまで待つ。この時間待っても止まなければ、その回は見送る
#define BEACON_BUSY_WAIT_MS     30000

// ---- メッセージ ----
#define MSG_STORE_COUNT        20          // 覚えておくメッセージの数 (受信・送信・一斉通知の合計)
#define MSG_RETRY_INTERVALS_S  { 30, 60, 120, 240, 480 }   // Ackが来ないときの送り直しの間隔 (秒)
#define MSG_RETRY_COUNT        5           // 上の個数 (送り直しの回数)
#define MSG_FINAL_WAIT_MS      60000       // 最後に送ってから、これだけAckを待って「応答なし」にする
#define MSG_BUSY_WAIT_MS       30000       // 他局の送信が止むのを待つ最長時間
// 本文の定型文と宛先の候補は config.txt (msg1〜8, friend1〜8)
#define MSG_PRESET_MAX    8
#define MSG_FRIEND_MAX    8
// キーボード (M5Stack CardKB2 / CardKB。GroveポートAにつなぐ。なくても動く)
#define CARDKB_I2C_ADDR   0x5F
#define CARDKB_I2C_FREQ   50000     // I2Cの速度 (Hz)。CardKB2は50kHzで安定 (100kHzだと入力が途切れることがある)

// ---- WX(気象)画面 ----
#define WX_STATION_COUNT  8          // 覚えておく気象局の数。超えたら受信がいちばん古い局と入れ替える
#define WX_STALE_MS       3600000UL  // これより長く受信がない局は灰色で表示する (60分)

// ===== 運用メモ(ソフトでは設定しない) =====
// 想定運用周波数: 431.04MHz (430MHz帯の1200bps AFSK用)。無線機側で設定する。
// 無線機: ICOM ID-31 をアナログFMで使用 (D-STARのDVモードは使わない)。

#endif // FW_CONFIG_H
