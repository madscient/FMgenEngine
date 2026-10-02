# FmGenEngine

**fmgen** ([cisc](mailto:cisc@retropc.net), 1998-2003) をコアとした、
[YMEngine](https://github.com/madscient/YMEngine) (ymfm版) と
**API (ABI) 互換**の Windows 向け FM 音源エンジン DLL。

ライセンス: FmGenEngine 独自コード (`src/`) は
**MIT License** ([`LICENSE`](./LICENSE))。同梱の fmgen 本体
(`extern/fmgen/`) は別ライセンス。詳細は「ライセンス」節を参照。

## 概要

YMEngine の `FmEngineApi.h` と同じ関数・列挙値を持つヘッダ (`FmGenEngine.h`) を
そのまま使い、DLL の中身だけを ymfm → fmgen に差し替えたもの。YMEngine 向けに
書かれたアプリケーションは、リンクする DLL を本プロジェクトのもの
( `FmGenEngine.dll` ) に差し替えるだけで動作する (対応チップの範囲内であれば
再コンパイルも不要)。

波形生成 (`FmEngine_Generate`) とオーディオ出力は分離されており、
DLL 側には特定のオーディオ API (WASAPI 等) への依存がない。
アプリケーション側のオーディオコールバックから `FmEngine_Generate` を
呼び出すことで任意のオーディオバックエンドと組み合わせられる。

## 対応チップ

| チップ名 (`FmEngine_AddChip` の `name`) | チップ | fmgen クラス | 備考 |
|---|---|---|---|
| `"OPN"`   | YM2203 (OPN)   | `FM::OPN`    | FM3ch + SSG3ch |
| `"OPNA"`  | YM2608 (OPNA)  | `FM::OPNA`   | FM6ch + SSG3ch + ADPCM-B + リズム(WAV) |
| `"OPNB"`  | YM2610 (OPNB)  | `FM::OPNB`   | FM4ch + SSG3ch + ADPCM-A/B |
| `"OPNBB"` | YM2610B (OPNBB)| `FM::OPNBB`  | FM6ch + SSG3ch + ADPCM-A/B ★追加実装 |
| `"OPN2"`  | YM2612 (OPN2)  | `FM::OPN2`   | FM6ch + DACチャンネル ★追加実装 |
| `"OPM"`   | YM2151 (OPM)   | `FM::OPM`    | FM8ch |
| `"SSG"`   | YM2149 (SSG)   | `::PSG`      | fmgen 同梱の PSG クラス |

上記以外の名前を渡すと `FmEngine_AddChip` は `FM_ERR_UNKNOWN_CHIP` を返す。

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
    ├── FmGenEngine.h       DLL公開 C ABI 宣言
    ├── FmGenEngine.cpp     DLL公開 C ABI 実装
    ├── FmGenEngine.def
    └── FmGenEngine.rc
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
  YM2612 のプリスケーラは 1/6 固定なので、`0x2D`〜`0x2F` の書き込みは無視する。

**5. `opna.h` / `opna.cpp` — FM と SSG を別々に出力する `MixSplit` の追加**
`OPN` / `OPNA` / `OPNB` に、FM 側 (ADPCM・リズムを含む) と SSG を別々の
バッファに加算する `MixSplit(fm, ssg, nsamples)` を追加した。元の `Mix` の
本体を `MixSplit` に移し、`Mix` は同じバッファを 2 つ渡して `MixSplit` を
呼ぶ形にしたので、`Mix` の出力は変わらない。部位ごとのゲイン
(`FmEngine_SetPartGain`) の実現に使う。

**6. `opna.h` / `opna.cpp` — ADPCM のメモリアクセスの差し替え口と、ROM モードの番地**
`OPNABase` に、メモリの読み書きを差し替える口 (`ADPCMMemory`、`SetADPCMMemory`) を
追加し、ADPCM-A/B のメモリの読み書きをすべてここに通すようにした。設定しなければ、
元どおり内部のバッファを読み書きする。ADPCM-B は ROM/RAM 選択ビットに応じて
ROM モードと RAM モードの別々の空間として呼ぶ。外部メモリの割り当て
(`FmEngine_SetMemoryEx`) の実現に使う。
あわせて、ADPCM-B の ROM モード (control2 の bit0) は、x1/x8 の選択 (bit1) によらず
32 バイト単位で番地順に読み書きするようにした。元の fmgen は ROM モードでも bit1 に
従って x1/x8 の読み方をしていた。

## ライセンス

| コンポーネント | パス | ライセンス |
|---|---|---|
| FmGenEngine 独自コード | `src/` | **MIT** ([`LICENSE`](./LICENSE) 参照) |
| fmgen 本体 + 追加実装 | `extern/fmgen/` | cisc (1998, 2003) 独自ライセンス (MIT非互換) |

`src/` の MIT License は `FmGenEngine.h` の元になった YMEngine
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
#   build/bin/Release/FmGenEngine.dll
#   build/lib/Release/FmGenEngine.lib   (インポートライブラリ)
```

## DLL の使い方

### YMEngine との切り替え

| | YMEngine (ymfm) | FmGenEngine (fmgen) |
|---|---|---|
| DLL | `YMFMEngine.dll` | `FmGenEngine.dll` |
| インポートライブラリ | `YMFMEngine.lib` | `FmGenEngine.lib` |
| ヘッダ | `FmEngineApi.h` | `FmGenEngine.h` |
| 関数名・シグネチャ・列挙値 | 共通 | 共通 |

チップは文字列で指定する。対応チップ一覧は `FmEngine_Inquiry` /
`FmEngine_GetSupportedChip` で実行時に取得できる。

```c
#include "FmGenEngine.h"
#pragma comment(lib, "FmGenEngine.lib")

FmEngineHandle eng = FmEngine_Create(48000);

// 対応チップ一覧を取得
uint32_t n = FmEngine_Inquiry(eng);
for (uint32_t i = 0; i < n; ++i)
    printf("%s ", FmEngine_GetSupportedChip(eng, i));
// → OPN OPNA OPNB OPNBB OPN2 OPM SSG

// チップを文字列で追加
uint32_t opnaId;
FmEngine_AddChip(eng, "OPNA", 7987200, &opnaId);  // マスタークロック (Hz)。必ず指定する
FmEngine_SetGain(eng, opnaId, 1.0f, 1.0f);

// レジスタ書き込み (任意スレッドから可)
FmEngine_Write(eng, opnaId, 0xB4, 0xC0, 0);   // port=0
FmEngine_Write(eng, opnaId, 0x28, 0xF0, 0);   // key on

// オーディオコールバック内で呼び出す
float out_l[512], out_r[512];
FmEngine_Generate(eng, out_l, out_r, 512);

FmEngine_Destroy(eng);
```

### OPNA リズム音源

fmgen の OPNA はリズム音源を WAV ファイル
(`2608_BD.WAV` / `2608_SD.WAV` / `2608_TOP.WAV` / `2608_HH.WAV` /
`2608_TOM.WAV` / `2608_RIM.WAV`、またはまとめて `2608_RYM.WAV`)
から読み込む設計になっている。

`FmEngine_AddChip(eng, "OPNA", ...)` を呼んだ時点で、
**`FmGenEngine.dll` と同じフォルダにある WAV ファイルを自動的にロード**する。
アプリ側での明示的な操作は不要。
WAV ファイルが存在しない場合はリズムチャンネルが無音になるだけで、
FM / SSG / ADPCM-B チャンネルの動作には影響しない。

### 外部メモリ (ADPCM)

ADPCM を持つチップのメモリには、アプリケーションが用意したブロックを割り当てる。
割り当てはオーディオストリームを始める前に行う (スレッドセーフではない)。

| `FmMemoryType` | チップ | 内容 |
|---|---|---|
| `FM_MEM_ADPCM_A`         | OPNA | リズム音の内蔵 ROM の内容。受け付けるが読まない (リズムは WAV ファイルから鳴らす。「OPNA リズム音源」を参照) |
| `FM_MEM_ADPCM_A`         | OPNB, OPNBB | ADPCM-A のメモリ |
| `FM_MEM_ADPCM_B`         | OPNA | ADPCM-B の ROM/RAM 選択ビットが RAM のときにアクセスするメモリ |
| `FM_MEM_ADPCM_B`         | OPNB, OPNBB | ADPCM-B のメモリ |
| `FM_MEM_ADPCM_B_ROMMODE` | OPNA | ADPCM-B の ROM/RAM 選択ビットが ROM のときにアクセスするメモリ |

OPNA は、ROM/RAM 選択ビット (port1 の `0x01` の bit0) で、ROM モードと RAM モードの
別々のメモリにアクセスする。ROM モードで鳴らすデータは `FM_MEM_ADPCM_B_ROMMODE` に
割り当てる。`FM_MEM_ADPCM_B` に割り当てたデータは RAM モードでだけ読まれる。

割り当ての無い番地を読むと 0 で、書き込みは捨てる。何もつながっていない状態から
始まるので、OPNA にレジスタ経由で ADPCM-B のデータを転送して鳴らすには、先に RAM を
割り当てておく (`FmEngine_SetMemoryEx` の `FM_ACCESS_RAM`、または
`FmEngine_SetMemory`)。

#### FmEngine_SetMemoryEx

```c
// OPNA: RAM モードのメモリに 256KB の RAM、ROM モードのメモリに ROM イメージ
static uint8_t ram[0x40000];
FmEngine_SetMemoryEx(eng, opnaId, FM_MEM_ADPCM_B, 0, ram, sizeof ram, FM_ACCESS_RAM);
FmEngine_SetMemoryEx(eng, opnaId, FM_MEM_ADPCM_B_ROMMODE, 0, rom, romSize, FM_ACCESS_ROM);
```

- `[base, base + size)` に `data` を割り当てる。番地 `base + i` のバイトが `data[i]`。
  範囲が重ならなければ、1つのメモリに複数のブロックを並べられる。
- ブロックは複製せずに参照する。割り当てを外すか `FmEngine_Destroy` が戻るまで
  解放しないこと。
- `FM_ACCESS_RAM` のブロックには、チップの書き込み (レジスタ経由の転送) をその場で
  書く。`FM_ACCESS_ROM` のブロックへの書き込みは捨てる。
- ROM/RAM 選択ビットが ROM の間にレジスタ経由で転送したデータは、
  `FM_MEM_ADPCM_B_ROMMODE` に書く。
- `data` に `NULL` を渡すと、`[base, base + size)` と重なるブロックをすべて外す
  (`access` は見ない)。
- 未知の `chip_id`、チップが持たない `mem_type`、`size` が 0、`base + size` が 2^32 を
  越える、既存のブロックと範囲が重なる、未知の `access` のときは `FM_ERR_INVALID_ARG`
  を返す。

#### FmEngine_SetMemory / FmEngine_GetMemorySize

```c
FmEngine_SetMemory(eng, opnbId, FM_MEM_ADPCM_A, adpcmaRom, adpcmaSize);
FmEngine_SetMemory(eng, opnbId, FM_MEM_ADPCM_B, adpcmbRom, adpcmbSize);
```

`FmEngine_SetMemory` は、`mem_type` のメモリを `[0, size)` の `data` だけにする
(それまでのブロックは外れる)。DLL は `data` に書き込まない。

- OPNA の `FM_MEM_ADPCM_B` は DLL の内部に写す。チップの書き込みは写しに入る。
- それ以外は `data` を参照する。割り当てを外すか `FmEngine_Destroy` が戻るまで
  解放しないこと。
- `FM_MEM_ADPCM_B_ROMMODE`、範囲外の種別、`data` が `NULL`、`size` が 0、未知の
  `chip_id` を渡すと `FM_ERR_INVALID_ARG` を返す。チップが持たない種別は `FM_OK` を
  返して無視する。

`FmEngine_GetMemorySize` は、割り当てたブロックの大きさの合計を返す。

#### 書き込みが反映される時点

`FmEngine_Write` の書き込みは、次の `FmEngine_Generate` の中でチップに反映される。
チップがレジスタ経由の転送でメモリに書いた値は、その `FmEngine_Generate` が戻った
時点で `FM_ACCESS_RAM` のブロックに入っている。DLL がブロックを読み書きするのは
`FmEngine_Generate` の実行中だけ。

#### 番地とバイトの並び

1番地が1バイト。チップの番地レジスタ (開始・終了番地など) の単位と、ブロックの
バイトの並びは次のとおり。

| チップ・モード | 番地レジスタの単位 | バイトの並び |
|---|---|---|
| OPNA の RAM モード x1 (port1 `0x01` の bit1=0) | 4 バイト | 番地順 |
| OPNA の RAM モード x8 (bit1=1) | 32 バイト | 32KB の面 8 つにビットを振り分ける |
| OPNA の ROM モード (bit0=1) | 32 バイト (bit1 によらない) | 番地順 |
| OPNB, OPNBB の ADPCM-A / ADPCM-B | 256 バイト | 番地順 |

x8 の振り分け: チップから見て L 番目のバイトのビット p は、メモリの
`p × 0x8000 + (L >> 3)` 番地のバイトのビット `L & 7` に置かれる。

OPNA の番地は 256KB (`0x00000`〜`0x3FFFF`) の範囲で折り返す。それより後ろに割り当てた
ブロックは読まれない。

### 部位ごとのゲイン

OPN / OPNA / OPNB / OPNBB は、FM 部と SSG 部を足し合わせて出力する。実機では
FM と SSG を別々の端子から出してボード上の回路でミックスするため、音量バランスは
機種によって異なる。`FmEngine_SetPartGain` で部位ごとにゲインを設定できる。

```c
FmEngine_SetPartGain(eng, opnaId, FM_PART_OPN_SSG, 0.5f, 0.5f);  // SSG を -6 dB
```

| 部位 | 対象チップ | 内容 | 既定値 |
|---|---|---|---|
| `FM_PART_OPN_FM`  | OPN, OPNA, OPNB, OPNBB | FM 部 (ADPCM・リズムを含む) | 1.0 |
| `FM_PART_OPN_SSG` | OPN, OPNA, OPNB, OPNBB | SSG 部 | 1.0 |

実際に掛かるゲインは、`FmEngine_SetGain` で設定したチップ全体のゲインと部位の
ゲインの積。既定値 (1.0) では、FM 部と SSG 部を fmgen 本来のバランスで足し合わせる。

OPN2・OPM・SSG は部位を持たないので、`FmEngine_SetGain` を使う。`FmPart` の
その他の値 (`FM_PART_OPLL_MELODY` など) は YMEngine のチップ用で、本 DLL の
チップには無い。チップが持たない部位を指定すると `FM_ERR_INVALID_ARG` を返す。

チップが持つ部位は `FmEngine_GetPartMask` で調べられる。bit n が `FmPart` の
n 番に当たり、部位を持たないチップでは 0。

```c
uint32_t mask = 0;
FmEngine_GetPartMask(eng, opnaId, &mask);
if (mask & (1u << FM_PART_OPN_SSG)) {
    // SSG のゲインを設定できる
}
```

### ネイティブサンプルレート

`FmEngine_GetNativeRate` は FM 部のサンプルレート (Hz、端数切り捨て) を返す。
OPN / OPNA では prescale レジスタ (port 0 の `0x2D`〜`0x2F`) の書き込みで
変わる。レジスタ書き込みは `FmEngine_Generate` の中で適用されるので、値が
変わるのはその後になる。

| チップ | リセット時 / `0x2D` | `0x2E` | `0x2F` |
|---|---|---|---|
| OPN         | clock / 72  | clock / 36 | clock / 24 |
| OPNA        | clock / 144 | clock / 72 | clock / 48 |
| OPNB, OPNBB, OPN2 | clock / 144 | 変わらない | 変わらない |
| OPM         | clock / 64  | — | — |
| SSG         | clock / 16  | — | — |

SSG は FM 部を持たないので、トーンのカウンタが進むレートを返す
(トーン周波数 = このレート / (2 × TP) = clock / (32 × TP))。

fmgen は出力サンプルレートで直接波形を生成するので、この値は生成には使われない。

## クロック

`FmEngine_AddChip` の `clock` には、チップに入れるマスタークロック (Hz) を必ず
指定する。DLL は既定のクロックを持たず、0 を渡すと `FM_ERR_INVALID_ARG` を返す。
同じチップでも機種によってクロックが異なり、F-Number やトーン周期などの
レジスタ値はクロックを前提に計算するため。

SSG の `clock` は YM2149 のマスタークロックで、SEL 端子を Low (クロック÷2) に
した扱いになる。トーン周波数は clock / (32 × TP) で、clock = 3,579,545 Hz なら
1,789,772 Hz の AY-3-8910 と同じ音程になる。
