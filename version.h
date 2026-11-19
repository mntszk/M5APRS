// version.h
// ファームウェアのバージョン。リリースのたびに FW_VERSION を手で書き換える。
//   形式: メジャー.マイナー.パッチ (セマンティック バージョニング)
//     メジャー: 互換性のない変更 (設定の保存形式が変わる など)
//     マイナー: 機能の追加
//     パッチ  : 不具合の修正
// ビルド日時はコンパイラが自動で入れる (__DATE__ / __TIME__)。

#ifndef VERSION_H
#define VERSION_H

#define FW_NAME          "M5APRS"
#define FW_COPYRIGHT     "Copyright (c) 2026 Minato Suzuki"   // バージョン情報に出す (LICENSE と同じ)
#define FW_VERSION       "0.14.0"
#define FW_BUILD_DATE    __DATE__      // 例: "Oct 18 2026"
#define FW_BUILD_TIME    __TIME__      // 例: "12:34:56"

// 対応するインターフェース基板の版 (hardware/)
#define HW_INTERFACE_REV "IF v1"

#endif
