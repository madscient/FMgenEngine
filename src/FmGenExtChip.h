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
// 依存: fmgen 0.08 (cisc) — extern/fmgen 以下にオリジナルのまま配置

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
        m_psg.SetClock(static_cast<int>(clock), static_cast<int>(target_rate));
        m_psg.Reset();
        m_native_rate = target_rate;
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
        m_psg.SetClock(static_cast<int>(m_clock), static_cast<int>(target_rate));
        m_native_rate = target_rate;
    }

    uint32_t          nativeRate() const override { return m_native_rate; }
    FmGenExtChipType  type()       const override { return FmGenExtChipType::SSG; }
    uint32_t          clock()      const override { return m_clock; }
    const char*       name()       const override { return "SSG (YM2149) [fmgen]"; }

private:
    PSG                      m_psg;
    uint32_t                  m_clock;
    uint32_t                  m_native_rate = 0;
    std::vector<PSG::Sample>  m_work;
};

// =========================================================
//  ファクトリ関数
// =========================================================
inline std::unique_ptr<FmGenExtChip> createFmGenExtChip(
    FmGenExtChipType type, uint32_t clock, uint32_t target_rate) {
    auto resolve = [](uint32_t c, uint32_t def) { return c ? c : def; };
    switch (type) {
        case FmGenExtChipType::SSG:
            return std::make_unique<FmGenPsgChip>(
                resolve(clock, FmGenClock::SSG), target_rate);
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
    void generate(float* out_l, float* out_r, uint32_t samples) override {
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
