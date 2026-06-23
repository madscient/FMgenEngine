# FmGenEngine

**fmgen** ([cisc](mailto:cisc@retropc.net), 1998-2003) をコアとした、
[YMEngine](https://github.com/madscient/YMEngine) (ymfm版) と
**API (ABI) 互換**の Windows 向け FM 音源エンジン DLL。

ライセンス: FmGenEngine 独自コード (`src/`) は
**MIT License** ([`LICENSE`](./LICENSE))。同梱の fmgen 本体
(`extern/fmgen/`) は別ライセンス。詳細は「ライセンス」節を参照。

## 概要

`FmGenEngineApi.h` を一切変更せずに DLL の中身だけを ymfm → fmgen に
差し替えたもの。YMEngine 向けに書かれたアプリケーションは、リンクする
DLL を本プロジェクトのもの ( `FmGenEngineApi.dll` ) に差し替えるだけで
動作する (対応チップの範囲内であれば再コンパイルも不要)。

波形生成 (`FmEngine_Generate`) とオーディオ出力は分離されており、
DLL 側には特定のオーディオ API (WASAPI 等) への依存がない。
アプリケーション側のオーディオコールバックから `FmEngine_Generate` を
呼び出すことで任意のオーディオバックエンドと組み合わせられる。

## 対応チップ

| `FmChipType` / `FmChipTypeExt` | チップ | fmgen クラス | 備考 |
|---|---|---|---|
| `FM_CHIP_OPN`     | YM2203 (OPN)   | `FM::OPN`    | FM3ch + SSG3ch |
| `FM_CHIP_OPNA`    | YM2608 (OPNA)  | `FM::OPNA`   | FM6ch + SSG3ch + ADPCM-B + リズム(WAV) |
| `FM_CHIP_OPNB`    | YM2610 (OPNB)  | `FM::OPNB`   | FM4ch + SSG3ch + ADPCM-A/B |
| `FM_CHIP_OPNBB`   | YM2610B (OPNBB)| `FM::OPNBB`  | FM6ch + SSG3ch + ADPCM-A/B ★追加実装 |
| `FM_CHIP_OPN2`    | YM2612 (OPN2)  | `FM::OPN2`   | FM6ch + DACチャンネル ★追加実装 |
| `FM_CHIP_OPM`     | YM2151 (OPM)   | `FM::OPM`    | FM8ch |
| `FM_CHIP_EXT_SSG` | YM2149 (SSG)   | `::PSG`      | fmgen 同梱の PSG クラス |

上記以外は `FmEngine_AddChip` / `FmEngine_AddExtChip` が
`FM_ERR_INVALID_ARG` を返す。

## ファイル構成

```
FmGenEngine/
├── CMakeLists.txt
├── extern/
│   └── fmgen/              ← fmgen 0.08 (cisc) + FmGenEngine による追加実装
└── src/
    ├── FmGenChip.h         fmgenチップラッパー (OPN/OPNA/OPNB/OPNBB/OPN2/OPM)
    ├── FmGenExtChip.h      fmgen PSG (SSG) ラッパー
    ├── FmEngine.h          複数チップ管理・SPSCキュー・ゲイン
    ├── FmGenEngineApi.h    DLL公開 C ABI 宣言
    ├── FmGenEngineApi.cpp  DLL公開 C ABI 実装
    ├── FmGenEngineApi.def
    └── FmGenEngineApi.rc
```

## fmgen ソースについて

fmgen 0.08 は cisc 氏が制作した FM 音源エミュレータライブラリで、
`extern/fmgen/` に同梱している。配布元:

> http://retropc.net/cisc/m88/

### fmgen ソースへの変更点

`[FmGenEngine]` マーカー付きコメントで全変更箇所を明示している。

**1. 文字エンコーディング変換 (全ファイル)**
配布時点の Shift_JIS から UTF-8 へ変換。内容・字句は無変更。
詳細は `extern/fmgen/ENCODING_NOTE.md` を参照。

**2. `opna.h` — OPNB クラスのバグ修正 (1行削除)**
`OPNB` クラスが基底クラス `OPNABase` と同名の `Channel4 ch[6]` を
重複定義しており、通常使用時に確実にクラッシュするバグを修正。
重複宣言の 1 行削除で解消。

**3. `opna.h` — OPN2 クラス宣言の修正**
fmgen 0.08 の `OPN2` はヘッダ宣言のみで実装が皆無だった。
本プロジェクトでの完全実装にあたり 6ch + DAC チャンネル対応に修正。

**4. `opna_ext.h` / `opna_ext.cpp` — OPN2 / OPNBB の追加実装 (新規)**

- **`FM::OPNBB` (YM2610B)**: `OPNB` の派生クラス。レジスタ `0x29` を
  正しく更新することで CH3〜5 の有効/無効制御を可能にする。
- **`FM::OPN2` (YM2612)**: FM 6ch + DAC チャンネル。ポート 0/1 で
  CH1〜3 / CH4〜6 を分離。DAC は `0x2B` bit7=1 で有効化。
  fmgen の `SetPrescaler` との整合のため `SetRate` 内で `clock/2` を
  渡す補正を行っている (YM2612 の内部 FM クロックは `masterClock/144`)。

## ライセンス

| コンポーネント | パス | ライセンス |
|---|---|---|
| FmGenEngine 独自コード | `src/` | **MIT** ([`LICENSE`](./LICENSE) 参照) |
| fmgen 本体 + 追加実装 | `extern/fmgen/` | cisc (1998, 2003) 独自ライセンス (MIT非互換) |

`src/` の MIT License は `FmGenEngineApi.h` の元になった YMEngine
(MIT License) から流用・改変した部分も含む。

fmgen のライセンス全文は `extern/fmgen/readme.txt` に同梱
(UTF-8 変換済み、内容は原文と同一)。要点:
- 由来 (作者・著作権) を明記すること
- 配布する際はフリーソフトとすること
- 改変内容を明示すること (→ 本プロジェクトでは `[FmGenEngine]` コメントで明示)
- `readme.txt` を改変せず添付すること
- 商用ソフトへの組み込みには作者の事前合意が必要

## セットアップ

```bash
git clone <このリポジトリ>
cd FmGenEngine
# extern/fmgen/ には fmgen008.lzh を展開済みのソースが同梱されている
```

## ビルド (Visual Studio 2022)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 成果物
#   build/bin/FmGenEngineApi.dll
#   build/bin/FmGenEngineApi.pdb
#   build/lib/FmGenEngineApi.lib
```

## DLL の使い方

### YMEngine との切り替え

| | YMEngine (ymfm) | FmGenEngine (fmgen) |
|---|---|---|
| DLL | `FmEngineApi.dll` | `FmGenEngineApi.dll` |
| インポートライブラリ | `FmEngineApi.lib` | `FmGenEngineApi.lib` |
| ヘッダ | `FmEngineApi.h` | `FmGenEngineApi.h` |
| 関数名・シグネチャ | 共通 | 共通 |

チップは文字列で指定する。対応チップ一覧は `FmEngine_Inquiry` /
`FmEngine_GetSupportedChip` で実行時に取得できる。

```c
#include "FmGenEngineApi.h"
#pragma comment(lib, "FmGenEngineApi.lib")

FmEngineHandle eng = FmEngine_Create(48000);

// 対応チップ一覧を取得
uint32_t n = FmEngine_Inquiry(eng);
for (uint32_t i = 0; i < n; ++i)
    printf("%s ", FmEngine_GetSupportedChip(eng, i));
// → OPN OPNA OPNB OPNBB OPN2 OPM SSG

// チップを文字列で追加
uint32_t opnaId;
FmEngine_AddChip(eng, "OPNA", 0, &opnaId);  // clock=0 で標準クロック
FmEngine_SetGain(eng, opnaId, 1.0f, 1.0f);

// レジスタ書き込み (任意スレッドから可)
FmEngine_Write(eng, opnaId, 0xB4, 0xC0, 0);   // port=0
FmEngine_Write(eng, opnaId, 0x28, 0xF0, 0);   // key on

// オーディオコールバック内で呼び出す
float out_l[512], out_r[512];
FmEngine_Generate(eng, out_l, out_r, 512);

FmEngine_Destroy(eng);
```

### ADPCM メモリ設定

```c
// OPNA: ADPCM-B RAM 初期値
// (FM_MEM_ADPCM_A は fmgen の OPNA では不使用。渡しても FM_OK を返して無視する)
FmEngine_SetMemory(eng, opnaId, FM_MEM_ADPCM_B, adpcmbData, adpcmbSize);

// OPNB / OPNBB: ADPCM-A / ADPCM-B ROM
FmEngine_SetMemory(eng, opnbId, FM_MEM_ADPCM_A, adpcmaRom, adpcmaSize);
FmEngine_SetMemory(eng, opnbId, FM_MEM_ADPCM_B, adpcmbRom, adpcmbSize);
// SetMemory はストリーム開始前 (AddChip 直後) に呼ぶこと
```

> **FMEngineTest との互換性**: FMEngineTest が `"OPNA"` に対して
> `FM_MEM_ADPCM_A` (`ym2608.rom`) を渡す場合、fmgen の OPNA はリズム音源を
> WAV ファイルから読み込む設計のため、このデータは使用されない。
> `FM_OK` を返して無視する設計になっており、他チャンネルの動作に影響しない。

## クロック

| チップ | 標準クロック (`clock=0` 指定時) |
|---|---|
| OPN   | 3,993,600 Hz |
| OPNA  | 7,987,200 Hz |
| OPNB  | 8,000,000 Hz |
| OPNBB | 8,000,000 Hz |
| OPN2  | 7,670,453 Hz |
| OPM   | 3,579,545 Hz |
| SSG   | 3,579,545 Hz |
