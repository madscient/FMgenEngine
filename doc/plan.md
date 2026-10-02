# plan.md — FmGenEngine 開発経緯

AI 向けの作業記録。セッションをまたいで、仕様の決定、その前提、見送った案、
確認した結果、残っている課題を引き継ぐために書く。

- 利用者向けの現在の仕様は `README.md` にある。この文書からは書き写さない
- 新しい経緯は「経緯」の先頭に足す (新しいものが上)
- 確度の印: **確認済み** (このリポジトリで走らせて確かめた。方法を添える) /
  **未検証** (作ったが走らせていない) / **推測** (根拠を一行添える)

## API の仕様の出どころ

仕様の原本は FMEngineTest (https://github.com/madscient/FMEngineTest) の
`docs/FmEngineApi.md`。決定の経緯は同じリポジトリの `docs/CHANGELOG.md` にある。
FMEngineTest の `src/FmEngineApi.h` は初版のまま更新されていないので、仕様として
読まない。

YMEngine (https://github.com/madscient/YMEngine) は参照実装。ヘッダ
(`src/FmEngineApi.h`) の書き方と、仕様が定めない細部の振る舞いはこちらに合わせる。
仕様書の改定が YMEngine より先に入ることがあるので、追従するときは必ず仕様書から
見る。

追従の状況 (FMEngineTest `origin/main` の `docs/FmEngineApi.md` で見る):

| 仕様書のコミット | 内容 | FmGenEngine |
|---|---|---|
| `e39b206` | 部位ごとのゲイン | 対応済み |
| `e002890`、`c0589c1` | 外部メモリの ROM/RAM (`FmEngine_SetMemoryEx` は任意) | 一部対応 (列挙値と `SetMemory` の規則。`SetMemoryEx` はエクスポートしない。OPNA の ROM モードは**未対応**) |
| `866f4a3` | `AddChip` の clock=0 を廃止 | 対応済み |

次に追従するときは、`866f4a3` から先の `docs/FmEngineApi.md` の差分と、
未対応の部分を見る。

## 回帰テスト (`_test/`)

CMake には組み込んでいない。fmgen のソースと `src/FmGenEngine.cpp` を一緒に
ビルドし、C API を直接呼ぶ。

| ファイル | 見ていること |
|---|---|
| `api_test.cpp` | `AddChip` が clock=0 を拒否すること。部位ごとのゲインの受け付け・既定値・読み戻し・`GetPartMask`・不正な引数の拒否 (accept)。`FM_PART_OPN_FM` が FM と ADPCM に、`FM_PART_OPN_SSG` が SSG に掛かること、L/R が独立なこと、チップのゲインとの積になること (route)。既定のゲインで fmgen の `Mix` と全サンプル一致すること (default)。`GetNativeRate` の値と、その値が fmgen の実際のレートであることを FM と SSG の音程から確かめる。単体 SSG と OPN 系の SSG 部のトーン周波数が TP から正しく決まること (native)。`SetMemory` の拒否・`GetMemorySize`・エンジンが data に書き込まないこと (memory) |

Windows (vcvars64.bat を通した cmd、リポジトリ直下で):

```cmd
mkdir build\test
cl /std:c++17 /EHsc /O2 /utf-8 /DFMENGINE_EXPORTS /FI types.h /I src /I extern\fmgen /I extern _test\api_test.cpp src\FmGenEngine.cpp extern\fmgen\*.cpp /Fo:build\test\ /Fe:build\test\api_test.exe
build\test\api_test.exe
```

全件通れば終了コード 0。**確認済み** (MSVC 19.51、上のコマンドそのままで実行)。
Windows 以外ではビルドできない。**推測**: fmgen の `headers.h` が `windows.h` を
include しているため。試していない。

走らせる場面: `FmGenChip.h` / `FmGenExtChip.h` / `FmEngine.h` / `FmGenEngine.cpp`
の部位・ゲイン・レート・クロック・外部メモリまわり、fmgen の `Mix` / `MixSplit` /
`SetPrescaler` / ADPCM のメモリアクセスを変えたとき。

テストは `operator new` を「確保したメモリを 0 で埋める」ものに差し替えている。
fmgen に未初期化のまま使われるメンバがあり (下の「気づいたが手を付けていない
こと」)、そのままでは同じ書き込みをした2つのインスタンスの出力が一致しない
ため。fmgen を直した場合もこの差し替えは残してよい。

## 経緯

### 2026-10-02 外部メモリの改定に一部追従する (列挙値と SetMemory の規則)

仕様書 (FMEngineTest `e002890`、`c0589c1`) への追従。範囲は利用者と決めた。

範囲 (利用者と決めた): 列挙値と `FmEngine_SetMemory` の規則だけ。
`FmEngine_SetMemoryEx` (任意のエクスポート) はエクスポートしない。
見送った案: `SetMemoryEx` を ROM だけ対応する (RAM は `FM_ERR_UNAVAILABLE`。
割り当てを管理する層が要る)、RAM のその場読み書きまで対応する (fmgen の ADPCM の
メモリアクセスを差し替える改造が要る)。

- ヘッダに `FM_MEM_ADPCM_B_ROMMODE` と `FmMemoryAccess` を足した。`SetMemoryEx`
  は宣言しない (インポートライブラリに無い関数を宣言すると、直接呼んだアプリが
  リンクで失敗するため)
- `SetMemory`: `ROMMODE`、範囲外の種別、size 0、未知の chip_id を
  `FM_ERR_INVALID_ARG` で拒否する。チップが持たない種別は `FM_OK` で無視する。
  どれも YMEngine (`ac29207`) と同じ。未知の chip_id は、これまで `assert` だけで
  Release では範囲外アクセスになっていた
- 「エンジンは data に書き込まない」は、変える前から満たしていた (コードで確認:
  ADPCM-B のメモリへ書く `WriteRAM` を呼ぶのは `OPNA::SetReg` だけで、OPNA は
  fmgen 内部のバッファに写して使う。OPNB/OPNBB の ADPCM-A/B は読むだけ)
- テストの名前を `part_gain_test.cpp` から `api_test.cpp` に変えた (部位ゲイン
  以外も見るようになったため)。この文書の古い節の `part_gain_test` はこれを指す

前提: 仕様が定めない `SetMemory` の細部 (size 0、チップが持たない種別) は
YMEngine に合わせる。

確認 (**確認済み**):

- 先にテストに memory の項目を足し、直す前のコードで、拒否の項目が落ち、未知の
  chip_id で `assert` が発生して止まることを見た。直したあとは全件通る
- OPNA でチップ経由でメモリに書かせても、渡したバッファは変わらない。書き込みが
  実際に起きたことは、再生の出力が書かせない場合と食い違うことで確かめた
- CMake (VS 18 2026、Release) でビルドが通る

**未対応 — OPNA の ADPCM-B の ROM モード**。仕様では、ROM/RAM 選択ビットが ROM の
ときは別のメモリ (`FM_MEM_ADPCM_B_ROMMODE`) にアクセスし、割り当てが無ければ 0 を
読む。fmgen は ADPCM の control2 の bit0 (ROM 選択) を見ておらず (コードで確認:
`opna.cpp` で使うのは bit1 と bit6/7 だけ)、ROM モードでも `FM_MEM_ADPCM_B` の
データを読む。README に現状として書いた。直すなら fmgen の `ReadRAM` /
`ReadRAMN` で bit0 を見て 0 を返す改造になる (YMEngine は `ac29207` で同じ扱いに
した)。

### 2026-10-02 AddChip の clock=0 (標準クロック) を廃止する

仕様書 (FMEngineTest `866f4a3`) への追従。仕様: `FmEngine_AddChip` は clock=0 で
`FM_ERR_INVALID_ARG` を返す。エンジンは既定のクロックを持たない。経緯は
FMEngineTest の `docs/CHANGELOG.md`。

- `FmGenEngine.cpp`: `AddChip` は clock=0 なら `FM_ERR_INVALID_ARG` を返し、チップを
  追加しない
- `FmGenChip.h`: 標準クロックの定数 (`FmGenClock`) を削除。`createFmGenChip` /
  `createFmGenExtChip` は clock=0 で `std::invalid_argument` を投げる (C++ から
  直接使う場合の守り)。`FmEngine::addChip` / `addExtChip` の既定引数を削除
- README のクロックの表を削除し、必ず指定することと SSG の clock の意味を書いた

仕様が定めないところ (相談せずに決めた):

- clock=0 と未知の名前が重なったときは `FM_ERR_INVALID_ARG` を返す。null の
  ハンドル・出力先と同じく、引数の検査を名前を引くより先に行う。
  見送った案: 名前を先に引いて `FM_ERR_UNKNOWN_CHIP` を返す。理由: 引数の検査の
  順序がばらばらになる。
  やり直しの値段: `FmGenEngine.cpp` の検査の順序と、テストの1項目

確認 (**確認済み**):

- 先に `part_gain_test` を直した (全箇所でクロックを明示し、clock=0 の拒否を
  確かめる項目を足した)。直す前のコードでは clock=0 の項目だけが落ち、明示した
  クロックでほかの全項目が通った。直したあとは全件通る
- 改定前 (`3ba7f6d`) の DLL との比較を、両方に同じクロックを明示して行った。
  食い違うのは単体 SSG を含む4件 (前の節の音程の修正による) だけで、残り30件は
  不一致 0
- CMake (VS 18 2026、Release) でビルドが通る

FMEngineTest の確認 (FMEngineTest の `docs/CHANGELOG.md`) では、パッチの SSG の
clock は 3,579,545 で、根拠は旧 YMEngine (emu2149 に ÷2 を設定) の値。前の節の
単体 SSG の修正 (SEL=Low) で、この clock のときの音程は旧 YMEngine と同じになる
(コードと測定からの見立て。FMEngineTest で鳴らしてはいない。**未検証**)。

外部メモリの改定 (`e002890`、`c0589c1`) にはまだ追従していない。対応の範囲は
利用者と決める (`FmEngine_SetMemoryEx` は任意のエクスポート)。

### 2026-10-02 単体 SSG のトーン周波数を直す

下の節の「気づいたが手を付けていないこと」の 3 への対処。利用者の見立ては
「単体 PSG の TP 計算の不具合」。

不具合の場所: fmgen の PSG はトーン周波数 = 渡したクロック / (8 × TP) で、
AY-3-8910 の式 (クロック / (16 × TP)) から見ると AY 相当のクロックの半分を
受け取る約束になっている。OPN 系では fmgen が自分で分周して渡すので正しく、
単体 SSG のラッパー (`FmGenPsgChip`) だけが YM2149 のマスタークロックを
そのまま渡していた。標準クロックでは4倍 (2オクターブ上) で鳴っていた。

直し方: `FmGenPsgChip` が fmgen の PSG に clock/4 を渡す。単体の YM2149 は
SEL を Low (クロック÷2) とした扱いで、トーン周波数は clock / (32 × TP)。
エンベロープとノイズの周期も同じ値から決まる (`PSG::SetClock` の
`eperiodbase` / `nperiodbase` も clock に比例。コードで確認) ので一緒に直る。
fmgen の `psg.cpp` は直さない (OPN 系の SSG が狂う)。

外から見える変化 (どちらも利用者と決めた定義から導いた値):

- 単体 SSG の音程が、同じクロック・同じ TP で2オクターブ下がる
- `GetNativeRate` (トーンのカウンタのレート) が clock/4 から clock/16 に変わる
  (標準クロックで 894,886 → 223,721)

前提: 単体の YM2149 は SEL=Low (÷2) とする。旧 YMEngine が emu2149 に
`PSG_setClockDivider(1)` を設定していたのに合わせた。SEL=High の扱いに変える
なら、`FmGenExtChip.h` の `psgClock()` の除数を 4 → 2、`nativeRate()` を
16 → 8、テストの期待値と README を直す。

確認 (**確認済み**):

- 先に `part_gain_test` の期待値を直し、直す前のコードで単体 SSG の2項目が
  落ちることを見た (TP=1000 で 447.44Hz、正しくは 111.86Hz)。直したあとは
  全件通り、111.86Hz で鳴る
- 同じテストに、OPN 系の SSG 部のトーン周波数の確認を足した。OPN は
  clock / (32 × TP)、OPNA/OPNB/OPNBB は clock / (64 × TP) で、YMEngine (ymfm) の
  SSG のレートから導いた式と一致した (直す前も後も)。fmgen の PSG クラス自体は
  正しいことの裏付け
- 改定前 (`3ba7f6d`) の DLL との比較で、食い違うのは単体 SSG を含む4件
  (SSG、OPNA+OPM+SSG の各2通り) だけ。残り30件は不一致 0
- エンベロープとノイズの周期は測っていない (**未検証**)

### 2026-10-02 OPN2 のプリスケーラを 1/6 固定にする

下の節の「気づいたが手を付けていないこと」の 2 への対処。

利用者の指摘: YM2612 のプリスケーラは 1/6 固定。FmGenEngine の OPN2
(`opna_ext.cpp`) は port0 の 0x2D〜0x2F を `SetPrescaler` に渡しており、書くと
FM のクロックが 2倍・3倍になっていた。

- `opna_ext.cpp` の `OPN2::SetReg` で 0x2D〜0x2F を無視するようにした
- `FmRate` から OPN2 を外し、分周比を clock/144 に固定した。これで
  `GetNativeRate` の振る舞いが YMEngine の README (prescale で変わるのは
  OPN/OPNA だけ) とそろった
- 前提に「OPN2 は 0x2D〜0x2F を無視する」が加わった (`FmRate` が依存する)

外から見える変化: OPN2 に 0x2D〜0x2F を書いたときだけ、音程と `GetNativeRate`
が変わらなくなる。書かなければ出力は同じ。

確認 (**確認済み**):

- 先に `part_gain_test` の OPN2 の期待値を「0x2D〜0x2F で変わらない」に変え、
  直す前のコードで 0x2E/0x2F の2項目が落ちることを見た (音程が 502.14Hz /
  753.22Hz に上がっていた)。直したあとは全件通り、音程は 251.07Hz のまま
- 改定前 (`3ba7f6d`) の DLL との比較 (下の節と同じ方法) で、34 件すべて不一致 0。
  比較の OPN2 のケースは 0x2D〜0x2F を書かないので、ここで出力は変わらない

### 2026-10-02 FmEngineApi の改定 (YMEngine `8f81213` まで) に追従

#### YMEngine 側の改定の中身

改定前の FmGenEngine のヘッダは、冒頭のコメントを除いて YMEngine `39fe9f1`
(2026-06-24) のものと同じだった (差分で確認)。そこからの API の差分:

- `FmPart` 列挙 (0〜8) を追加。チップが別々の端子から出す出力 (部位) の番号で、
  チップをまたいで重ならない
- `FmEngine_SetPartGain` / `FmEngine_GetPartGain` / `FmEngine_GetPartMask` を追加。
  実際のゲインは「チップのゲイン × 部位のゲイン」。チップが持たない部位は
  `FM_ERR_INVALID_ARG`
- `FmEngine_GetNativeRate` の意味を「FM 部のネイティブサンプルレート (端数切り
  捨て)。OPN/OPNA では prescale の書き込みで変わる」と明記

FmGenEngine は改定前から `GetNativeRate` で出力サンプルレートを返していた
(YMEngine は ymfm の内部レートを返していた)。今回、仕様どおりの値に直した。

#### 外から見える値 (利用者と決めた)

- **単体の SSG チップの `GetNativeRate` は clock/4** (標準クロックで 894,886)。
  (→ 同日、トーン周波数の修正に伴い、同じ定義のまま clock/16 になった。上の節)
  SSG は FM 部を持たないので、fmgen の PSG がトーンのカウンタを進めるレートを
  返す。OPN 系の SSG 部をこの数え方で数えると、YMEngine の SSG のレート
  (OPN は clock/16、OPNA は clock/32) と一致する (fmgen と YMEngine のコードを
  読んで確認)。
  見送った案: 出力サンプルレートのまま (他のチップと意味がそろわない)、0 を返す
  (未知の chip_id に対する戻り値と区別できない)。
  やり直しの値段: `FmGenExtChip.h` の除数1か所、テストの期待値、README。
- **改名 (FmGenEngineApi → FmGenEngine) の取り残しを全部そろえた**。`.def` の
  `LIBRARY`、`.rc` の ProductName / InternalName / OriginalFilename、README・
  LICENSE・コメントのファイル名。
  見送った案: `.def` だけ直す、触らずに記録だけする。

#### 仕様から導いた値 (相談せずに決めた)

- 部位を持つのは OPN / OPNA / OPNB / OPNBB (`FM_PART_OPN_FM` と `FM_PART_OPN_SSG`、
  マスク 0x3)。OPN2 / OPM / SSG は部位を持たない (マスク 0)。前提: YMEngine の
  `FmPart` が変わらないこと。単体の SSG に部位を与える値は `FmPart` に無い
- `FM_PART_OPN_FM` には ADPCM-A/B と OPNA のリズムを含める (YMEngine の仕様どおり)
- OPN2 の `GetNativeRate` も prescale の書き込みで変える。FmGenEngine の OPN2
  (`opna_ext.cpp`) が port0 の 0x2D〜0x2F を受け付けて FM のクロックを変える
  ので、実際の動作に合わせた。YMEngine の README では prescale で変わるのは
  OPN/OPNA だけ (→ 同日、OPN2 側を 1/6 固定に直して取りやめた。上の節)
- 部位のゲインの既定値 1.0 で、改定前と同じ出力 (ビット単位) にする

#### 実装

- fmgen (`opna.h` / `opna.cpp`、`[FmGenEngine]` マーカー付き): `OPN` / `OPNA` /
  `OPNB` に `MixSplit(fm, ssg, n)` を足した。元の `Mix` の本体を移し、`Mix` は
  `MixSplit(buf, buf, n)` を呼ぶ。`OPNBB` は `OPNB` から継承する
- `FmGenChip.h`: `ChipPart` (FM/SSG の2つだけ。番号は `FmPart` と同じで、
  `FmGenEngine.cpp` の `static_assert` で照合)、`PartGains`、`hasPart()`。
  OPN 系は `MixSplit` で2本のバッファに出し、部位のゲインを掛けて足す
- `FmEngine.h`: チップごとに部位のゲインを atomic で持つ。チップのゲインは
  部位を混ぜたあとに掛ける (改定前と同じ位置)
- ネイティブレート: `fmgen_detail::FmRate` が書き込みを見て、fmgen の
  `OPNBase::SetPrescaler` と同じ規則で分周比を切り替える。書き込みは
  オーディオスレッドで適用され、読むのは任意のスレッドなので atomic
- `.def` の `LIBRARY` を `FmGenEngine` に直した。食い違うと CMake の configure で
  止まる検査を `CMakeLists.txt` に入れた (リンカの LNK4070 は警告で通ってしまう)

前提 (これが崩れると `FmRate` が fmgen とずれる。`part_gain_test` の native の
音程の確認で落ちる):

- fmgen の prescale の番号は `0x2D→0, 0x2E→1, 0x2F→2`、分周は FM が 6/3/2 × 12
- `OPNA` / `OPNB` は `SetRate` でクロックを半分にし、FmGenEngine の `OPN2` も
  半分にして渡す
- `OPNB` (と `OPNBB`) は 0x2D〜0x2F を無視する。`OPN` / `OPNA` / `OPN2` は
  port1 側の 0x2D〜0x2F を無視する

見送った案:

- `ChipPart` に `FmPart` の9つ全部を置く (YMEngine と同じ形)。理由: このエンジンの
  チップが持たない部位の定義だけが残る。範囲外として拒否すれば API の振る舞いは
  同じ
- fmgen を改造せず、派生クラスで FM と SSG を分ける。理由: OPN の FM 部
  (`ch` / `fnum`)、OPNA の `RhythmMix`、OPNB の `ADPCMAMix` が private で、
  派生クラスから呼べない。可視性を変えるか本体を写すことになり、写すと `Mix` と
  二重管理になる
- PSG のチャンネルマスクで SSG を止めて `Mix` を分ける。理由: `PSG::Mix` は
  マスクしてもカウンタを進める (コードで確認) ので、SSG が2倍の速さで進む
- SSG だけ別の PSG インスタンスで鳴らす。理由: prescale による SSG クロックの
  切り替え (`SetPrescaler` の `table[p][1]`) をラッパー側に写すことになる
- `GetNativeRate` を fmgen の内部状態から読む。理由: prescale は `OPNBase` の
  private で、読むには fmgen にアクセサを足す改造が要る。ラッパー側で規則を写し、
  音程で確かめる方を選んだ
- YMEngine のように、チップのゲインを `PartGains` に入れてチップの中で掛ける。
  理由: 掛け算の順序が変わり、チップのゲインが 2 の冪以外のとき改定前の出力と
  ビット単位で一致しなくなる
- YMEngine の改定のうち API に現れないもの (KEY OFF→ON の衝突時の保留、
  `LinearResampler` の修正、OPN 系の FM/SSG のネイティブレート生成) は持ち込んで
  いない。どれも ymfm と YMEngine のリサンプラに固有の問題への対処。fmgen に
  同種の問題 (同じチャンネルへの KEY OFF→ON が生成の前に続くと KEY OFF が
  音に出ない) があるかは**未確認**。決めるには YMEngine の
  `keyoff_retrigger_test.cpp` と同じ形の試験を fmgen で走らせる

#### 確認

**確認済み** — 改定前 (`3ba7f6d`) と改定後の DLL の出力の比較。両方を `cl` で
ビルドし、同じプロセスに読み込んで同じ書き込みをし、`Generate` の出力
(51,700 サンプル × L/R、480 と 37 サンプルの呼び出しを交互) をビット単位で
比べた。比較用のプログラムはリポジトリに残していない。

- 対象: OPN (FM / SSG / FM+SSG、prescale の切り替えを含む)、OPNA (FM と LFO /
  SSG / ADPCM-B / 全部)、OPNB (FM / SSG / ADPCM-A / ADPCM-B / 全部)、OPNBB (全部)、
  OPN2 (FM と DAC)、OPM (LFO とノイズを含む)、SSG、1つのエンジンに
  OPNA+OPM+SSG。それぞれチップのゲイン 1.0/1.0 と 0.3/0.7 の2通り。34 件すべて
  不一致 0。どのケースも鳴っている (0 でないサンプルが 4 割以上)
- 両方の DLL に「確保したメモリを 0 で埋める `operator new`」を入れてビルドした。
  入れないと、同じ DLL の写しどうしでも SSG・OPM・ADPCM-B のケースが食い違う
  (下の「気づいたが手を付けていないこと」の 1)
- 対照: 改定前の DLL とその写しの比較で全件一致。改定後の側だけ SSG の部位
  ゲインを 0 にすると、SSG を含むケースだけが食い違い、FM・ADPCM だけのケースは
  一致した (ADPCM が FM 側の部位に入っていることの確認にもなる)
- OPNA のリズムは試していない (WAV ファイルが無い)。**推測**: `MixSplit` で
  `RhythmMix` は FM 側のバッファに足すので、ADPCM-B と同じ経路になる。根拠は
  コードを読んだことだけ

**確認済み** — `part_gain_test` が全件通る (MSVC 19.51)。試験が効いていることを、
`src` の写しに次の改変を入れて確かめた:

- FM と SSG のゲインの取り違え → route の全項目が落ちる
- OPNB も prescale で分周比が変わることにする → native の OPNB の 0x2E/0x2F が
  落ちる。期待値との比較に加え、測った音程 (261.68Hz) と、報告されたレートから
  計算した音程 (523.88Hz / 785.82Hz) の食い違いでも分かる

音程の確認の許容幅は 0.5%。分周比の取り違え (1.5 倍以上) は見分けられるが、
それより細かい差は見ていない。

**確認済み** — CMake (Visual Studio 18 2026、Release) でビルドが通り、LNK4070 が
出なくなった。dumpbin で、インポートライブラリの参照先が `FmGenEngine.dll` で
あること、DLL が新しい3関数を含む 17 関数をエクスポートすることを見た。
`.def` の `LIBRARY` だけを旧名に戻した写しで configure が止まることも見た。

試験していないこと:

- `SetPartGain` を `Generate` と並行して呼ぶこと。**未検証**: atomic の読み書き
  だけで、負荷を掛けた試験はしていない
- CMake の Debug 構成、MSVC 以外のコンパイラ

#### 気づいたが手を付けていないこと

1. **fmgen の出力が実行ごとに変わる**。**確認済み**: 改定前の同じ DLL を2つ
   読み込んで同じ書き込みをすると、SSG・OPM・ADPCM-B の出力が食い違う。メモリを
   0 で埋めると一致する。原因の1つは、`PSG` の `scount` / `ncount` / `ecount` を
   コンストラクタでも `Reset()` でも初期化していないこと (コードで確認)。
   OPM と ADPCM-B で何が未初期化なのかは**未特定**。決めるには、メモリを 0 以外の
   値で埋めて、出力が変わるメンバを探す
2. **OPN2 が prescale (0x2D〜0x2F) を受け付ける**。実機の YM2612 にこのレジスタが
   あるかは**未確認**。無いなら `opna_ext.cpp` で無視するのが正しく、そのときは
   `FmRate` も OPN2 を外す (→ 同日、1/6 固定に直した。上の節)
3. **単体の SSG の標準クロック** (`AddChip` に clock=0 を渡したときの値。
   `FmGenClock::SSG` = 3,579,545Hz、README の「クロック」節)。
   - 出どころ: `FmGenChip.h` のコメントは「YMEngine の FmClock と同値」。今の
     YMEngine の `FmClock` に SSG は無い。外部チップを削除する前の YMEngine
     (`130c5e8` の親) の `ExternalChip.h` に `ExtClock::SSG = 3'579'545; // NTSC`
     があり、emu2149 に `PSG_setClockDivider(1)` (YM2149 のクロック÷2) を設定して
     使っていた。初回コミット (`e2c953a`) でこの値だけを写したと見られる
   - emu2149 (v1.42) のトーン周波数は、÷2 後のクロック / (16 × TP)。コードで確認
     (`internal_refresh` で ÷2、カウンタは ÷8 のレートで進み TP ごとに反転)。
     旧 YMEngine の SSG は 1,789,772 / (16 × TP) で鳴っていた。**未検証**:
     emu2149 は走らせていない
   - fmgen の PSG のトーン周波数は、渡したクロック / (8 × TP) (**確認済み**:
     TP=1000 で 447.44Hz を測った)。FmGenEngine は 3,579,545 をそのまま渡すので、
     同じ TP で旧 YMEngine の4倍 (2オクターブ上) になる
   - 旧 YMEngine と同じ音程にするには、fmgen の PSG に clock/4 を渡す。そうすると
     トーンのカウンタのレート (`GetNativeRate`) も clock/16 に変わる
   - (→ 同日、この方法で直した。上の節)
4. **未知の chip_id を `FmEngine_SetGain` / `GetGain` / `Write` / `SetMemory` に
   渡すと範囲外アクセスになる** (`assert` だけで、Release では素通り。コードで
   確認)。YMEngine も同じ。今回足した3関数は検査している
   (→ 同日、`SetMemory` は外部メモリの追従で直した。上の節。残りは `SetGain` /
   `GetGain` / `Write`)
5. **OPNA (7.9872MHz) と OPNB (8MHz) で、同じ fnum の音程が同じ 261.68Hz と
   測れた**。クロック比 0.16% の差が音程に出ていない。原因は**未確認**。
   fmgen の `Operator::Prepare` の位相増分の計算 (丸め) を読めば決まる

### 2026-06-22〜2026-08-28 (git の記録から再構成)

このセッションより前の経緯。コミットメッセージと差分から読み取れる範囲だけを書く。

- fmgen 0.08 を `extern/fmgen/` に同梱し、UTF-8 に変換。OPNB の `ch[6]` の重複
  宣言を削除。OPN2 と OPNBB を `opna_ext.*` に追加実装し、OPN2 の prescale を
  補正 (`bef3c67`、`6e92d4b`)
- オーディオ出力 (WASAPI) とテストコードを DLL から分離 (`f57dcba`)
- YMEngine の API 改定 (チップを文字列で指定、`FmEngine_Inquiry` /
  `FmEngine_GetSupportedChip`) に追従 (`1756f39`)
- 出力ファイル名を FmGenEngineApi から FmGenEngine に変更 (`3ba7f6d`)。このとき
  `.def` の `LIBRARY` が旧名のまま残り、インポートライブラリが
  `FmGenEngineApi.dll` を参照していた (2026-10-02 に修正)
