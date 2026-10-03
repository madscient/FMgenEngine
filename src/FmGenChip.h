#pragma once
// FmGenChip.h
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (このラッパーコード自体は MIT License。内部で利用する fmgen 本体
//  (extern/fmgen/) は cisc 氏による別ライセンス。
//  詳細はリポジトリルートの LICENSE / README.md を参照)
//
// fmgen (cisc, 1998-2003) のラッパー。
// YMEngine (ymfm版) の FmChip 抽象インターフェースに合わせ、上位層
// (FmEngine.h / FmGenEngine.cpp) を YMEngine と同じ構造で書けるようにする。
//
// 対応チップ:
//   OPN   (YM2203) ... FM::OPN
//   OPNA  (YM2608) ... FM::OPNA  (ADPCM-B / リズム音源 内蔵)
//   OPNB  (YM2610) ... FM::OPNB  (ADPCM-A / ADPCM-B 内蔵)
//   OPNBB (YM2610B)... FM::OPNBB (OPNB 派生、FM 6ch 全有効 / FmGenEngine 追加)
//   OPN2  (YM2612) ... FM::OPN2  (FM 6ch + DAC / FmGenEngine 追加)
//   OPM   (YM2151) ... FM::OPM
//   SSG   (YM2149) ... ::PSG     (外部ライブラリチップ扱い。ExtChip.h 参照)
//
// fmgen の Mix() は「加算合成」である点に注意:
//   呼び出し前にバッファをゼロクリアしておく必要がある
//   (FM_SAMPLETYPE が int32 の場合 StoreSample はクリップなしの単純加算)。
//   本ラッパーは毎回ゼロクリアした int32 ワークバッファに Mix() させてから
//   float に変換することでこれを吸収する。
//
// レート変換:
//   fmgen は Init/SetRate に直接出力レートを渡せるため、ymfm 版のような
//   外付け LinearResampler は不要。ただし OPNA::SetRate はサンプリングレート
//   変更時にリズムサンプルの step を再計算するだけで FM 部分は
//   RebuildTimeTable() で追従する。
//   nativeRate() は実機の FM 部のレートを API 仕様のために返すだけで、
//   生成には使わない。
//
// ポート (OPNA/OPNB のレジスタ拡張面):
//   fmgen は SetReg(addr, data) のアドレス空間上で port1 を addr+0x100 として
//   表現する (実機の YM2608/YM2610 と同じ)。
//   YMEngine の write(port, reg, value) は port!=0 のとき addr に 0x100 を
//   加算することで対応させる。
//   OPN/OPM はポート概念を持たないため port は無視する。
//
// 依存: fmgen 0.08 (cisc) — extern/fmgen 以下 (改変箇所には [FmGenEngine] の印)
//       C++17 以上

// fmgen の各ヘッダ (fmgen.h/opna.h/opm.h/psg.h) は uint/uint8/int32 等の
// 型を前提とするが、それらを定義する types.h を自身では include しない
// (fmgen.h 内で `//#include "types.h"` とコメントアウトされている)。
// オリジナルの Visual Studio プロジェクトでは各 .cpp が headers.h
// (windows.h 等を include) を経由して間接的に解決していたと見られるが、
// ここでは types.h を明示的に先に include することで対応する。
// このために fmgen 本体 (.h/.cpp) には手を入れていない。
#include "fmgen/types.h"
#include "fmgen/fmgen.h"
#include "fmgen/opna.h"
#include "fmgen/opna_ext.h"   // OPN2 / OPNBB (FmGenEngine 追加実装)
#include "fmgen/opm.h"
#include "fmgen/psg.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   // GetModuleHandleEx, GetModuleFileName
#endif

// =========================================================
//  DLL 自身のディレクトリを返すユーティリティ (Windows 専用)
//  OPNA リズム WAV の自動ロードに使用する。
//  戻り値: '\' または '/' で終わるディレクトリパス文字列。
//          取得に失敗した場合は空文字列 ("") を返し、
//          fmgen は カレントディレクトリを検索する。
// =========================================================
namespace fmgen_detail {
    inline std::string getDllDir() {
#ifdef _WIN32
        HMODULE hm = nullptr;
        // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS:
        //   コード中のアドレス (本関数自体) からモジュールハンドルを取得する。
        //   これにより、どのプロセスからロードされても常に
        //   FmGenEngine.dll 自身のパスが得られる。
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&getDllDir),
                &hm) && hm) {
            char buf[MAX_PATH] = {};
            DWORD len = GetModuleFileNameA(hm, buf, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                std::string path(buf, len);
                const auto pos = path.find_last_of("\\/");
                if (pos != std::string::npos)
                    return path.substr(0, pos + 1);  // 末尾の区切り文字を含む
            }
        }
#endif
        return "";  // カレントディレクトリへのフォールバック (fmgen の動作)
    }
}

// =========================================================
//  チップ種別列挙
//  FmEngineApi.h では文字列で指定されるが、内部でこの列挙に変換して管理する。
// =========================================================
enum class FmGenChipType {
    OPN,    // YM2203
    OPNA,   // YM2608
    OPNB,   // YM2610
    OPNBB,  // YM2610B (FmGenEngine 追加実装)
    OPN2,   // YM2612  (FmGenEngine 追加実装)
    OPM,    // YM2151
};


// =========================================================
//  チップから見えるメモリと、そこにつないだデバイスの種類
//  C API ではメモリを名前の文字列で指定する。どのチップがどの名前のメモリを
//  持つかは、チップごとの表 (FmGenChip::memories) にある。
//  ChipMemoryAccess の番号は FmEngineApi.h の FmMemoryAccess と同じ
//  (FmGenEngine.cpp の static_assert で照合する)。
// =========================================================
enum class ChipMemoryType : uint32_t {
    ADPCM_A,          // OPNB/OPNBB: ADPCM-A
    ADPCM_B,          // OPNB/OPNBB: ADPCM-B / OPNA: RAM モードのメモリ
    ADPCM_B_ROMMODE,  // OPNA: ROM モードのメモリ
};
constexpr uint32_t kChipMemoryTypeCount = 3;

enum class ChipMemoryAccess : uint32_t {
    ROM = 0,  // チップからの書き込みは捨てる
    RAM = 1,  // ブロックをその場で読み書きする
};

// =========================================================
//  出力の部位
//  C API では部位を名前の文字列で指定する。どのチップがどの名前の部位を持つかは、
//  チップごとの表 (FmGenChip::parts) にある。
// =========================================================
enum class ChipPart : uint32_t {
    OPN_FM  = 0,  // OPN/OPNA/OPNB/OPNBB: FM (ADPCM・リズムを含む)
    OPN_SSG = 1,  //                      SSG
};
constexpr uint32_t kChipPartCount = 2;

// =========================================================
//  名前の表
//  部位と外部メモリの名前 (大文字小文字を区別する) と、内部の番号の対応。
//  名前は FmEngineApi の仕様書の表のとおりにする。文字列はリテラルを指すので、
//  エンジンを破棄したあとも有効。
// =========================================================
template<typename T>
struct Named {
    const char* name;
    T           value;
};

template<typename T>
struct NamedList {
    const Named<T>* items = nullptr;
    uint32_t        count = 0;

    // 範囲外は nullptr
    const char* name(uint32_t index) const {
        return index < count ? items[index].name : nullptr;
    }

    // name が nullptr か、表に無ければ false
    bool find(const char* name, T& out) const {
        if (!name) return false;
        for (uint32_t i = 0; i < count; ++i) {
            if (std::strcmp(items[i].name, name) == 0) {
                out = items[i].value;
                return true;
            }
        }
        return false;
    }

    bool contains(T value) const {
        for (uint32_t i = 0; i < count; ++i)
            if (items[i].value == value) return true;
        return false;
    }
};

// 部位ごとのゲイン。チップ全体のゲインは FmEngine が後から掛ける。
struct PartGains {
    float l[kChipPartCount];
    float r[kChipPartCount];
};

// =========================================================
//  FM 部のネイティブサンプルレート
//  レート = クロック / fmDivider()。prescale の番号 p は fmgen の
//  OPNBase::SetPrescaler と同じく 0x2D→0, 0x2E→1, 0x2F→2。
//  fmgen は OPNA/OPNB にクロックの半分を渡す (OPNABase::SetRate)。FmGenEngine の
//  OPN2 も同じく半分を渡すので、分周比は OPN の2倍になる。
// =========================================================
namespace fmgen_detail {
    constexpr uint32_t fmDivider(FmGenChipType type, uint32_t p) {
        constexpr uint32_t kOpn[3] = { 72, 36, 24 };
        switch (type) {
            case FmGenChipType::OPN:   return kOpn[p];
            case FmGenChipType::OPNA:  return kOpn[p] * 2;
            case FmGenChipType::OPNB:
            case FmGenChipType::OPNBB:
            case FmGenChipType::OPN2:  return kOpn[0] * 2;  // 0x2D-0x2F を無視する (1/6 固定)
            case FmGenChipType::OPM:   return 64;
        }
        return 1;
    }

    // fmgen が prescale の書き込みを受け付けるのは、OPN/OPNA の port0 だけ。
    // 書き込みはオーディオスレッドで適用され、rate() は任意スレッドから読まれる。
    class FmRate {
    public:
        FmRate(FmGenChipType type, uint32_t clock)
            : m_type(type), m_clock(clock), m_rate(clock / fmDivider(type, 0)) {}

        void onWrite(uint32_t port, uint8_t reg) {
            const bool prescalable = m_type == FmGenChipType::OPN
                                  || m_type == FmGenChipType::OPNA;
            if (prescalable && port == 0 && reg >= 0x2d && reg <= 0x2f)
                m_rate.store(m_clock / fmDivider(m_type, reg - 0x2d),
                             std::memory_order_relaxed);
        }

        uint32_t rate() const { return m_rate.load(std::memory_order_relaxed); }

    private:
        FmGenChipType         m_type;
        uint32_t              m_clock;
        std::atomic<uint32_t> m_rate;
    };
}

// =========================================================
//  ADPCM の外部メモリ
//  ChipMemoryType ごとに、ブロックを番地の範囲に割り当てる。fmgen の ADPCM の
//  メモリアクセスはすべてここを通る (OPNABase::SetADPCMMemory)。
//  割り当ての無い番地を読むと 0、書き込みは捨てる。
//  割り当ての変更はスレッドセーフではない (生成を始める前に済ませること)。
// =========================================================
namespace fmgen_detail {
    class AdpcmMemoryMap final : public FM::ADPCMMemory {
    public:
        uint8 Read(Space space, uint addr) override {
            if (const Block* b = find(space, addr)) return b->read[addr - b->base];
            return 0;
        }

        void Write(Space space, uint addr, uint8 data) override {
            if (const Block* b = find(space, addr))
                if (b->write) b->write[addr - b->base] = data;
        }

        // [base, base + size) に data を割り当てる。RAM ならチップの書き込みを data に
        // 入れる。size が 0、範囲が 2^32 を越える、既存の割り当てと重なるなら false
        bool map(ChipMemoryType type, uint32_t base, uint8_t* data, uint32_t size,
                 ChipMemoryAccess access) {
            auto* s = space(type);
            if (!s || !data || !validRange(base, size)) return false;
            for (const Block& b : *s)
                if (overlaps(b, base, size)) return false;
            s->push_back({ base, size, data,
                           access == ChipMemoryAccess::RAM ? data : nullptr, nullptr });
            return true;
        }

        // [base, base + size) と重なる割り当てをすべて外す。size が 0、範囲が
        // 2^32 を越えるなら false
        bool unmap(ChipMemoryType type, uint32_t base, uint32_t size) {
            auto* s = space(type);
            if (!s || !validRange(base, size)) return false;
            s->erase(std::remove_if(s->begin(), s->end(),
                                    [&](const Block& b) { return overlaps(b, base, size); }),
                     s->end());
            return true;
        }

        // type の割り当てを [0, size) の data だけにする。copy なら data を写した
        // バッファを割り当て、チップの書き込みはそこに入る。copy でなければ data を
        // 参照し、チップの書き込みは捨てる
        void set(ChipMemoryType type, const uint8_t* data, uint32_t size, bool copy) {
            auto* s = space(type);
            if (!s) return;
            Block b{ 0, size, data, nullptr, nullptr };
            if (copy) {
                b.owned.reset(new uint8_t[size]);
                std::memcpy(b.owned.get(), data, size);
                b.read = b.write = b.owned.get();
            }
            std::vector<Block> next;
            next.push_back(std::move(b));
            s->swap(next);
        }

    private:
        struct Block {
            uint32_t                   base;
            uint32_t                   size;
            const uint8_t*             read;
            uint8_t*                   write;  // ROM なら nullptr
            std::unique_ptr<uint8_t[]> owned;  // set() で写したもの
        };

        static bool validRange(uint32_t base, uint32_t size) {
            return size > 0 && uint64_t{base} + size <= (uint64_t{1} << 32);
        }
        static bool overlaps(const Block& b, uint32_t base, uint32_t size) {
            return uint64_t{base} < uint64_t{b.base} + b.size &&
                   uint64_t{b.base} < uint64_t{base} + size;
        }

        std::vector<Block>* space(ChipMemoryType type) {
            const auto i = static_cast<uint32_t>(type);
            return i < kChipMemoryTypeCount ? &m_spaces[i] : nullptr;
        }
        const std::vector<Block>* space(ChipMemoryType type) const {
            const auto i = static_cast<uint32_t>(type);
            return i < kChipMemoryTypeCount ? &m_spaces[i] : nullptr;
        }

        const Block* find(Space s, uint addr) const {
            ChipMemoryType type;
            switch (s) {
                case spaceA:    type = ChipMemoryType::ADPCM_A;         break;
                case spaceB:    type = ChipMemoryType::ADPCM_B;         break;
                case spaceBROM: type = ChipMemoryType::ADPCM_B_ROMMODE; break;
                default:        return nullptr;
            }
            for (const Block& b : *space(type))
                if (addr >= b.base && addr - b.base < b.size) return &b;
            return nullptr;
        }

        std::array<std::vector<Block>, kChipMemoryTypeCount> m_spaces;
    };
}

// =========================================================
//  FmGenChip インターフェース
//  YMEngine src/FmChip.h の FmChip クラスに合わせている。
// =========================================================
class FmGenChip {
public:
    virtual ~FmGenChip() = default;
    virtual void        write(uint32_t port, uint8_t reg, uint8_t value) = 0;
    // 部位を持たないチップは gains を見ない
    virtual void        generate(float* out_l, float* out_r, uint32_t samples,
                                 const PartGains& gains) = 0;
    virtual void        setTargetRate(uint32_t target_rate) = 0;
    // FM 部のネイティブサンプルレート (Hz、端数切り捨て)
    virtual uint32_t    nativeRate() const = 0;
    virtual FmGenChipType type() const = 0;
    virtual const char* name()  const = 0;
    virtual uint32_t    clock() const = 0;

    // チップが持つ部位と外部メモリ。持たないチップは空の表を返す。
    // 同じチップには、いつも同じ順序で同じ名前を返すこと
    virtual NamedList<ChipPart>       parts()    const { return {}; }
    virtual NamedList<ChipMemoryType> memories() const { return {}; }

    // 外部メモリ。挙動は fmgen_detail::AdpcmMemoryMap の map / unmap / set を参照。
    // memories() に無い種別では、mapMemory / unmapMemory は false を返し、
    // setMemory は何もしない
    virtual bool        mapMemory(ChipMemoryType /*type*/, uint32_t /*base*/, uint8_t* /*data*/,
                                  uint32_t /*size*/, ChipMemoryAccess /*access*/) { return false; }
    virtual bool        unmapMemory(ChipMemoryType /*type*/, uint32_t /*base*/,
                                    uint32_t /*size*/) { return false; }
    virtual void        setMemory(ChipMemoryType /*type*/,
                                  const uint8_t* /*data*/, uint32_t /*size*/) {}

    // OPNA のリズムサンプル (2608_BD.WAV 等) を読み込む。
    // OPNA 以外では何もしない。
    // dir_path: WAV ファイルが置かれているディレクトリ ('\' or '/' 終端)
    virtual bool        loadRhythmSamples(const char* /*dir_path*/) { return true; }
};

// =========================================================
//  int32 ワークバッファ → float 変換共通ヘルパー
//  fmgen の Sample 型は FM_SAMPLETYPE (= int32) を前提とする。
//  実測スケールはおおむね 16bit PCM 相当 (±32768 程度) のため、
//  ymfm 版 FmChip.h と同じ 1/32768 スケールで float に変換し、
//  チップ間の音量感を揃える。
// =========================================================
namespace fmgen_detail {
    constexpr float kScale = 1.0f / 32768.0f;

    inline void mixBufferToFloat(const std::vector<FM::Sample>& buf,
                                  float* out_l, float* out_r, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            out_l[i] = static_cast<float>(buf[i * 2 + 0]) * kScale;
            out_r[i] = static_cast<float>(buf[i * 2 + 1]) * kScale;
        }
    }

    // ゲインが 1.0 なら、FM と SSG を1本のバッファに足してから変換したときと
    // ビット単位で同じ値になる。2^24 未満の整数に 2 の冪を掛けた値は float で
    // 正確に表せるので、それぞれの積も、その和も丸められない。
    inline void mixSplitToFloat(const std::vector<FM::Sample>& fm,
                                 const std::vector<FM::Sample>& ssg,
                                 const PartGains& g,
                                 float* out_l, float* out_r, uint32_t n) {
        constexpr size_t kFm  = static_cast<size_t>(ChipPart::OPN_FM);
        constexpr size_t kSsg = static_cast<size_t>(ChipPart::OPN_SSG);
        const float fm_l  = g.l[kFm]  * kScale, fm_r  = g.r[kFm]  * kScale;
        const float ssg_l = g.l[kSsg] * kScale, ssg_r = g.r[kSsg] * kScale;
        for (uint32_t i = 0; i < n; ++i) {
            out_l[i] = static_cast<float>(fm[i * 2 + 0]) * fm_l
                     + static_cast<float>(ssg[i * 2 + 0]) * ssg_l;
            out_r[i] = static_cast<float>(fm[i * 2 + 1]) * fm_r
                     + static_cast<float>(ssg[i * 2 + 1]) * ssg_r;
        }
    }
}

// =========================================================
//  OpnFamilyChip<ChipImpl, TType>
//  OPN / OPNA / OPNB に共通のテンプレート実装。
//  3クラスとも Init(clock, rate, bool, ...) / SetReg / Mix の
//  シグネチャがほぼ共通だが、Init の追加引数と「ポートを持つか」が
//  異なるため、各特殊化ポイントを policy として外出しする。
// =========================================================
template<typename ChipImpl, FmGenChipType TType>
class OpnFamilyChip final : public FmGenChip {
    static constexpr bool kAdpcm = TType != FmGenChipType::OPN;

public:
    explicit OpnFamilyChip(uint32_t clock, uint32_t target_rate)
        : m_clock(clock), m_fm_rate(TType, clock)
    {
        if (!initImpl(target_rate))
            throw std::runtime_error(std::string("fmgen: Init failed for ") + name());
        if constexpr (kAdpcm) m_chip.SetADPCMMemory(&m_mem);
    }

    // m_chip が m_mem を指すので写せない
    OpnFamilyChip(const OpnFamilyChip&) = delete;
    OpnFamilyChip& operator=(const OpnFamilyChip&) = delete;

    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint32_t addr = (port != 0) ? (static_cast<uint32_t>(reg) + 0x100u)
                                           : static_cast<uint32_t>(reg);
        m_chip.SetReg(addr, value);
        m_fm_rate.onWrite(port, reg);
    }

    void generate(float* out_l, float* out_r, uint32_t samples,
                  const PartGains& gains) override {
        if (samples == 0) return;
        const size_t n = static_cast<size_t>(samples) * 2;
        m_work.assign(n, 0);     // fmgen の Mix 系は加算合成
        m_work_ssg.assign(n, 0);
        m_chip.MixSplit(m_work.data(), m_work_ssg.data(), static_cast<int>(samples));
        fmgen_detail::mixSplitToFloat(m_work, m_work_ssg, gains, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate, false);
    }

    NamedList<ChipPart> parts() const override {
        static constexpr Named<ChipPart> kParts[] = {
            { "FM",  ChipPart::OPN_FM  },
            { "SSG", ChipPart::OPN_SSG },
        };
        return { kParts, 2 };
    }

    NamedList<ChipMemoryType> memories() const override {
        // OPNA のリズム音の内蔵 ROM ("RHYTHM") は持たない。fmgen の OPNA は
        // リズムを WAV ファイルから読み、ROM の内容を使わないため
        static constexpr Named<ChipMemoryType> kOpna[] = {
            { "ADPCM_B",         ChipMemoryType::ADPCM_B         },
            { "ADPCM_B_ROMMODE", ChipMemoryType::ADPCM_B_ROMMODE },
        };
        static constexpr Named<ChipMemoryType> kOpnb[] = {
            { "ADPCM_A", ChipMemoryType::ADPCM_A },
            { "ADPCM_B", ChipMemoryType::ADPCM_B },
        };
        switch (TType) {
            case FmGenChipType::OPNA:  return { kOpna, 2 };
            case FmGenChipType::OPNB:
            case FmGenChipType::OPNBB: return { kOpnb, 2 };
            default:                   return {};
        }
    }

    bool mapMemory(ChipMemoryType type, uint32_t base, uint8_t* data,
                   uint32_t size, ChipMemoryAccess access) override {
        return memories().contains(type) && m_mem.map(type, base, data, size, access);
    }

    bool unmapMemory(ChipMemoryType type, uint32_t base, uint32_t size) override {
        return memories().contains(type) && m_mem.unmap(type, base, size);
    }

    // OPNA の ADPCM-B (RAM モードのメモリ) はレジスタ経由で書き込まれる RAM だが、
    // data は書き込めるメモリとは限らないので写す。OPNA の ROM モードのメモリは
    // 参照し、チップの書き込みを捨てる (書き込ませるなら mapMemory で RAM を
    // 割り当てる)。OPNB/OPNBB はメモリに書き込まないので参照する
    void setMemory(ChipMemoryType type, const uint8_t* data, uint32_t size) override {
        if (memories().contains(type))
            m_mem.set(type, data, size,
                      TType == FmGenChipType::OPNA && type == ChipMemoryType::ADPCM_B);
    }

    bool loadRhythmSamples(const char* dir_path) override {
        return loadRhythmSamplesImpl(dir_path);
    }

    uint32_t      nativeRate() const override { return m_fm_rate.rate(); }
    FmGenChipType type()       const override { return TType; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override;

private:
    // ---- チップ種別ごとの特殊化ポイント ----
    bool initImpl(uint32_t target_rate);
    bool loadRhythmSamplesImpl(const char* dir_path);

    fmgen_detail::AdpcmMemoryMap m_mem;  // m_chip が指すので、m_chip より先に作り後に壊す
    ChipImpl              m_chip;
    uint32_t               m_clock;
    fmgen_detail::FmRate   m_fm_rate;
    std::vector<FM::Sample> m_work;      // FM (ADPCM・リズムを含む)
    std::vector<FM::Sample> m_work_ssg;
};

// ---------------------------------------------------------
//  name() 特殊化
// ---------------------------------------------------------
template<> inline const char* OpnFamilyChip<FM::OPN,  FmGenChipType::OPN >::name() const { return "OPN (YM2203) [fmgen]";  }
template<> inline const char* OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::name() const { return "OPNA (YM2608) [fmgen]"; }
template<> inline const char* OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::name() const { return "OPNB (YM2610) [fmgen]"; }

// ---------------------------------------------------------
//  OPN: Init(clock, rate, ipflag=false, path=nullptr)
//  ポート概念なし・ADPCM/リズムなし
// ---------------------------------------------------------
template<>
inline bool OpnFamilyChip<FM::OPN, FmGenChipType::OPN>::initImpl(uint32_t target_rate) {
    return m_chip.Init(m_clock, target_rate, false, nullptr);
}
template<>
inline bool OpnFamilyChip<FM::OPN, FmGenChipType::OPN>::loadRhythmSamplesImpl(
    const char*) { return true; }

// ---------------------------------------------------------
//  OPNA: Init(clock, rate, ipflag=false, rhythmpath=nullptr)
//  インスタンス作成時に DLL と同じフォルダの 2608_*.WAV を自動ロードする。
//  ファイルが存在しない場合はリズムチャンネルが無音になるだけで、
//  FM/SSG/ADPCM-B チャンネルへの影響はない。
// ---------------------------------------------------------
template<>
inline bool OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::initImpl(uint32_t target_rate) {
    if (!m_chip.Init(m_clock, target_rate, false, nullptr))
        return false;
    // DLL と同じフォルダを基準に 2608_BD.WAV 等を探してロードする。
    // getDllDir() が空文字列の場合は fmgen のデフォルト動作
    // (カレントディレクトリを検索) にフォールバックする。
    const std::string dir = fmgen_detail::getDllDir();
    m_chip.LoadRhythmSample(dir.empty() ? nullptr : dir.c_str());
    // LoadRhythmSample の失敗は無視する (ファイル不在は許容)
    return true;
}
template<>
inline bool OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::loadRhythmSamplesImpl(
    const char* dir_path) {
    // 再ロード用。通常は initImpl が自動ロードするため外部から呼ぶ必要はない。
    return m_chip.LoadRhythmSample(dir_path);
}

// ---------------------------------------------------------
//  OPNB: Init(clock, rate, ipflag=false,
//              adpcma, adpcma_size, adpcmb, adpcmb_size)
//  ADPCM-A/B のメモリは m_mem から読むので、バッファは渡さない。
//  fmgen は ADPCM-B の番地のマスクを adpcmb_size から決める (2 の冪に切り上げる)。
//  開始・終了番地のレジスタで表せる範囲 (16MB) 全体を番地にするため、その大きさを
//  渡す。
// ---------------------------------------------------------
template<>
inline bool OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::initImpl(uint32_t target_rate) {
    return m_chip.Init(m_clock, target_rate, false, nullptr, 0, nullptr, 1 << 24);
}
template<>
inline bool OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::loadRhythmSamplesImpl(
    const char*) { return true; }

using FmGenOpnChip  = OpnFamilyChip<FM::OPN,  FmGenChipType::OPN>;
using FmGenOpnaChip = OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>;
using FmGenOpnbChip = OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>;

// =========================================================
//  OPNBB (YM2610B) ラッパー
//  FM::OPNBB は FM::OPNB の派生クラスのため、OpnFamilyChip テンプレートを
//  そのまま使える。
// =========================================================

// name() 特殊化
template<> inline const char*
OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::name() const {
    return "OPNBB (YM2610B) [fmgen]";
}

// initImpl 特殊化 (OPNB と同じ)
template<>
inline bool OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::initImpl(uint32_t target_rate) {
    return m_chip.Init(m_clock, target_rate, false, nullptr, 0, nullptr, 1 << 24);
}

template<>
inline bool OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::loadRhythmSamplesImpl(
    const char*) { return true; }

// write: OPNB と同じポート変換 (port=1 → addr+0x100)
// OpnFamilyChip::write はテンプレートで実装済みのため再定義不要。

using FmGenOpnbbChip = OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>;

// =========================================================
//  OPN2 (YM2612) ラッパー
//  FM::OPN2 は OPNBase 継承の独自クラス (opna_ext.h で実装)。
//  OpnFamilyChip テンプレートは OPNB 系の ADPCM 管理メンバを持つため
//  OPN2 には合わないので独立クラスとして実装する。
// =========================================================
class FmGenOpn2Chip final : public FmGenChip {
public:
    explicit FmGenOpn2Chip(uint32_t clock, uint32_t target_rate)
        : m_clock(clock), m_fm_rate(FmGenChipType::OPN2, clock)
    {
        if (!m_chip.Init(m_clock, target_rate))
            throw std::runtime_error("fmgen: OPN2::Init failed");
    }

    // OPN2 もポート0/1 で CH1〜3 / CH4〜6 を分離する。
    // port=1 → addr+0x100 に変換して FM::OPN2::SetReg に渡す。
    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint32_t addr = (port != 0)
            ? (static_cast<uint32_t>(reg) + 0x100u)
            : static_cast<uint32_t>(reg);
        m_chip.SetReg(addr, value);
        m_fm_rate.onWrite(port, reg);
    }

    void generate(float* out_l, float* out_r, uint32_t samples,
                  const PartGains& /*gains*/) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0);
        m_chip.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate);
    }

    uint32_t      nativeRate() const override { return m_fm_rate.rate(); }
    FmGenChipType type()       const override { return FmGenChipType::OPN2; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override { return "OPN2 (YM2612) [fmgen]"; }

private:
    FM::OPN2                m_chip;
    uint32_t                 m_clock;
    fmgen_detail::FmRate     m_fm_rate;
    std::vector<FM::Sample>  m_work;
};

// =========================================================
//  FmGenOpmChip — OPM (YM2151) ラッパー
//  ポート概念なし・ADPCM/リズムなし
// =========================================================
class FmGenOpmChip final : public FmGenChip {
public:
    explicit FmGenOpmChip(uint32_t clock, uint32_t target_rate)
        : m_clock(clock)
    {
        if (!m_chip.Init(m_clock, target_rate, false))
            throw std::runtime_error("fmgen: OPM::Init failed");
    }

    void write(uint32_t /*port*/, uint8_t reg, uint8_t value) override {
        m_chip.SetReg(reg, value);
    }

    void generate(float* out_l, float* out_r, uint32_t samples,
                  const PartGains& /*gains*/) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0);
        m_chip.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate, false);
    }

    uint32_t      nativeRate() const override {
        return m_clock / fmgen_detail::fmDivider(FmGenChipType::OPM, 0);
    }
    FmGenChipType type()       const override { return FmGenChipType::OPM; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override { return "OPM (YM2151) [fmgen]"; }

private:
    FM::OPM                 m_chip;
    uint32_t                 m_clock;
    std::vector<FM::Sample>  m_work;
};

// =========================================================
//  ファクトリ関数
//  既定のクロックは持たない。clock=0 なら std::invalid_argument。
// =========================================================
inline std::unique_ptr<FmGenChip> createFmGenChip(
    FmGenChipType type, uint32_t clock, uint32_t target_rate) {
    if (clock == 0) throw std::invalid_argument("fmgen: clock must not be 0");
    switch (type) {
        case FmGenChipType::OPN:   return std::make_unique<FmGenOpnChip>(clock, target_rate);
        case FmGenChipType::OPNA:  return std::make_unique<FmGenOpnaChip>(clock, target_rate);
        case FmGenChipType::OPNB:  return std::make_unique<FmGenOpnbChip>(clock, target_rate);
        case FmGenChipType::OPNBB: return std::make_unique<FmGenOpnbbChip>(clock, target_rate);
        case FmGenChipType::OPN2:  return std::make_unique<FmGenOpn2Chip>(clock, target_rate);
        case FmGenChipType::OPM:   return std::make_unique<FmGenOpmChip>(clock, target_rate);
    }
    return nullptr;
}
