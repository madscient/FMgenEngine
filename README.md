# FmGenEngine

**fmgen** ([cisc](mailto:cisc@retropc.net), 1998-2003) をコアとした、
[YMEngine](https://github.com/madscient/YMEngine) (ymfm版) と
**API (ABI) 互換**の Windows 向け FM 音源エンジン。

ライセンス: FmGenEngine 独自コード (`src/`) は
**MIT License** ([`LICENSE`](./LICENSE))。同梱の fmgen 本体
(`extern/fmgen/`) は別ライセンス。詳細は「ライセンス」節を参照。

`FmGenEngineApi.h` を一切変更せずに DLL の中身だけを ymfm → fmgen に
差し替えたもの。YMEngine 向けに書かれたアプリケーションは、リンクする
DLL を本プロジェクトのもの ( `FmGenEngineApi.dll` ) に差し替えるだけで
動作する (対応チップの範囲内であれば再コンパイルも不要)。

## なぜ fmgen 版が必要か

ymfm (YMEngine 本家) と fmgen は同じ YM2608/YM2151 系チップでも
内部実装・再現アルゴリズムが異なり、出力される波形の質感が異なる。
fmgen は PC-8801/PC-9801 系レトロゲームのサウンドドライバや
エミュレータ (M88 等) で長年使われてきた実装で、その音色を
YMEngine と同じ C ABI で利用したいというニーズに応える。

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

上記以外 (`FM_CHIP_Y8950` / `OPL` 系 / `OPLL` 系 / `OPZ` / `VRC7` /
`FM_CHIP_EXT_DCSG` / `SCC` / `SAA`) は `FmEngine_AddChip` /
`FmEngine_AddExtChip` が `FM_ERR_INVALID_ARG` を返す。

## ファイル構成

```
FmGenEngine/
├── CMakeLists.txt
├── extern/
│   ├── fmgen/              ← fmgen 0.08 (cisc) + FmGenEngine による追加実装
│   └── nlohmann_json/      ← single_include 同梱 (git submodule でも可)
├── src/
│   ├── FmGenChip.h         fmgenチップラッパー (OPN/OPNA/OPNB/OPNBB/OPN2/OPM)
│   ├── FmGenExtChip.h      fmgen PSG (SSG) ラッパー
│   ├── FmEngine.h          複数チップ管理・SPSCキュー・ゲイン (YMEngine同名クラス)
│   ├── WasapiOutput.h      WASAPI出力 (YMEngine からそのまま流用・無改造)
│   ├── FmGenEngineApi.h    DLL公開 C ABI 宣言 (YMEngine からそのまま流用・無改造)
│   ├── FmGenEngineApi.cpp  DLL公開 C ABI 実装 (fmgen バックエンド向けに実装)
│   ├── FmGenEngineApi.def
│   ├── FmGenEngineApi.rc
│   └── main.cpp            sample_app (YMEngine版を流用、対応7チップ向けに調整)
├── patches/                sample_app 用 JSON パッチ (対応7チップ分)
└── README.md (本ファイル)
```

## fmgen ソースについて

fmgen 0.08 は cisc 氏が制作した FM 音源エミュレータライブラリで、
`extern/fmgen/` に同梱している。配布元は以下の URL を参照。

> http://retropc.net/cisc/m88/

### fmgen ソースへの変更点

本プロジェクトでは fmgen 0.08 のオリジナルソースに対して以下の変更を加えている。
変更箇所にはすべて `[FmGenEngine]` マーカー付きコメントを付与している。

**1. 文字エンコーディング変換 (全ファイル)**

配布時点の Shift_JIS (CP932) から UTF-8 へ変換した。
バイトエンコーディングの変換のみで、テキストとしての内容・字句は一切変更していない。
詳細は `extern/fmgen/ENCODING_NOTE.md` を参照。

**2. `opna.h` — OPNB クラスのバグ修正 (1行削除)**

`OPNB` (YM2610) クラスが基底クラス `OPNABase` と同名・同型の
`private Channel4 ch[6];` を重複定義しており、これが基底クラスの
`protected Channel4 ch[6]` メンバを名前隠蔽 (shadowing) してしまうバグがあった。

`OPNB::OPNB()` コンストラクタ内の `csmch = &ch[2];` はこの隠蔽により
意図せず `OPNB::ch[2]` (未初期化の重複配列) を指してしまい、
`OPNABase::FMMix()` 内の `fnum[csmch-ch]` が異なる配列間の差分計算となって
未定義動作を起こし、CSM モードを使わない通常使用時に OPNB が確実にクラッシュする。

`OPNB` の実処理はすべて `OPNABase::ch` のみを参照していて `OPNB::ch` は
コンストラクタの `csmch` 初期化以外で使われていなかったため、
重複宣言 `Channel4 ch[6];` の 1 行削除で安全に解消できる。

**3. `opna.h` — OPN2 クラス宣言の修正 (追加実装のため)**

fmgen 0.08 の `OPN2` (YM2612) クラスはヘッダ宣言のみ存在し実装が皆無だった。
本プロジェクトで完全実装するにあたり、オリジナルの 3ch+補間ワーク程度のスケルトン
宣言を 6ch + DAC チャンネル対応に修正した。

**4. `opna_ext.h` / `opna_ext.cpp` — OPN2 / OPNBB の追加実装 (新規ファイル)**

fmgen 0.08 に存在しない以下のクラスを新たに実装した。

- **`FM::OPNBB` (YM2610B)**: `OPNB` の派生クラス。`OPNB` との唯一の差分は
  レジスタ `0x29` の扱いで、`OPNB` はこのレジスタへの書き込みを無視するが、
  `OPNBB` は `reg29` を正しく更新することで CH3〜5 の有効/無効制御を可能にする。

- **`FM::OPN2` (YM2612)**: `OPNBase` を継承した FM 6ch + DAC チャンネルの実装。
  SSG / ADPCM なし。ポート 0 (0x000-0x0FF) が CH1〜3、ポート 1 (0x100-0x1FF) が
  CH4〜6 に対応する。DAC チャンネルはレジスタ `0x2B` bit7=1 で有効化し、
  `0x2A` に書き込んだ 8bit PCM 値を CH6 の代わりに出力する。
  YM2612 の内部 FM クロックは `masterClock/144` であるため、fmgen の
  `SetPrescaler` (内部で `clock/72` を計算) との整合を取るため
  `SetRate` 内で `clock/2` を渡す補正を行っている。

## ライセンス

本プロジェクトは複数のライセンスが混在するコンポーネント構成になっている。
**FmGenEngine 独自のラッパー・エンジンコード (`src/`) は MIT License** とする
(YMEngine と同じライセンス)。全文はリポジトリルートの [`LICENSE`](./LICENSE) を参照。

| コンポーネント | パス | ライセンス |
|---|---|---|
| FmGenEngine ラッパー・エンジンコード | `src/`, `CMakeLists.txt` | **MIT** ([`LICENSE`](./LICENSE) 参照) |
| fmgen 本体 + 追加実装 | `extern/fmgen/` | cisc (1998, 2003) オリジナルライセンス (MIT非互換、下記参照) |
| nlohmann/json | `extern/nlohmann_json/` | MIT (nlohmann) |

- **FmGenEngine 独自コード (`src/`)**: MIT License。
  `src/FmGenEngineApi.h` / `src/WasapiOutput.h` / `src/main.cpp` は
  YMEngine 本家 (MIT License) から流用・改変したものだが、
  YMEngine 自体が MIT License のため引き続き MIT として扱う。
  各ファイル冒頭に `SPDX-License-Identifier: MIT` を明記している。
- **fmgen**: cisc (1998, 2003) によるオリジナルライセンス。
  **MIT ではない**ため取り扱いに注意。
  `extern/fmgen/readme.txt` に全文を同梱 (UTF-8 変換済み、内容は原文と同一)。要約:
  - 由来 (作者・著作権) を明記すること
  - 配布する際はフリーソフトとすること
  - 改変したソースコードを配布する際は改変内容を明示すること
    (→ 本プロジェクトでは `[FmGenEngine]` コメントおよび本節で明示している)
  - ソースコードを配布する際はこの readme.txt を一切改変せずに添付すること
  - 商用ソフトへの組み込みには作者の事前合意が必要
- **nlohmann/json**: MIT (nlohmann)

## セットアップ

```bash
git clone <このリポジトリ>
cd FmGenEngine
# fmgen008.lzh を入手し、中身一式を extern/fmgen/ に展開済みであること
# (本リポジトリには展開済みのものを同梱)

# nlohmann/json を submodule で取得する場合
git submodule add https://github.com/nlohmann/json extern/nlohmann_json
# (本リポジトリには single_include を直接同梱済み)
```

## ビルド (Visual Studio 2022)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 成果物
#   build/bin/FmGenEngineApi.dll
#   build/bin/FmGenEngineApi.pdb
#   build/lib/FmGenEngineApi.lib
#   build/bin/sample_app.exe
#   build/bin/patches/
```

## DLL の使い方

### YMEngine との切り替え

| | YMEngine (ymfm) | FmGenEngine (fmgen) |
|---|---|---|
| DLL | `FmEngineApi.dll` | `FmGenEngineApi.dll` |
| インポートライブラリ | `FmEngineApi.lib` | `FmGenEngineApi.lib` |
| ヘッダ | `FmEngineApi.h` | `FmGenEngineApi.h` |
| 関数名・型名 | 共通 | 共通 |

関数名・型名・列挙値は両 DLL で完全に一致している。アプリケーション側で
切り替えるには、インクルードするヘッダとリンクするライブラリ名を変えるだけでよい。
実行時に切り替える場合は `LoadLibrary` / `GetProcAddress` 経由でどちらかを
動的ロードする方式が使える (両 DLL のエクスポート関数シグネチャが同一のため)。

```c
// ymfm バックエンド版 (YMEngine)
// #include "FmEngineApi.h"
// #pragma comment(lib, "FmEngineApi.lib")

// fmgen バックエンド版 (FmGenEngine) — ヘッダ名とライブラリ名だけ異なる
#include "FmGenEngineApi.h"
#pragma comment(lib, "FmGenEngineApi.lib")
```

`FmGenEngineApi.h` の関数シグネチャは YMEngine の `FmEngineApi.h` と完全に同一。

```c
#include "FmGenEngineApi.h"
#pragma comment(lib, "FmGenEngineApi.lib")

FmEngineHandle eng = FmEngine_Create(48000);

uint32_t opnaId;
FmEngine_AddChip(eng, FM_CHIP_OPNA, 0, &opnaId);   // clock=0 で標準クロック(7.9872MHz)

uint32_t ssgId;
FmEngine_AddExtChip(eng, FM_CHIP_EXT_SSG, 0, &ssgId);

FmEngine_SetGain(eng, opnaId, 1.0f, 1.0f);

WasapiHandle wasapi = Wasapi_Create(eng, 0);
Wasapi_Start(wasapi);

FmEngine_Write(eng, opnaId, 0xB4, 0xC0, 0);   // port=0
FmEngine_Write(eng, opnaId, 0x10, 0x0F, 1);   // port=1 (OPNA/OPNB 拡張レジスタ面 = addr+0x100)

Wasapi_Stop(wasapi);
Wasapi_Destroy(wasapi);
FmEngine_Destroy(eng);
```

### [fmgen 固有拡張] OPNA リズムサンプル読み込み

YMEngine (ymfm版) には存在しない、fmgen バックエンド固有の追加 API。
**既存関数の挙動・シグネチャは一切変更していないため、ABI 互換性は
維持される** (純粋な追加関数)。

fmgen の `OPNA` はリズム音源を実機の ADPCM-A ではなく WAV サンプル
(`2608_BD.WAV` / `2608_SD.WAV` / `2608_TOP.WAV` / `2608_HH.WAV` /
`2608_TOM.WAV` / `2608_RIM.WAV`、またはまとめて `2608_RYM.WAV`)
から読み込む設計になっている。

```c
uint32_t opnaId;
FmEngine_AddChip(eng, FM_CHIP_OPNA, 0, &opnaId);

// dir_path には '\' か '/' で終わるディレクトリパスを渡す
FmEngine_LoadRhythmSamples(eng, opnaId, "C:\\path\\to\\samples\\");
// Wasapi_Start より前、AddChip 直後に呼ぶこと (スレッドセーフではない)
// ファイルが見つからなくても FM_OK を返す (リズムchが無音になるだけ)
```

`sample_app.exe` は実行ファイルと同じフォルダから自動的にこれらの
WAV ファイルを探してロードする。

### OPNA / OPNB / OPNBB の ADPCM メモリ設定

`FmEngine_SetMemory` は YMEngine と同じシグネチャだが、内部動作が
異なる点に注意。

- **OPNA**: `FM_MEM_ADPCM_B` のみ対応。OPNA 内部の 256KB ADPCM-B RAM
  バッファへ `memcpy` する (`FM_MEM_ADPCM_A` は OPNA では未使用。
  リズムは `FmEngine_LoadRhythmSamples` 経由の WAV で代替するため)。
- **OPNB / OPNBB**: `FM_MEM_ADPCM_A` / `FM_MEM_ADPCM_B` の両方に対応。
  fmgen の `OPNB::Init` がポインタを直接保持する設計のため、
  `SetMemory` 呼び出し時に内部で `Init` を再実行する。
  **これによりレジスタ状態が `Reset()` される**ため、
  `FmEngine_SetMemory` は必ず `Wasapi_Start` より前
  (`AddChip` 直後) に呼ぶこと。

## 出力フォーマットについて

fmgen の `Mix()` はチップ内部で FM・SSG・ADPCM・リズムを
**すべて合成し終えたステレオ PCM** を返す設計
(ymfm 版のように「チャンネル別の生データを取得してエンジン側で
ミックスする」方式ではない)。本ラッパーは fmgen の `Mix()` 出力を
`int32` ワークバッファ (呼び出しごとにゼロクリア。fmgen の `Mix()` は
**加算合成**のため) に受け、`±32768` を `±1.0` として `float` に
変換している。これは ymfm 版 `FmChip.h` と同じスケール規約であり、
`FmEngine::generate()` 内のソフトクリップ処理もそのまま共用できる。

fmgen は ymfm 版と異なり、`Init`/`SetRate` に直接ターゲットサンプル
レートを渡せる設計のため、ymfm 版の `LinearResampler` のような
外付けリサンプラーは使用していない (fmgen 内部でレート変換まで完結する)。

## クロック

| チップ | 標準クロック |
|---|---|
| OPN   | 3,993,600 Hz |
| OPNA  | 7,987,200 Hz |
| OPNB  | 8,000,000 Hz |
| OPNBB | 8,000,000 Hz |
| OPN2  | 7,670,453 Hz |
| OPM   | 3,579,545 Hz |
| SSG   | 3,579,545 Hz |

`FmEngine_AddChip` / `FmEngine_AddExtChip` に `clock=0` を渡すと
上記の標準クロックが自動選択される。
