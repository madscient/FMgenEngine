#pragma once
// FmGenExtChip.h
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (このラッパーコード自体は MIT License。内部で利用する fmgen 本体
//  (extern/fmgen/) は cisc 氏による別ライセンス。
//  詳細はリポジトリルートの LICENSE / README.md を参照)
//
// fmgen 同梱の PSG (YM2149 / AY-3-8910 互換) クラスのラッパー。
// YMEngine src/ExternalChip.h の ExtChip インターフェースと同じシグネチャを
// 提供する。
//
// fmgen の PSG::SetReg(regnum, data) はアドレスラッチ不要で直接レジスタに
// 書き込む方式 (emu2149 の PSG_writeReg と同じ流儀)。
//
// 依存: fmgen 0.08 (cisc) — extern/fmgen 以下 (改変箇所には [FmGenEngine] の印)

#include "fmgen/psg.h"
#include "FmGenChip.h"   // fmgen_detail::kScale, FM::Sample 互換のため

#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>

// =========================================================
//  FmGenExtChipType — 外部(非FM)チップ種別
// =========================================================
enum class FmGenExtChipType {
    SSG = 100,  // YM2149 (PSG) via fmgen ::PSG
};

// =========================================================
//  FmGenExtChip インターフェース
// =========================================================
class FmGenExtChip {
public:
    virtual ~FmGenExtChip() = default;
    virtual void           write(uint32_t port, uint8_t reg, uint8_t value) = 0;
    virtual void           generate(float* out_l, float* out_r, uint32_t samples) = 0;
    virtual void           setTargetRate(uint32_t target_rate) = 0;
    virtual uint32_t       nativeRate() const = 0;
    virtual FmGenExtChipType type()     const = 0;
    virtual const char*    name()       const = 0;
    virtual uint32_t       clock()      const = 0;
};

// =========================================================
//  FmGenPsgChip — ::PSG (YM2149) ラッパー
// =========================================================
class FmGenPsgChip final : public FmGenExtChip {
public:
    explicit FmGenPsgChip(uint32_t clock, uint32_t target_rate)
        : m_clock(clock)
    {
        m_psg.SetClock(psgClock(), static_cast<int>(target_rate));
        m_psg.Reset();
    }

    // PSG はアドレスラッチを持たない直接レジスタ書き込み方式。
    // port は無視する (emu2149 版 PSGChip と同じ振る舞い)。
    void write(uint32_t /*port*/, uint8_t reg, uint8_t value) override {
        m_psg.SetReg(reg, value);
    }

    void generate(float* out_l, float* out_r, uint32_t samples) override {
        if (samples == 0) return;
        m_work.assign(static_cast<size_t>(samples) * 2, 0); // Mix() は加算合成
        m_psg.Mix(m_work.data(), static_cast<int>(samples));
        fmgen_detail::mixBufferToFloat(m_work, out_l, out_r, samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_psg.SetClock(psgClock(), static_cast<int>(target_rate));
    }

    // FM 部を持たないので、トーンのカウンタが進むレートを返す
    // (トーン周波数 = このレート / (2 × TP) = clock / (32 × TP))。
    uint32_t          nativeRate() const override { return m_clock / 16; }
    FmGenExtChipType  type()       const override { return FmGenExtChipType::SSG; }
    uint32_t          clock()      const override { return m_clock; }
    const char*       name()       const override { return "SSG (YM2149) [fmgen]"; }

private:
    // fmgen の PSG は YM2203 の SSG 部として書かれていて、トーン周波数は
    // SetClock に渡した値 / (8 × TP) になる。AY-3-8910 の式は クロック / (16 × TP)
    // なので、AY 相当のクロックの半分を渡す約束になっている (OPN 系では fmgen が
    // 自分で分周して渡す)。単体の YM2149 は SEL を Low (クロック÷2) として扱い、
    // AY 相当のクロック clock/2 のさらに半分を渡す。clock=3,579,545Hz なら
    // AY 相当で 1,789,772Hz になる。エンベロープとノイズの周期も同じ値から決まる。
    int psgClock() const { return static_cast<int>(m_clock / 4); }

    PSG                      m_psg;
    uint32_t                  m_clock;
    std::vector<PSG::Sample>  m_work;
};

// =========================================================
//  ファクトリ関数
//  既定のクロックは持たない。clock=0 なら std::invalid_argument。
// =========================================================
inline std::unique_ptr<FmGenExtChip> createFmGenExtChip(
    FmGenExtChipType type, uint32_t clock, uint32_t target_rate) {
    if (clock == 0) throw std::invalid_argument("fmgen: clock must not be 0");
    switch (type) {
        case FmGenExtChipType::SSG:
            return std::make_unique<FmGenPsgChip>(clock, target_rate);
    }
    return nullptr;
}

// =========================================================
//  FmGenExtChipAdapter — FmGenExtChip を FmGenChip インターフェースに変換
//  FmGenEngine は FmGenChip* を一律保持するため、このアダプタ経由で
//  FmGenExtChip (SSG等) を追加する。
// =========================================================
class FmGenExtChipAdapter final : public FmGenChip {
public:
    explicit FmGenExtChipAdapter(std::unique_ptr<FmGenExtChip> chip)
        : m_chip(std::move(chip)) {}

    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        m_chip->write(port, reg, value);
    }
    void generate(float* out_l, float* out_r, uint32_t samples,
                  const PartGains& /*gains*/) override {
        m_chip->generate(out_l, out_r, samples);
    }
    void setTargetRate(uint32_t target_rate) override {
        m_chip->setTargetRate(target_rate);
    }
    uint32_t      nativeRate() const override { return m_chip->nativeRate(); }
    // FmGenEngine 側では type() は使わない (ChipType と ChipTypeExt の
    // 区別はファクトリ呼び出し側で既についているため)。
    FmGenChipType type()       const override { return static_cast<FmGenChipType>(-1); }
    const char*   name()       const override { return m_chip->name(); }
    uint32_t      clock()      const override { return m_chip->clock(); }

    FmGenExtChip* extChip() { return m_chip.get(); }

private:
    std::unique_ptr<FmGenExtChip> m_chip;
};
