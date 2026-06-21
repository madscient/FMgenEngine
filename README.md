# FmGenEngine

**fmgen** ([cisc](mailto:cisc@retropc.net), 1998-2003) をコアとした、
[YMEngine](https://github.com/madscient/YMEngine) (ymfm版) と
**API (ABI) 互換**の Windows 向け FM 音源エンジン。

`FmEngineApi.h` を一切変更せずに DLL の中身だけを ymfm → fmgen に
差し替えたもの。YMEngine 向けに書かれたアプリケーションは、リンクする
DLL を本プロジェクトのもの ( `FmEngineApi.dll` ) に差し替えるだけで
動作する (対応チップの範囲内であれば再コンパイルも不要)。

## なぜ fmgen 版が必要か

ymfm (YMEngine 本家) と fmgen は同じ YM2608/YM2151 系チップでも
内部実装・再現アルゴリズムが異なり、出力される波形の質感が異なる。
fmgen は PC-8801/PC-9801 系レトロゲームのサウンドドライバや
エミュレータ (M88 等) で長年使われてきた実装で、その音色を
YMEngine と同じ C ABI で利用したいというニーズに応える。

## 対応チップ

fmgen 0.08 が実装しているチップのみに対応する
(fmgen 0.08 は `OPN2` (YM2612) をヘッダ宣言のみで実装を含まないため、
本プロジェクトでも `OPN2` は非対応)。

| `FmChipType` / `FmChipTypeExt` | チップ | fmgen クラス | 備考 |
|---|---|---|---|
| `FM_CHIP_OPN`  | YM2203 (OPN)  | `FM::OPN`  | FM3ch + SSG3ch |
| `FM_CHIP_OPNA` | YM2608 (OPNA) | `FM::OPNA` | FM6ch + SSG3ch + ADPCM-B + リズム(WAV) |
| `FM_CHIP_OPNB` | YM2610 (OPNB) | `FM::OPNB` | FM6ch(実質4ch、CH0/3はADPCM専用) + SSG3ch + ADPCM-A/B |
| `FM_CHIP_OPM`  | YM2151 (OPM)  | `FM::OPM`  | FM8ch |
| `FM_CHIP_EXT_SSG` | YM2149 (SSG) | `::PSG`  | fmgen 同梱の PSG クラス |

上記以外 (`FM_CHIP_Y8950` / `OPL` / `OPL2` / `OPL3` / `OPL4` / `OPNBB` /
`OPN2` / `OPLL` 系 / `OPZ` / `VRC7` / `FM_CHIP_EXT_DCSG` / `SCC` / `SAA`)
は `FmEngine_AddChip` / `FmEngine_AddExtChip` が `FM_ERR_INVALID_ARG`
を返す。

## ファイル構成

```
FmGenEngine/
├── CMakeLists.txt
├── extern/
│   ├── fmgen/              ← fmgen 0.08 (cisc) を無改造で配置 (1ファイルのみ差分あり、後述)
│   └── nlohmann_json/      ← single_include 同梱 (git submodule でも可)
├── src/
│   ├── FmGenChip.h         fmgen (OPN/OPNA/OPNB/OPM) ラッパー
│   ├── FmGenExtChip.h      fmgen PSG (SSG) ラッパー
│   ├── FmEngine.h          複数チップ管理・SPSCキュー・ゲイン (YMEngine同名クラス)
│   ├── WasapiOutput.h      WASAPI出力 (YMEngine からそのまま流用・無改造)
│   ├── FmEngineApi.h       DLL公開 C ABI 宣言 (YMEngine からそのまま流用・無改造)
│   ├── FmEngineApi.cpp     DLL公開 C ABI 実装 (fmgen バックエンド向けに実装)
│   ├── FmEngineApi.def
│   ├── FmEngineApi.rc
│   └── main.cpp            sample_app (YMEngine版を流用、対応5チップ向けに調整)
├── patches/                 sample_app 用 JSON パッチ (対応5チップ分のみ)
├── test_native/             Windows実機なしで fmgen DSP ロジックを検証するスモークテスト
└── README.md (本ファイル)
```

## fmgen ソースについて

`extern/fmgen/` には fmgen 0.08 のソース一式をそのまま配置する。
**1 箇所だけソース上のバグを修正している** (詳細は次節)。
それ以外のロジックは無改造。

### fmgen ソースへの変更点 (`extern/fmgen/opna.h`)

`OPNB` (YM2610) クラスが、基底クラス `OPNABase` と同名・同型の
`private Channel4 ch[6];` を重複定義しており、これが基底クラスの
`protected Channel4 ch[6]` メンバを**名前隠蔽 (shadowing)** してしまう
バグがあった。

`OPNB::OPNB()` コンストラクタ内の `csmch = &ch[2];` はこの隠蔽により
意図せず `OPNB::ch[2]` (未初期化の重複配列) を指してしまい、
`OPNABase::FMMix()` 内の `fnum[csmch-ch]` という式
(`OPNABase::ch` に対するポインタ差分のつもりが、異なる配列間の
差分計算になってしまう) が未定義動作を起こし、CSM モードを使わない
通常使用時 (`!(regtc & 0xc0)` が真の場合、つまりほぼ常に) に
**OPNB が確実にクラッシュする**ことを確認した。

`OPNB` の実処理 (`Mix`/`Mix6`/`MixSubS` 等) はすべて `OPNABase` の
メンバ関数であり、もともと `OPNABase::ch` のみを参照していて
`OPNB::ch` はコンストラクタの `csmch` 初期化以外どこからも
使われていなかった (デッドコード)。そのため `extern/fmgen/opna.h`
内の重複宣言 `Channel4 ch[6];` (`OPNB` クラス内) を削除する 1 行のみの
修正で安全に解消できる。修正箇所には改変理由を示すコメントを
付している (`opna.h` 内 `[FmGenEngine]` で検索可能)。

このバグ修正は `test_native/` のネイティブ検証テストで
再現・修正確認を行っている。

## ライセンス

- **fmgen**: cisc (1998, 2003) によるオリジナルライセンス。
  `extern/fmgen/readme.txt` に全文を同梱。要約:
  - 由来 (作者・著作権) を明記すること
  - 配布する際はフリーソフトとすること
  - 改変したソースコードを配布する際は改変内容を明示すること
    (→ 本プロジェクトでは `opna.h` 内のコメントで明示している)
  - ソースコードを配布する際はこの readme.txt を一切改変せずに添付すること
  - 商用ソフトへの組み込みには作者の事前合意が必要
- **nlohmann/json**: MIT (nlohmann)
- **このラッパー・エンジンコード (src/ 以下)**: MIT
  (YMEngine 本家の `FmEngineApi.h` / `WasapiOutput.h` は同じく MIT の
  YMEngine プロジェクトから無改造で流用している)

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
#   build/bin/FmEngineApi.dll
#   build/bin/FmEngineApi.pdb
#   build/lib/FmEngineApi.lib
#   build/bin/sample_app.exe
#   build/bin/patches/
```

## ビルド (MinGW-w64 クロスコンパイル / 検証用)

開発・CI 環境に Windows がなくても、Linux 上で MinGW-w64 を使って
本物の `.dll`/`.exe` (PE32+ バイナリ) を生成できることを確認済み。

```bash
cmake -B build-mingw -G "Unix Makefiles" \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
cmake --build build-mingw
```

(`CMakeLists.txt` 内、`if (MINGW)` ブロックで `INITGUID` 定義と
`ksguid` リンクを追加している。これは MinGW 環境固有の
PROPERTYKEY/GUID 定数の解決のためで、MSVC ビルドには無関係・無影響)

## fmgen DSP ロジックの動作検証 (Windows 実機不要)

`test_native/` に、WASAPI を介さず fmgen コア DSP ロジック
(レジスタ書き込み → サンプル生成) のみを Linux ネイティブで
検証するスモークテストを用意している。

```bash
sh test_native/build_and_run.sh
```

`windows.h` は `test_native/winstub/windows.h` という最小スタブ
(`MAX_PATH` / `__stdcall` の空展開 / `HANDLE` の代替 typedef のみ)
に差し替え、`FileIO` はファイル I/O を行わないダミー実装
(`test_native/file_stub.cpp`) で代替している。
これにより MinGW すら不要で、通常の Linux 用 g++ だけで
OPN/OPNA/OPNB/OPM/SSG 各チップが実際に非無音の音声サンプルを
生成すること、`FmEngine` (複数チップ管理・ゲイン・SPSCキュー・
ソフトクリップ) が正しく動作すること、`SetMemory` (ADPCM) 経路や
ポート1 (`addr+0x100`) レジスタ書き込みが正しく機能することを
検証できる。

## DLL の使い方

`FmEngineApi.h` は YMEngine と完全に同一なので、使い方も同一。

```c
#include "FmEngineApi.h"
#pragma comment(lib, "FmEngineApi.lib")

FmEngineHandle eng = FmEngine_Create(48000);

uint32_t opnaId;
FmEngine_AddChip(eng, FM_CHIP_OPNA, 0, &opnaId);   // clock=0 で標準クロック(7.9872MHz)

uint32_t ssgId;
FmEngine_AddExtChip(eng, FM_CHIP_EXT_SSG, 0, &ssgId);

FmEngine_SetGain(eng, opnaId, 1.0f, 1.0f);

WasapiHandle wasapi = Wasapi_Create(eng, 0);
Wasapi_Start(wasapi);

FmEngine_Write(eng, opnaId, 0xB4, 0xC0, 0);   // port=0
FmEngine_Write(eng, opnaId, 0x10, 0x0F, 1);   // port=1 (OPNA/OPNB の拡張レジスタ面 = addr+0x100)

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

### OPNA / OPNB の ADPCM メモリ設定

`FmEngine_SetMemory` は YMEngine と同じシグネチャだが、内部動作が
異なる点に注意。

- **OPNA**: `FM_MEM_ADPCM_B` のみ対応。OPNA 内部の 256KB ADPCM-B RAM
  バッファへ `memcpy` する (`FM_MEM_ADPCM_A` は OPNA では未使用。
  リズムは `FmEngine_LoadRhythmSamples` 経由の WAV で代替するため)。
- **OPNB**: `FM_MEM_ADPCM_A` / `FM_MEM_ADPCM_B` の両方に対応。
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
| OPN  | 3,993,600 Hz |
| OPNA | 7,987,200 Hz |
| OPNB | 8,000,000 Hz |
| OPM  | 3,579,545 Hz |
| SSG  | 3,579,545 Hz |

`FmEngine_AddChip` / `FmEngine_AddExtChip` に `clock=0` を渡すと
上記の標準クロックが自動選択される。

## 既知の制約

- OPNB (YM2610) は実機仕様上 CH0 / CH3 が FM チャンネルとして
  使用できない (ADPCM 専用)。`patches/opnb.json` は CH1/2/4/5 を使う。
- fmgen 0.08 は YM2610B (OPNBB) 相当の差分実装を持たないため非対応。
- WASAPI / fmgen ともに Windows 専用 API に依存するため、
  本番ビルドは Windows (MSVC または MinGW) 限定。
  (`test_native/` のスモークテストのみ Linux ネイティブで実行可能)
