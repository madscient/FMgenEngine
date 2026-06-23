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
// YMEngine (ymfm版) の FmChip 抽象インターフェースと完全互換のシグネチャを
// 提供し、上位層 (FmEngine.h / FmEngineApi.cpp) をそのまま流用できるようにする。
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
//
// ポート (OPNA/OPNB のレジスタ拡張面):
//   fmgen は SetReg(addr, data) のアドレス空間上で port1 を addr+0x100 として
//   表現する (実機の YM2608/YM2610 と同じ)。
//   YMEngine の write(port, reg, value) は port!=0 のとき addr に 0x100 を
//   加算することで対応させる。
//   OPN/OPM はポート概念を持たないため port は無視する。
//
// 依存: fmgen 0.08 (cisc) — extern/fmgen 以下にオリジナルのまま配置
//       C++17 以上

// fmgen の各ヘッダ (fmgen.h/opna.h/opm.h/psg.h) は uint/uint8/int32 等の
// 型を前提とするが、それらを定義する types.h を自身では include しない
// (fmgen.h 内で `//#include "types.h"` とコメントアウトされている)。
// オリジナルの Visual Studio プロジェクトでは各 .cpp が headers.h
// (windows.h 等を include) を経由して間接的に解決していたと見られるが、
// ここでは types.h を明示的に先に include することで対応する。
// fmgen 本体 (.h/.cpp) のロジックには一切手を入れていない。
#include "fmgen/types.h"
#include "fmgen/fmgen.h"
#include "fmgen/opna.h"
#include "fmgen/opna_ext.h"   // OPN2 / OPNBB (FmGenEngine 追加実装)
#include "fmgen/opm.h"
#include "fmgen/psg.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

// =========================================================
//  チップ種別列挙
//  FmGenEngineApi.h では文字列で指定されるが、内部でこの列挙に変換して管理する。
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
//  標準クロック定数 (実機準拠。YMEngine/FmChip.h の FmClock と同値)
// =========================================================
namespace FmGenClock {
    constexpr uint32_t OPN   = 3'993'600;
    constexpr uint32_t OPNA  = 7'987'200;
    constexpr uint32_t OPNB  = 8'000'000;
    constexpr uint32_t OPNBB = 8'000'000;  // YM2610B (OPNB と同クロック)
    constexpr uint32_t OPN2  = 7'670'453;  // YM2612 (Mega Drive 標準)
    constexpr uint32_t OPM   = 3'579'545;
    constexpr uint32_t SSG   = 3'579'545;
}

// =========================================================
//  外部メモリアクセス種別 (FmGenEngineApi.h の FmMemoryType と対応)
// =========================================================
enum class FmGenAccessClass {
    IO      = 0,
    ADPCM_A = 1,   // OPNB の ADPCM-A ROM
    ADPCM_B = 2,   // OPNA/OPNB の ADPCM-B RAM/ROM
    PCM     = 3,   // (fmgen では未使用)
};

// =========================================================
//  FmGenChip インターフェース
//  YMEngine src/FmChip.h の FmChip クラスとシグネチャを完全一致させる。
// =========================================================
class FmGenChip {
public:
    virtual ~FmGenChip() = default;
    virtual void        write(uint32_t port, uint8_t reg, uint8_t value) = 0;
    virtual void        generate(float* out_l, float* out_r, uint32_t samples) = 0;
    virtual void        setTargetRate(uint32_t target_rate) = 0;
    virtual uint32_t    nativeRate() const = 0;
    virtual FmGenChipType type() const = 0;
    virtual const char* name()  const = 0;
    virtual uint32_t    clock() const = 0;

    // 外部メモリ設定 (OPNA: ADPCM_B のみ / OPNB: ADPCM_A, ADPCM_B)
    // data の寿命は呼び出し元 (FmEngineApi 経由の利用者) が管理する。
    virtual void        setMemory(FmGenAccessClass /*access_type*/,
                                  const uint8_t* /*data*/, uint32_t /*size*/) {}
    virtual uint32_t    memorySize(FmGenAccessClass /*access_type*/) const { return 0; }

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
public:
    explicit OpnFamilyChip(uint32_t clock, uint32_t target_rate)
        : m_clock(clock)
    {
        if (!initImpl(target_rate))
            throw std::runtime_error(std::string("fmgen: Init failed for ") + name());
        m_native_rate = target_rate;
    }

    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint32_t addr = (port != 0) ? (static_cast<uint32_t>(reg) + 0x100u)
                                           : static_cast<uint32_t>(reg);
        m_chip.SetReg(addr, value);
    }

    void generate(float* out_l, float* out_r, uint32_t samples) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0); // fmgen Mix() は加算合成
        m_chip.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate, false);
        m_native_rate = target_rate;
    }

    void setMemory(FmGenAccessClass access_type,
                   const uint8_t* data, uint32_t size) override {
        setMemoryImpl(access_type, data, size);
    }

    uint32_t memorySize(FmGenAccessClass access_type) const override {
        return memorySizeImpl(access_type);
    }

    bool loadRhythmSamples(const char* dir_path) override {
        return loadRhythmSamplesImpl(dir_path);
    }

    uint32_t      nativeRate() const override { return m_native_rate; }
    FmGenChipType type()       const override { return TType; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override;

private:
    // ---- チップ種別ごとの特殊化ポイント ----
    bool initImpl(uint32_t target_rate);
    void setMemoryImpl(FmGenAccessClass access_type, const uint8_t* data, uint32_t size);
    uint32_t memorySizeImpl(FmGenAccessClass access_type) const;
    bool loadRhythmSamplesImpl(const char* dir_path);

    ChipImpl              m_chip;
    uint32_t               m_clock;
    uint32_t               m_native_rate = 0;
    std::vector<FM::Sample> m_work;

    // OPNB: setMemory で受け取ったポインタ/サイズを保持
    // (fmgen::OPNB::Init は ROM ポインタを直接保持するだけだが、
    //  Init 自体が一度しか呼べないため SetMemory のたびに作り直す)
    const uint8_t* m_adpcmAData = nullptr; uint32_t m_adpcmASize = 0;
    const uint8_t* m_adpcmBData = nullptr; uint32_t m_adpcmBSize = 0;
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
inline void OpnFamilyChip<FM::OPN, FmGenChipType::OPN>::setMemoryImpl(
    FmGenAccessClass, const uint8_t*, uint32_t) {}
template<>
inline uint32_t OpnFamilyChip<FM::OPN, FmGenChipType::OPN>::memorySizeImpl(
    FmGenAccessClass) const { return 0; }
template<>
inline bool OpnFamilyChip<FM::OPN, FmGenChipType::OPN>::loadRhythmSamplesImpl(
    const char*) { return true; }

// ---------------------------------------------------------
//  OPNA: Init(clock, rate, ipflag=false, rhythmpath=nullptr)
//  リズムサンプル(WAV)は Init 内部で読み込まれる。
//  rhythmpath が不正/未配置でも Init 自体は (ファイルが1つも無ければ)
//  続行されるよう、まず target rhythm dir なしで初期化し、後段で
//  明示的に LoadRhythmSample を呼べるようにする。
//  ADPCM-B は OPNA 内部に 256KB の RAM バッファを持つため
//  (LoadRhythmSample とは別物。OPNA::Init が自前で new[] する)、
//  setMemory(ADPCM_B, ...) はそのバッファへ memcpy する形で対応する。
// ---------------------------------------------------------
template<>
inline bool OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::initImpl(uint32_t target_rate) {
    // path=nullptr で初期化: リズムサンプルが見つからなくても
    // OPNA::LoadRhythmSample は false を返すだけで Init 自体は継続される
    // (fmgen 実装上 Init の戻り値は adpcmbuf の確保成否に依存し、
    //  リズムロード成否には依存しない)。
    return m_chip.Init(m_clock, target_rate, false, nullptr);
}
template<>
inline void OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::setMemoryImpl(
    FmGenAccessClass access_type, const uint8_t* data, uint32_t size) {
    if (access_type != FmGenAccessClass::ADPCM_B) return;
    // OPNA::GetADPCMBuffer() は内部 256KB(0x40000) バッファへの
    // ポインタを返す。外部 ROM/RAM 内容をそのバッファへコピーする。
    uint8_t* buf = m_chip.GetADPCMBuffer();
    if (!buf) return;
    const uint32_t copySize = std::min<uint32_t>(size, 0x40000u);
    std::memcpy(buf, data, copySize);
    if (copySize < 0x40000u)
        std::memset(buf + copySize, 0, 0x40000u - copySize);
    m_adpcmBData = data;
    m_adpcmBSize = size;
}
template<>
inline uint32_t OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::memorySizeImpl(
    FmGenAccessClass access_type) const {
    return (access_type == FmGenAccessClass::ADPCM_B) ? m_adpcmBSize : 0;
}
template<>
inline bool OpnFamilyChip<FM::OPNA, FmGenChipType::OPNA>::loadRhythmSamplesImpl(
    const char* dir_path) {
    return m_chip.LoadRhythmSample(dir_path);
}

// ---------------------------------------------------------
//  OPNB: Init(clock, rate, ipflag=false,
//              adpcma, adpcma_size, adpcmb, adpcmb_size)
//  ROM ポインタを直接 Init に渡す設計のため、setMemory が呼ばれるまでは
//  ヌル/サイズ0で初期化しておき、setMemory 時にチップを再構築する。
//  (fmgen は SetMemory に相当する後挿し API を持たないため)
// ---------------------------------------------------------
template<>
inline bool OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::initImpl(uint32_t target_rate) {
    return m_chip.Init(m_clock, target_rate, false, nullptr, 0, nullptr, 0);
}
template<>
inline void OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::setMemoryImpl(
    FmGenAccessClass access_type, const uint8_t* data, uint32_t size) {
    if (access_type == FmGenAccessClass::ADPCM_A) {
        m_adpcmAData = data; m_adpcmASize = size;
    } else if (access_type == FmGenAccessClass::ADPCM_B) {
        m_adpcmBData = data; m_adpcmBSize = size;
    } else {
        return;
    }
    // OPNB::Init はレジスタ・チャンネル状態を Reset() するため、
    // 既存の発音状態は失われる (内部で Reset() が呼ばれる)。
    // YMEngine の FmEngine_SetMemory と同じ運用制約
    // (ストリーム開始前、AddChip 直後に呼ぶこと) であれば問題にならない。
    m_chip.Init(m_clock, m_native_rate, false,
                const_cast<uint8_t*>(m_adpcmAData), static_cast<int>(m_adpcmASize),
                const_cast<uint8_t*>(m_adpcmBData), static_cast<int>(m_adpcmBSize));
}
template<>
inline uint32_t OpnFamilyChip<FM::OPNB, FmGenChipType::OPNB>::memorySizeImpl(
    FmGenAccessClass access_type) const {
    if (access_type == FmGenAccessClass::ADPCM_A) return m_adpcmASize;
    if (access_type == FmGenAccessClass::ADPCM_B) return m_adpcmBSize;
    return 0;
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
//  そのまま使える。ADPCM-A/B の setMemory も OPNB と同一の実装を流用する。
// =========================================================

// name() 特殊化
template<> inline const char*
OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::name() const {
    return "OPNBB (YM2610B) [fmgen]";
}

// initImpl 特殊化 (OPNB と同じ引数列)
template<>
inline bool OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::initImpl(uint32_t target_rate) {
    return m_chip.Init(m_clock, target_rate, false, nullptr, 0, nullptr, 0);
}

// setMemoryImpl / memorySizeImpl: OPNB と完全に同じ実装
template<>
inline void OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::setMemoryImpl(
    FmGenAccessClass access_type, const uint8_t* data, uint32_t size) {
    if (access_type == FmGenAccessClass::ADPCM_A) {
        m_adpcmAData = data; m_adpcmASize = size;
    } else if (access_type == FmGenAccessClass::ADPCM_B) {
        m_adpcmBData = data; m_adpcmBSize = size;
    } else {
        return;
    }
    m_chip.Init(m_clock, m_native_rate, false,
                const_cast<uint8_t*>(m_adpcmAData), static_cast<int>(m_adpcmASize),
                const_cast<uint8_t*>(m_adpcmBData), static_cast<int>(m_adpcmBSize));
}

template<>
inline uint32_t OpnFamilyChip<FM::OPNBB, FmGenChipType::OPNBB>::memorySizeImpl(
    FmGenAccessClass access_type) const {
    if (access_type == FmGenAccessClass::ADPCM_A) return m_adpcmASize;
    if (access_type == FmGenAccessClass::ADPCM_B) return m_adpcmBSize;
    return 0;
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
        : m_clock(clock)
    {
        if (!m_chip.Init(m_clock, target_rate))
            throw std::runtime_error("fmgen: OPN2::Init failed");
        m_native_rate = target_rate;
    }

    // OPN2 もポート0/1 で CH1〜3 / CH4〜6 を分離する。
    // port=1 → addr+0x100 に変換して FM::OPN2::SetReg に渡す。
    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint32_t addr = (port != 0)
            ? (static_cast<uint32_t>(reg) + 0x100u)
            : static_cast<uint32_t>(reg);
        m_chip.SetReg(addr, value);
    }

    void generate(float* out_l, float* out_r, uint32_t samples) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0);
        m_chip.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate);
        m_native_rate = target_rate;
    }

    uint32_t      nativeRate() const override { return m_native_rate; }
    FmGenChipType type()       const override { return FmGenChipType::OPN2; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override { return "OPN2 (YM2612) [fmgen]"; }

private:
    FM::OPN2                m_chip;
    uint32_t                 m_clock;
    uint32_t                 m_native_rate = 0;
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
        m_native_rate = target_rate;
    }

    void write(uint32_t /*port*/, uint8_t reg, uint8_t value) override {
        m_chip.SetReg(reg, value);
    }

    void generate(float* out_l, float* out_r, uint32_t samples) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0);
        m_chip.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_chip.SetRate(m_clock, target_rate, false);
        m_native_rate = target_rate;
    }

    uint32_t      nativeRate() const override { return m_native_rate; }
    FmGenChipType type()       const override { return FmGenChipType::OPM; }
    uint32_t      clock()      const override { return m_clock; }
    const char*   name()       const override { return "OPM (YM2151) [fmgen]"; }

private:
    FM::OPM                 m_chip;
    uint32_t                 m_clock;
    uint32_t                 m_native_rate = 0;
    std::vector<FM::Sample>  m_work;
};

// =========================================================
//  ファクトリ関数
// =========================================================
inline std::unique_ptr<FmGenChip> createFmGenChip(
    FmGenChipType type, uint32_t clock, uint32_t target_rate) {
    auto resolve = [](uint32_t c, uint32_t def) { return c ? c : def; };
    switch (type) {
        case FmGenChipType::OPN:
            return std::make_unique<FmGenOpnChip>(resolve(clock, FmGenClock::OPN), target_rate);
        case FmGenChipType::OPNA:
            return std::make_unique<FmGenOpnaChip>(resolve(clock, FmGenClock::OPNA), target_rate);
        case FmGenChipType::OPNB:
            return std::make_unique<FmGenOpnbChip>(resolve(clock, FmGenClock::OPNB), target_rate);
        case FmGenChipType::OPNBB:
            return std::make_unique<FmGenOpnbbChip>(resolve(clock, FmGenClock::OPNBB), target_rate);
        case FmGenChipType::OPN2:
            return std::make_unique<FmGenOpn2Chip>(resolve(clock, FmGenClock::OPN2), target_rate);
        case FmGenChipType::OPM:
            return std::make_unique<FmGenOpmChip>(resolve(clock, FmGenClock::OPM), target_rate);
    }
    return nullptr;
}
