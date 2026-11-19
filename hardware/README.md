# M5APRS interface board (IF v1)

M5Stack Basic と無線機 (マイク・スピーカー端子) をつなぐインターフェース基板の設計データです。

| File | 内容 |
|---|---|
| `M5APRS_interface.kicad_pro` / `.kicad_sch` / `.kicad_pcb` | KiCad のプロジェクト、回路図、基板 |
| `M5APRS_interface.svg` | 回路図 (画像) |
| `M5APRS_interface_BOM.csv` | 部品表 |
| `gerber/M5APRS_interface_1.zip` | 製造用データ (ガーバー・ドリル) |
| `fabrication-toolkit-options.json` | 製造データ出力の設定 |
| `radio_compat.md` | 無線機との相性の記録 |

## License / ライセンス

Copyright (c) 2026 Minato Suzuki

SPDX-License-Identifier: CERN-OHL-P-2.0

The hardware design files in this directory are licensed under the
CERN Open Hardware Licence Version 2 - Permissive (CERN-OHL-P v2).
See `LICENSE` in this directory, or https://ohwr.org/cern_ohl_p_v2.txt

You may redistribute and modify this source and make products using it
under the terms of the CERN-OHL-P v2. This source is distributed WITHOUT
ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY
QUALITY AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN-OHL-P v2
for applicable conditions.

このディレクトリの基板の設計データは CERN-OHL-P v2 (寛容型のオープンハードウェア・ライセンス) です。
表示 (著作権・ライセンス・改変の記録) を残せば、自由に使用・改変・再配布でき、基板を作って配ったり売ったりもできます。
改変したデータを配るときは、改変したことを、日付と簡単な内容とともに書き添えてください (CERN-OHL-P v2 の 3.3)。
保証はありません。無線機への接続は自己責任で行ってください。

ファームウェア (このリポジトリのソフトウェア) は MIT ライセンスです (リポジトリ直下の `LICENSE`)。

### Third-party parts of the design / 設計に含まれる他の部品
- KiCad の標準ライブラリのシンボル・フットプリント (CC BY-SA 4.0。KiCad ライブラリの例外条項により、これらを使った設計にはその条件は及ばない)
- RV1・RV2 (Bourns 3362P) のフットプリント `TRIM_3362P-1-102LF` は SnapMagic (旧 SnapEDA) のもの。同社の利用規約 (Design License) に基づき、基板の設計データに組み込んで配布している。このフットプリント単体は CERN-OHL-P v2 の対象外で、設計データから取り出して単体で配布することはできない。単体で必要なときは SnapMagic から入手してほしい
  / The footprint `TRIM_3362P-1-102LF` of RV1 and RV2 (Bourns 3362P) is from SnapMagic (formerly SnapEDA). It is included in this board design under the SnapMagic Design License. The footprint by itself is not covered by the CERN-OHL-P v2 and must not be extracted from the design and distributed on its own. Get it from SnapMagic if you need it separately.
