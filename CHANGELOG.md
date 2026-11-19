# 変更履歴 / Changelog

各版の主な変更です。日付は版を上げた日です。
Main changes in each version. Dates are when the version was bumped.

## 0.14.0 (2026-11-17)
- 宛先 (TOCALL) を aprs-deviceid で割り当てられた `APM5AP` に変更
- Mic-E 以外のパケットも、宛先から機種名を表示 (aprs-deviceid の表、約480件)
- TOCALL changed to `APM5AP`, allocated by aprs-deviceid
- Device names are now shown for non-Mic-E packets too, identified from the destination (aprs-deviceid table)

## 0.13.3 (2026-11-04)
- 信号があるときだけ復調する (M5Stack 内部の雑音で偽のエラーを数えない)
- 受信エラーを「CRC不良」と「雑音」に分けて表示
- MARK/SPACE の強さの比を正しく表示 (以前は比が大きいほど小さく出ていた)。直近に受けたパケットの比も表示
- 設定メニューに「記録の消去」(受信一覧・WX局・メッセージ・レーダーの局)
- Decode only while a signal is present (no false errors from internal noise)
- RX errors are split into CRC errors and noise
- Correct MARK/SPACE ratio display, also for the last received packet
- "Erase records" in the settings menu

## 0.13.2 (2026-10-31)
- config.txt の上限を 4KB から 6KB に / config.txt limit raised from 4 KB to 6 KB

## 0.13.1 (2026-10-31)
- 受信音を全体に約 1/3 (−10dB) に / Chimes made about 10 dB quieter

## 0.13.0 (2026-10-31)
- 受信音: パケット・折り返し・Ack・メッセージで音を分ける。音量はモニター音量と別に設定
- ヘッダーに受信音の音量を示す鈴のアイコン
- 受信が始まったら稲妻のアイコンをすぐ表示
- いちばん暗い明るさをさらに暗く
- Chimes for packets, own packets repeated by a digipeater, acks and messages, with their own volume
- Bell icon for the chime volume in the header
- The receive icon appears as soon as a signal starts
- Darker minimum brightness

## 0.12.1 (2026-10-31)
- SSID 1〜4・15 の説明に日本の慣例を併記 / SSID 1-4 and 15 descriptions show the Japanese conventions

## 0.12.0 (2026-10-31)
- ビーコン自動送信に「フレックス」(八重洲・ケンウッドのスマートビーコニングと同じ方式。値は config.txt)
- "Flexible" automatic beaconing (works like SmartBeaconing of Yaesu and Kenwood radios; parameters in config.txt)

## 0.11.0 (2026-10-30)
- 圧縮形式の位置を受信 / Compressed position reports are decoded
- 代替表のシンボルの一部をプライマリ表のアイコンで表示 / Some alternate-table symbols use primary-table icons

## 0.10.2 (2026-10-29)
- いちばん暗い明るさを暗く、受信詳細の1行目を短く / Darker minimum brightness, shorter first line of the RX detail

## 0.10.1 (2026-10-29)
- レーダーの時間に 6・12・24 時間、文字のはみ出しを修正 / Radar time ranges of 6, 12 and 24 hours, text overflow fixed

## 0.10.0 (2026-10-29)
- レーダー画面 / Radar screen

## 0.9.1 (2026-10-29)
- MIT ライセンスと部品のライセンス一覧 / MIT license and the list of third-party licenses

## 0.9.0 (2026-10-29)
- 局ごとの設定を microSD の config.txt に移し、ビルド前のファイル編集を不要に
- Station settings moved to config.txt on the microSD card; no files to edit before building

## 0.8.0 (2026-10-29)
- シンボルと SSID を別々に設定、シンボルのアイコン / Symbol and SSID set separately, symbol icons

## 0.7.0 (2026-10-28)
- 画面の表示を日本語・英語で切り替え / Screen language: Japanese or English

## 0.6.0 (2026-10-28)
- 画面の明るさの設定 / Screen brightness setting

## 0.5.0 (2026-10-27)
- 受信一覧の種類別表示と詳細画面、Mic-E の機種名 / Received list by type, detail view, Mic-E device names

## 0.4.0 〜 0.4.4 (2026-10-27)
- 受信一覧・WX・メッセージを再起動後に復元 (0.4.0)
- 位置をメッセージ機能ありの `=` で送る、重複パケットをまとめる、Ack の判定の修正など (0.4.1〜0.4.4)
- Received list, WX stations and messages are restored after a restart (0.4.0)
- Positions are sent with `=` (messaging capable), duplicate packets are merged, ack handling fixes (0.4.1-0.4.4)

## 0.3.0 (2026-10-27)
- ヘッダーの状態アイコン、枠で囲んだ2行のボタン表示 / Header status icons, boxed two-row button labels

## 0.2.0 〜 0.2.1 (2026-10-19)
- ボタン操作の見直し、ビーコンの自動送信、WX 画面、メッセージ (CardKB2 での入力に対応)
- Reworked buttons, automatic beaconing, WX screen, messaging (with CardKB2 keyboard input)

## 0.1.0 (2026-10-18)
- 最初の版: GPS の位置を 1200bps AFSK で送信、受信した APRS パケットの復号と表示、バージョン情報
- First version: sends the GPS position as 1200 bps AFSK, decodes and shows received APRS packets, version information
