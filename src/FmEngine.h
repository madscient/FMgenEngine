#pragma once
// FmEngine.h
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (この FmEngineApi 互換ラッパー・エンジンコード自体は MIT License。
//  内部で利用する fmgen 本体 (extern/fmgen/) は別ライセンス。
//  詳細はリポジトリルートの LICENSE / README.md を参照)
//
// 複数の FmGenChip を管理し、レジスタ書き込み API と
// オーディオコールバック向けのサンプル生成 API を提供する。
//
// YMEngine (ymfm版) src/FmEngine.h と同名・同シグネチャのクラスとして
// 実装することで、YMEngine の FmEngineApi.cpp の構造をそのまま踏襲できるようにしている。
// 中身が保持するチップは ymfm 製ではなく fmgen 製 (FmGenChip/FmGenExtChip)。
//
// MSVC 対応:
//   constexpr 関数内で std::pow() は使えない (C3615)。
//   dBToLinear() を通常の inline static 関数にする。

#include "FmGenChip.h"
#include "FmGenExtChip.h"
#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <cassert>
#include <cmath>    // std::pow (実行時呼び出し用)

// =========================================================
//  SPSC コマンドキュー (lock-free)
// =========================================================
struct RegWriteCmd {
    uint32_t chip_id;
    uint32_t port;
    uint8_t  reg;
    uint8_t  value;
};

template<typename T, size_t Cap>
class SpscQueue {
public:
    bool push(const T& item) {
        const auto head = m_head.load(std::memory_order_relaxed);
        const auto next = (head + 1) % Cap;
        if (next == m_tail.load(std::memory_order_acquire))
            return false;
        m_buf[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        out = m_buf[tail];
        m_tail.store((tail + 1) % Cap, std::memory_order_release);
        return true;
    }
private:
    std::array<T, Cap> m_buf{};
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};

// =========================================================
//  ChipGain – チップごとのミキシングゲイン
// =========================================================
struct ChipGain {
    std::atomic<float> gain_l{1.0f};
    std::atomic<float> gain_r{1.0f};

    // dB → 線形スケール変換
    // ※ MSVC では std::pow が constexpr でないため constexpr にできない (C3615)。
    //    inline static 関数として定義する。
    static inline float dBToLinear(float dB) {
        return std::pow(10.0f, dB / 20.0f);
    }

    ChipGain() = default;
    ChipGain(const ChipGain& o)
        : gain_l(o.gain_l.load()), gain_r(o.gain_r.load()) {}
};

// =========================================================
//  FmEngine
// =========================================================
class FmEngine {
public:
    explicit FmEngine(uint32_t sample_rate = 44100)
        : m_sample_rate(sample_rate) {}

    // fmgen チップ追加。既定のクロックは持たない (clock=0 は std::invalid_argument)。
    uint32_t addChip(FmGenChipType type, uint32_t clock) {
        auto chip = createFmGenChip(type, clock, m_sample_rate);
        return registerChip(std::move(chip));
    }

    // 外部ライブラリ扱いのチップ追加 (SSG = fmgen::PSG)
    uint32_t addExtChip(FmGenExtChipType type, uint32_t clock) {
        auto ext  = createFmGenExtChip(type, clock, m_sample_rate);
        auto chip = std::make_unique<FmGenExtChipAdapter>(std::move(ext));
        return registerChip(std::move(chip));
    }

    uint32_t sampleRate() const { return m_sample_rate; }
    size_t   chipCount()  const { return m_chips.size(); }

    // ゲイン設定 (任意スレッドから呼べる)
    void setGain(uint32_t chip_id, float gain_l, float gain_r) {
        assert(chip_id < m_gains.size());
        m_gains[chip_id]->gain_l.store(gain_l, std::memory_order_relaxed);
        m_gains[chip_id]->gain_r.store(gain_r, std::memory_order_relaxed);
    }
    void setGain(uint32_t chip_id, float gain) { setGain(chip_id, gain, gain); }

    float getGainL(uint32_t chip_id) const {
        assert(chip_id < m_gains.size());
        return m_gains[chip_id]->gain_l.load(std::memory_order_relaxed);
    }
    float getGainR(uint32_t chip_id) const {
        assert(chip_id < m_gains.size());
        return m_gains[chip_id]->gain_r.load(std::memory_order_relaxed);
    }

    // チップが持つ部位。部位は名前の文字列で指定する (大文字小文字を区別する)。
    // 部位を持たないチップと未知の chip_id は 0 / nullptr。範囲外の index も nullptr。
    uint32_t partCount(uint32_t chip_id) const {
        return chip_id < m_chips.size() ? m_chips[chip_id]->parts().count : 0;
    }
    const char* partName(uint32_t chip_id, uint32_t index) const {
        return chip_id < m_chips.size() ? m_chips[chip_id]->parts().name(index) : nullptr;
    }

    // 部位ごとのゲイン (任意スレッドから呼べる)。実際に掛かるのは
    // setGain() のゲイン × 部位のゲイン。既定値は 1.0。
    // 未知の chip_id、チップが持たない部位の名前、nullptr なら false。
    bool setPartGain(uint32_t chip_id, const char* part, float gain_l, float gain_r) {
        ChipPart p;
        if (!findPart(chip_id, part, p)) return false;
        ChipGain& g = (*m_part_gains[chip_id])[static_cast<size_t>(p)];
        g.gain_l.store(gain_l, std::memory_order_relaxed);
        g.gain_r.store(gain_r, std::memory_order_relaxed);
        return true;
    }

    bool getPartGain(uint32_t chip_id, const char* part, float& out_l, float& out_r) const {
        ChipPart p;
        if (!findPart(chip_id, part, p)) return false;
        const ChipGain& g = (*m_part_gains[chip_id])[static_cast<size_t>(p)];
        out_l = g.gain_l.load(std::memory_order_relaxed);
        out_r = g.gain_r.load(std::memory_order_relaxed);
        return true;
    }

    // チップが持つ外部メモリ。外部メモリは名前の文字列で指定する (大文字小文字を
    // 区別する)。持たないチップと未知の chip_id は 0 / nullptr。範囲外の index も
    // nullptr。
    uint32_t memoryCount(uint32_t chip_id) const {
        return chip_id < m_chips.size() ? m_chips[chip_id]->memories().count : 0;
    }
    const char* memoryName(uint32_t chip_id, uint32_t index) const {
        return chip_id < m_chips.size() ? m_chips[chip_id]->memories().name(index) : nullptr;
    }

    // 外部メモリの割り当て。どれもオーディオスレッド起動前に呼ぶこと (スレッド
    // セーフではない)。参照する data は、割り当てを外すかエンジンを破棄するまで
    // 解放しないこと。
    //
    // mapMemory: C API の FmEngine_SetMemoryEx と同じ。memory のメモリの
    // [base, base + size) に data を割り当てる。data が nullptr なら、その範囲と
    // 重なる割り当てをすべて外す。未知の chip_id、チップが持たないメモリの名前、
    // memory が nullptr、size が 0、範囲が 2^32 を越える、既存の割り当てと重なる、
    // 未知の access なら false。
    bool mapMemory(uint32_t chip_id, const char* memory, uint32_t base,
                   uint8_t* data, uint32_t size, ChipMemoryAccess access) {
        ChipMemoryType type;
        if (!findMemory(chip_id, memory, type)) return false;
        if (!data) return m_chips[chip_id]->unmapMemory(type, base, size);
        if (access != ChipMemoryAccess::ROM && access != ChipMemoryAccess::RAM) return false;
        return m_chips[chip_id]->mapMemory(type, base, data, size, access);
    }

    // C API の FmEngine_SetMemory。memory の割り当てを [0, size) の data だけにする。
    // 未知の chip_id、チップが持たないメモリの名前、memory が nullptr、data が
    // nullptr、size が 0 なら false。
    bool setMemory(uint32_t chip_id, const char* memory,
                   const uint8_t* data, uint32_t size) {
        ChipMemoryType type;
        if (!findMemory(chip_id, memory, type) || !data || size == 0) return false;
        m_chips[chip_id]->setMemory(type, data, size);
        return true;
    }

    // OPNA リズムサンプル(WAV)読み込み。OPNA 以外のチップでは何もせず true を返す。
    // dir_path: '2608_BD.WAV' 等が置かれているディレクトリ ('\' or '/' 終端)
    bool loadRhythmSamples(uint32_t chip_id, const char* dir_path) {
        if (chip_id >= m_chips.size()) return false;
        return m_chips[chip_id]->loadRhythmSamples(dir_path);
    }

    // レジスタ書き込み (任意スレッドから呼べる)
    void write(uint32_t chip_id, uint8_t reg, uint8_t value, uint32_t port = 0) {
        assert(chip_id < m_chips.size());
        m_queue.push({chip_id, port, reg, value});
    }

    // サンプル生成 (オーディオスレッドから呼ぶ)
    void generate(float* out_l, float* out_r, uint32_t samples) {
        // 1. キュー消化
        RegWriteCmd cmd;
        while (m_queue.pop(cmd)) {
            m_chips[cmd.chip_id]->write(cmd.port, cmd.reg, cmd.value);
        }

        // 2. バッファクリア
        std::fill(out_l, out_l + samples, 0.0f);
        std::fill(out_r, out_r + samples, 0.0f);

        // 3. 各チップ生成 → ゲイン付きミックス
        assert(m_chips.size() == m_gains.size());
        assert(m_chips.size() == m_work_bufs.size());
        for (size_t i = 0; i < m_chips.size(); ++i) {
            WorkBuf& wb = m_work_bufs[i];
            wb.l.resize(samples);
            wb.r.resize(samples);

            PartGains pg;
            for (uint32_t p = 0; p < kChipPartCount; ++p) {
                const ChipGain& g = (*m_part_gains[i])[p];
                pg.l[p] = g.gain_l.load(std::memory_order_relaxed);
                pg.r[p] = g.gain_r.load(std::memory_order_relaxed);
            }
            m_chips[i]->generate(wb.l.data(), wb.r.data(), samples, pg);

            // チップのゲインは部位を混ぜたあとに掛ける。部位のゲインが既定の
            // 1.0 なら、チップのゲインの値によらず、fmgen の Mix の出力に
            // チップのゲインを掛けた値とビット単位で同じになる。
            const float gl = m_gains[i]->gain_l.load(std::memory_order_relaxed);
            const float gr = m_gains[i]->gain_r.load(std::memory_order_relaxed);
            for (uint32_t s = 0; s < samples; ++s) {
                out_l[s] += wb.l[s] * gl;
                out_r[s] += wb.r[s] * gr;
            }
        }

        // 4. ソフトクリップ
        for (uint32_t s = 0; s < samples; ++s) {
            out_l[s] = softClip(out_l[s]);
            out_r[s] = softClip(out_r[s]);
        }
    }

    const FmGenChip* chip(uint32_t id) const {
        if (id < m_chips.size()) return m_chips[id].get();
        return nullptr;
    }

private:
    uint32_t registerChip(std::unique_ptr<FmGenChip> chip) {
        const uint32_t id = static_cast<uint32_t>(m_chips.size());
        m_chips.push_back(std::move(chip));
        m_gains.push_back(std::make_unique<ChipGain>());
        m_part_gains.push_back(std::make_unique<PartGainSet>());
        m_work_bufs.emplace_back();
        return id;
    }

    bool findPart(uint32_t chip_id, const char* name, ChipPart& out) const {
        return chip_id < m_chips.size() && m_chips[chip_id]->parts().find(name, out);
    }

    bool findMemory(uint32_t chip_id, const char* name, ChipMemoryType& out) const {
        return chip_id < m_chips.size() && m_chips[chip_id]->memories().find(name, out);
    }

    static float softClip(float x) {
        if (x >  1.5f) return  1.0f;
        if (x < -1.5f) return -1.0f;
        return x * (1.0f - (x * x) / 9.0f);
    }

    struct WorkBuf {
        std::vector<float> l;
        std::vector<float> r;
    };

    using PartGainSet = std::array<ChipGain, kChipPartCount>;

    uint32_t                                m_sample_rate;
    std::vector<std::unique_ptr<FmGenChip>>     m_chips;
    std::vector<std::unique_ptr<ChipGain>>      m_gains;   // unique_ptr: atomic は vector 再確保でムーブ不可
    std::vector<std::unique_ptr<PartGainSet>>   m_part_gains;
    std::vector<WorkBuf>                        m_work_bufs;
    SpscQueue<RegWriteCmd, 4096>                m_queue;
};
