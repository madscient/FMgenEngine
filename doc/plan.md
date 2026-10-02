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
| `e002890`、`c0589c1` | 外部メモリの ROM/RAM (`FmEngine_SetMemoryEx` は任意) | 対応済み (`SetMemoryEx` をエクスポートする) |
| `866f4a3` | `AddChip` の clock=0 を廃止 | 対応済み |

次に追従するときは、`866f4a3` から先の `docs/FmEngineApi.md` の差分を見る。

## 回帰テスト (`_test/`)

CMake には組み込んでいない。fmgen のソースと `src/FmGenEngine.cpp` を一緒に
ビルドし、C API を直接呼ぶ。

| ファイル | 見ていること |
|---|---|
| `api_test.cpp` | `AddChip` が clock=0 を拒否すること。部位ごとのゲインの受け付け・既定値・読み戻し・`GetPartMask`・不正な引数の拒否 (accept)。`FM_PART_OPN_FM` が FM と ADPCM に、`FM_PART_OPN_SSG` が SSG に掛かること、L/R が独立なこと、チップのゲインとの積になること (route)。既定のゲインで fmgen の `Mix` と全サンプル一致すること (default)。`GetNativeRate` の値と、その値が fmgen の実際のレートであることを FM と SSG の音程から確かめる。単体 SSG と OPN 系の SSG 部のトーン周波数が TP から正しく決まること (native)。`SetMemory` の拒否・`GetMemorySize`・エンジンが data に書き込まないこと (memory)。`SetMemoryEx` の受け付けと範囲の検査、`SetMemory` が割り当てを置き換えること (memex)。OPNA の ADPCM-B が ROM/RAM 選択ビットの側のメモリだけを読むこと、ROM モードの番地の単位と並び、ブロックを分けても同じ出力になること (play)。転送が RAM のブロックにその場で入り ROM には入らないこと、入る時点、ROM モードと x8 の転送の行き先、RAM のブロックを複製しないこと (store)。割り当ての無い番地で 0 を読み書き込みを捨てること、OPNB が落ちないこと (unmapped) |

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
`SetPrescaler` / ADPCM のメモリアクセス (`ReadRAM` / `WriteRAM` / `ReadRAMN` /
`ADPCMAMix`、差し替え口) を変えたとき。

テストは `operator new` を「確保したメモリを 0 で埋める」ものに差し替えている。
fmgen に未初期化のまま使われるメンバがあり (下の「気づいたが手を付けていない
こと」)、そのままでは同じ書き込みをした2つのインスタンスの出力が一致しない
ため。fmgen を直した場合もこの差し替えは残してよい。

## 経緯

### 2026-10-02 OPNA の ADPCM-B の x8 を番地順にする

#### 経過

1. 利用者の指摘: RAM の x1/x8 の解釈が間違っている。Y8950 のアプリケーション
   マニュアル (IV-2 外部メモリー・インターフェイス、図IV-3) によれば、データ 1 ビットの
   D-RAM を 1 個だけつなぐときが x1、8 ビットのデータバスを全部使うときが x8 と読める。
   図: D-RAM 8 個の DO は、MDEN で開くトライステートバッファを通って DTO と DM1〜7 に
   並列に出る (ROM の D0〜D7 と同じ線)。D-RAM 1 個の最小構成では外部回路が要らない
2. 利用者の追加: x1 は番地 18 ビット・データ 1 ビットなので 32KB まで。これを受けて、
   いったん x1 を 32KB で折り返すようにした
3. 利用者の追加 (YM2608 のデータシートと照合): 回路図は無いが、対応するピンが同じ
   なので回路は同じ。Y8950 は D-RAM を 1〜8 個つなぎ、全部を 1 ビットずつ直列に使う
   (数をソフトから指定しない)。YM2608 は x1/x8 をソフトから選ぶ
4. 利用者が示した YM2608 の資料 (スタート/ストップアドレス $02〜$05 の説明) で、x1 の
   番地が分かった: ビットの番地は 21 ビットで、上位 3 ビットの BANK が 8 個の D-RAM の
   チップセレクト、残りが CAS 9 ビット・RAS 9 ビット。レジスタの 16 ビットは上位 16
   ビットに当たり、下位 5 ビットは開始で 0、終了で 1。x1 はビット単位のアクセスで
   最小分解能 32 ビット (4 バイト)。ROM と D-RAM の x8 はバイト単位のアクセスで
   最小分解能 32 バイト、BANK は開始・終了で同じ値にする。
   x1 も D-RAM 8 個を直列に使えて範囲は 256KB なので、2 の 32KB は取りやめた
   (コミット前)。前の節の ROM モードの **推測** (32 バイト単位・番地順、範囲 256KB) は、
   この資料で裏付けられた

#### 決めたこと (利用者と決めた)

- **メモリは、そのモードでチップが読み書きするバイトを番地順に並べたもの** として
  扱う。x1 も x8 も ROM モードも番地順。YMEngine・ymfm と同じ。
  利用者の判断: ハードウェアの仕様とは異なるが、1 ビットずつ直列に使う場合に、
  ホストや他のデバイスとメモリを共有することは考えづらいので、ソフトから見える形に
  そろえる。
  前提: 1 ビットずつ直列に使う D-RAM を、ホストや他のデバイスと共有する構成を扱わない
  こと。崩れたら下の P を考え直す。
  見送った案 P (fmgen の元の表し方): メモリを D-RAM 8 個の中身を順に並べたもの
  (1 個目の 32KB、2 個目の 32KB、…) とし、x1 は番地順、x8 は各バイトのビット p を
  面 p (D-RAM p) に振り分ける。x1 と x8 をまたいでも実機どおりに動くが、x8 用の
  イメージを振り分けた形で渡す必要がある。
  見送った案 B (利用者の提案、4 の前): メモリを 18 ビットの番地 × 8 ビットとし、x1 は
  各要素の LSB だけを使う。x1 を D-RAM 1 個と読んでいたときの案。x1 のイメージが
  1 バイトを 8 要素に展開した形になり、`SetMemory` に番地順のバイト列を渡して x1 で
  鳴らしているアプリが鳴らなくなる
- x1 の範囲は 256KB (4 の資料による。fmgen のまま)

#### 直したこと

- x8 は番地順 (32 バイト単位)。fmgen には x8 の振り分けをしない読み方が元から
  あった (`NO_BITTYPE_EMULATION` を定義したとき。fmgen の注記は「ADPCM データの
  格納方式の違い (8bit/1bit) をエミュレートしない」)。この切り替えを削除して、その
  読み方だけを残した。x1 はその読み方でも今までと同じ計算 (コードで確認、下の比較で
  も一致)。ROM モードも、前の節で足した分岐をやめ、同じ読み方 (granuality 1) に
  まとめた
- `granuality` を `OPNABase` のコンストラクタで 4 (x1) に初期化した。残した読み方は
  x1 でも `granuality` を使うので、control2 を書かずにリセット状態のまま転送すると
  不定の値で番地を計算していた (前の節の「気づいたが手を付けていないこと」の 3)

外から見える変化: x8 でメモリを読み書きするとき、バイトの並びが変わる
(`SetMemory` / `SetMemoryEx` で渡したデータを x8 で鳴らすと、今までと違う音になる)。
x1 で書いたデータを x8 で読むと、実機とは違う値になる (README に書いた)。

やり直しの値段: P に戻すなら、fmgen の `WriteRAM` / `ReadRAM` / `ReadRAMN` の x8 の
分岐を元に戻し (`c4a4a9e` にある)、テストの x8 の期待値 (play / store / default) と
README の番地の節を直す。

#### 確認 (**確認済み**)

- 先にテストを直した (x8 の転送の行き先と x8 の再生を番地順の期待に、リセット状態の
  転送、x1 が 32KB の境目をまたいでも折り返さない転送と再生を追加)。直す前のコード
  (`c4a4a9e`) では x8 の2項目が落ちた。リセット状態と 32KB の項目は直す前も通る
  (直す前の x1 は `granuality` を使わず、256KB)。直したあとは全件通る (MSVC 19.51)
- 今のコードの写しに改変を入れ、テストが落ちることを見た: `granuality` を初期化
  しない → リセット状態の項目、x1 を 32KB で折り返す → 32KB をまたぐ転送と再生の
  2項目、ROM モードも bit1 で刻みを決める → ROM の2項目。前の節の改変 (選択ビットを
  見ない、RAM を写す、差し替え口の読み出しを壊す、x8 の読み出しだけ 0、ROM にも書く)
  もすべて落ちた
- 直す前 (`c4a4a9e`) と後の出力の比較 (前の節と同じプログラム): x8 の転送と再生だけが
  食い違い、ほかの12件は一致した

### 2026-10-02 OPNA の ADPCM の ROM/RAM を区別し、SetMemoryEx をエクスポートする

仕様書 (FMEngineTest `e002890`、`c0589c1`) への追従の残り。利用者の指示: OPNA の
ADPCM メモリを仕様どおり RAM と ROM で区別する。`FmEngine_SetMemoryEx` にも対応する。

#### 利用者と決めたこと

- **何も割り当てていないメモリは、何もつながっていない扱い** (読むと 0、書き込みは
  捨てる)。仕様の文言と YMEngine に合わせた。
  外から見える変化: `SetMemory` も `SetMemoryEx` もせずにレジスタ経由で ADPCM-B を
  転送して鳴らしていたアプリは、無音になる。FMEngineTest は OPNA の ADPCM-B を
  使っていない (パッチを読んで確認)。
  前提: アプリが RAM をつなぐ手段 (`SetMemoryEx` の RAM、`SetMemory` の写し) を
  持つこと。
  見送った案: 既定で 256KB の内部 RAM をつないでおき、ADPCM_B に初めて割り当てた
  ときに外す (今までの動作が残るが、規則が1つ増える)。
  やり直しの値段: ラッパーのコンストラクタで内部 RAM を割り当てる数行、テストの
  unmapped の1項目、README。
- **ROM モードは実機に合わせる**: x1/x8 の選択 (bit1) によらず、32 バイト単位で
  番地順に読み書きする。RAM モード (x1/x8) は fmgen のまま (→ 同日、x8 も番地順に
  した。上の節)。
  根拠: ymfm の `adpcm_b_channel::address_shift()` (ROM なら 5 ビット。YMEngine の
  `extern/ymfm` で読んだ) と、FMEngineTest の CHANGELOG にある Y8950 のマニュアルの
  ROM の記述 (バイト単位のアクセス、32 バイト単位のアドレス)。
  前提: YM2608 の ROM モードが Y8950 と同じ読み方であること。**推測** (YM2608 の
  資料は見ていない)。崩れたら ROM の分岐を直す。(→ 同日、利用者が示した YM2608 の
  資料で裏付けられた。上の節)
  見送った案: fmgen のまま (ROM も bit1 で x1/x8 の読み方をする。実機用の ROM
  イメージがそのままでは鳴らない見込み)、x8 も番地順にする (YMEngine と同じ並び。
  x8 で `SetMemory` したデータの読まれ方が変わり、x1/x8 を切り替えたときの fmgen の
  再現が失われる)。
  やり直しの値段: fmgen の `WriteRAM` / `ReadRAM` / `ReadRAMN` の ROM の分岐と
  `SetADPCMBReg` の1行、テストの play / store の各1項目、README の表。

#### YMEngine に合わせて決めたこと

相談せず、上の「仕様が定めない細部の振る舞いは YMEngine に合わせる」による。実装の
前に一覧を利用者に示した。

- `SetMemoryEx` が受け付ける種別: OPNA は ADPCM_A / ADPCM_B / ADPCM_B_ROMMODE、
  OPNB/OPNBB は ADPCM_A / ADPCM_B。ほかのチップは持たない。OPNA の ADPCM_A
  (リズムの内蔵 ROM) は受け付けて読まない (fmgen のリズムは WAV)
- 検査: size 0、base + size が 2^32 を越える、重なり、未知の access は
  `FM_ERR_INVALID_ARG`。data が null なら重なる割り当てを外す (access は見ない)。
  `FM_ERR_UNAVAILABLE` は返さない
- `SetMemory` はその種別の割り当てを [0, size) だけにする。OPNA の ADPCM_B は
  今までどおり写し (チップの書き込みは写しに入る)、ほかは参照。YMEngine は参照して
  書き込みを捨てるが、仕様は複製してよいとしているので、今までの FmGenEngine の
  動作を残した。外から見える変化: 写しは [0, size) だけになり、size より後ろへの
  転送は捨てる (今までは 256KB の内部バッファに入っていた)
- `GetMemorySize` は割り当てたブロックの大きさの合計。外から見える変化: OPNA の
  ADPCM_A が 0 ではなく渡した大きさを返す
- ROM モード中のレジスタ経由の転送は止めない (ROM モード側のメモリに書く)。実機で
  止まるかはチップの動作の問題で、YMEngine も止めていない

#### 仕様から導いたこと (相談せずに決めた)

- OPNB/OPNBB の ADPCM-B の番地の範囲を 16MB (開始・終了番地のレジスタで表せる範囲)
  にした。今までは `SetMemory` の大きさを2の冪に切り上げたマスクで折り返し、大きさが
  2の冪でないと、その間はバッファの外を読んでいた。割り当ての無い番地は 0 なので、
  大きさで折り返す理由が無くなった
- OPNA の番地は fmgen のまま 256KB で折り返す。ROM モードも同じにした。実機の ROM の
  番地の幅は確かめていない (**推測**: Y8950 のマニュアルの「最大 256KB」と同じ扱い)
  (→ 同日、利用者が示した YM2608 の資料で裏付けられた。上の節)

#### 実装

- fmgen (`opna.h` / `opna.cpp`、`[FmGenEngine]` の印): 読み書きの差し替え口
  `ADPCMMemory` と `OPNABase::SetADPCMMemory`。ADPCM-B は `ReadADPCMBMem` /
  `WriteADPCMBMem`、ADPCM-A は `ReadADPCMAMem` を通す。口が無ければ今までどおり
  `adpcmbuf` / `adpcmabuf`。ROM モードの分岐。x8 の書き込みの8行の展開はループに
  した (同じ動作)。README の「fmgen ソースへの変更点」の 6
- `FmGenChip.h`: `FmGenAccessClass` を `ChipMemoryType` / `ChipMemoryAccess` (番号は
  C API と同じ。`FmGenEngine.cpp` の `static_assert` で照合) に置き換えた。
  `fmgen_detail::AdpcmMemoryMap` が割り当てを持ち、fmgen の口を実装する。
  `OpnFamilyChip` は OPNA/OPNB/OPNBB でこれを fmgen に渡す。OPNB/OPNBB は
  `SetMemory` でチップを作り直さなくなった (今までは `Init` を呼び直して Reset して
  いた)
- `FmEngine.h` / `FmGenEngine.cpp` / `FmGenEngine.h` / `.def`: `mapMemory` と
  `FmEngine_SetMemoryEx`。検査の形は YMEngine の `FmEngine.h` と同じ
- 仕様の RAM のブロックの約束 (1〜3) は、レジスタ書き込みが `Generate` の最初に
  キューから適用されることで満たす (コードで確認: `FmEngine::generate`)
- 「fmgen は無改造・オリジナルのまま」という古い注記 (`FmGenChip.h`、
  `FmGenExtChip.h`、`CMakeLists.txt`) を実態に合わせて直した
- テスト: `opnaWriteRam` を `opnaTransfer` (開始番地と control2 を指定) に、
  `render` の中身を `renderWith` (割り当てを呼び出し側で行う) に分けた

#### 確認

**確認済み** — `api_test` が全件通る (MSVC 19.51)。

**確認済み** — 直す前のコード (`3ebb4aa`) で新しいテストを走らせた。`SetMemoryEx` が
無いので、base 0 の ADPCM_A/B だけを `SetMemory` に回すスタブを写しに足した。落ちた
項目のうち、スタブの制約によらないもの: OPNA の ADPCM_A の大きさ (0 を返す)、ROM
モードが FM_MEM_ADPCM_B を読むこと、RAM のブロックに転送が入らないこと (写しに書く)、
何も割り当てない OPNA で転送が鳴ること、OPNB がブロックの後ろを読むこと。最後の項目
(何も割り当てない OPNB の ADPCM-A) で null を読んでプロセスが落ちた (終了コード 139)。

**確認済み** — 今のコードの写しに改変を入れ、テストが落ちることを見た:

- ROM モードの分岐を通らない → ROM の番地順の再生、ROM モードの転送
- 選択ビットを見ない → ROM モードの振り分けの2項目と、上の2項目
- RAM のブロックを写して使う → store の4項目
- 差し替え口の読み出しを壊す → default の OPNA (x1/x8)・OPNB・OPNBB ほか
- 差し替え口の x8 の読み出しだけを 0 にする → default の OPNA x8 ほか (default の
  x8 のケースで ADPCM が実際に鳴っていることの裏付け)
- ROM のブロックにも書く → ROM のブロックに転送が入らないこと

**確認済み** — 直す前 (`3ebb4aa`) と後の出力の比較。`SetMemory` だけを使う同じ
プログラムを両方のソースでビルドし、51,700 サンプル (480 と 37 の呼び出しを交互) の
出力のハッシュを比べた。OPN、OPN2、SSG、OPNA (FM+SSG+ADPCM-B x1、小さいデータ、x1 と
x8 の転送と再生)、OPNB (2の冪でない大きさを含む)、OPNBB の10件は一致。意図して変えた
3件 (`SetMemory` の範囲より後ろへの転送、何も割り当てない転送、ROM モードの再生) は
食い違った。比較用のプログラムはリポジトリに残していない。

**確認済み** — CMake (VS 18 2026、Release) でビルドが通り、LNK の警告が出ない。
dumpbin で、DLL が `FmEngine_SetMemoryEx` を含む 18 関数をエクスポートし、
インポートライブラリの参照先が `FmGenEngine.dll` であることを見た。

試験していないこと:

- RAM のブロックを別スレッドから書き換えながら鳴らすこと。約束の 2 (Write の前に
  書いた値が見える) は、キューの atomic の release/acquire による順序に頼っている
  (**未検証**: 負荷を掛けた試験はしていない)
- CMake の Debug 構成、MSVC 以外のコンパイラ

#### 気づいたが手を付けていないこと

1. **x8 の並びが YMEngine と違う**。FmGenEngine は fmgen の再現で 8 面にビットを
   振り分け、YMEngine は番地順。同じ RAM のイメージを両方の x8 で鳴らすと食い違う。
   仕様はバイトの並びをアプリとエンジンの取り決めにしているので、README に並びを
   書くにとどめた (→ 同日、x8 も番地順にして YMEngine とそろえた。上の節)
2. **`FmEngine_Write` がキューの満杯を報告しない**。`FmEngine::write` は
   `SpscQueue::push` の失敗を無視する (コードで確認)。容量は 4095 件で、`Generate`
   を挟まずにそれ以上書くと、溢れた分は黙って捨てられる。ADPCM のデータを
   レジスタ経由で転送すると起きやすい。YMEngine も同じかは見ていない
3. **OPNA の `granuality` は control2 (port1 の 0x01) を書くまで初期化されない**。
   `OPNABase::Reset` の `SetReg` は非 virtual で、`SetADPCMBReg` に届かない (コードで
   確認)。下の古い節の「気づいたが手を付けていないこと」の 1 (未初期化のメンバ) の
   一つ。control2 を書かずに再生しても L/R のビットが 0 なので音には出ない
   (**推測**: コードから。試していない) (→ 同日、x8 を番地順にしたときに転送の番地に
   効くようになったので直した。上の節)

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
した)。(→ 同日、`SetMemoryEx` とあわせて対応した。上の節)

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
