// test_fmgen_native.cpp
// fmgen コア DSP ロジックの動作検証 (Linux ネイティブビルド)。
// FmGenChip.h / FmGenExtChip.h ラッパー経由で各チップを実際に駆動し、
// 音声サンプルが正しく生成される(無音でない・クラッシュしない)ことを確認する。
//
// 検証ビルド専用: __stdcall / MAX_PATH 等の Windows 依存マクロ・型を
// 解決するためスタブ windows.h を明示的に先に include する。
#include "windows.h"  // winstub/windows.h (検証ビルド専用スタブ)
#include "FmGenChip.h"
#include "FmGenExtChip.h"
#include "FmEngine.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

static void testChip(const char* label, FmGenChip& chip,
                      const std::vector<std::tuple<uint32_t,uint8_t,uint8_t>>& regs,
                      uint32_t samples = 4800) {
    for (auto& [port, reg, val] : regs)
        chip.write(port, reg, val);

    std::vector<float> l(samples), r(samples);
    chip.generate(l.data(), r.data(), samples);

    float peak = 0.0f, sumsq = 0.0f;
    for (uint32_t i = 0; i < samples; ++i) {
        peak = std::max(peak, std::fabs(l[i]));
        peak = std::max(peak, std::fabs(r[i]));
        sumsq += l[i]*l[i] + r[i]*r[i];
    }
    float rms = std::sqrt(sumsq / (samples * 2));
    printf("[%-20s] name=%-24s native=%6u Hz  peak=%.5f rms=%.5f  %s\n",
           label, chip.name(), chip.nativeRate(), peak, rms,
           peak > 0.0001f ? "OK (sound generated)" : "WARN (silent!)");
}

int main() {
    const uint32_t SR = 48000;
    printf("=== fmgen native DSP smoke test (sample_rate=%u) ===\n\n", SR);

    // --- OPN (YM2203) ---
    {
        auto chip = createFmGenChip(FmGenChipType::OPN, 0, SR);
        // CH0: block=3 fnum=0x4D5 相当のレジスタ (YMEngine opn.json と同等の設定)
        testChip("OPN", *chip, {
            {0, 0x30, 0x71}, {0, 0x40, 0x23}, {0, 0x50, 0x9F}, {0, 0x60, 0x05},
            {0, 0x70, 0x02}, {0, 0x80, 0x11}, {0, 0x34, 0x0D}, {0, 0x44, 0x2D},
            {0, 0x54, 0x9F}, {0, 0x64, 0x05}, {0, 0x74, 0x02}, {0, 0x84, 0x11},
            {0, 0x38, 0x33}, {0, 0x48, 0x26}, {0, 0x58, 0x9F}, {0, 0x68, 0x05},
            {0, 0x78, 0x02}, {0, 0x88, 0x11}, {0, 0x3c, 0x01}, {0, 0x4c, 0x00},
            {0, 0x5c, 0x9F}, {0, 0x6c, 0x07}, {0, 0x7c, 0x02}, {0, 0x8c, 0x11},
            {0, 0xb0, 0x04}, {0, 0xa4, 0x1c}, {0, 0xa0, 0xd5},
            {0, 0x28, 0xf0},  // key on ch0
        });
    }

    // --- OPNA (YM2608) ---
    {
        auto chip = createFmGenChip(FmGenChipType::OPNA, 0, SR);
        testChip("OPNA", *chip, {
            {0, 0x29, 0x80}, // enable ch3-5
            {0, 0xb0, 0x04}, {0, 0xb4, 0x80},
            {0, 0x30, 0x71}, {0, 0x40, 0x23}, {0, 0x50, 0x9F}, {0, 0x60, 0x05},
            {0, 0x70, 0x02}, {0, 0x80, 0x11}, {0, 0x34, 0x0D}, {0, 0x44, 0x2D},
            {0, 0x54, 0x9F}, {0, 0x64, 0x05}, {0, 0x74, 0x02}, {0, 0x84, 0x11},
            {0, 0x38, 0x33}, {0, 0x48, 0x26}, {0, 0x58, 0x9F}, {0, 0x68, 0x05},
            {0, 0x78, 0x02}, {0, 0x88, 0x11}, {0, 0x3c, 0x01}, {0, 0x4c, 0x00},
            {0, 0x5c, 0x9F}, {0, 0x6c, 0x07}, {0, 0x7c, 0x02}, {0, 0x8c, 0x11},
            {0, 0xa4, 0x1c}, {0, 0xa0, 0xd5},
            {0, 0x28, 0xf0},
        });
    }

    // --- OPNB (YM2610) ---
    // 注意: OPNB は CH0/CH3 が ADPCM 専用で FM として使えない仕様
    // (YM2610 実機の制限)。patches/opnb.json と同様に CH1 (0xb1 系) を使う。
    {
        auto chip = createFmGenChip(FmGenChipType::OPNB, 0, SR);
        testChip("OPNB", *chip, {
            {0, 0xb1, 0x04}, {0, 0xb5, 0x80},
            {0, 0x31, 0x71}, {0, 0x41, 0x23}, {0, 0x51, 0x9F}, {0, 0x61, 0x05},
            {0, 0x71, 0x02}, {0, 0x81, 0x11}, {0, 0x35, 0x0D}, {0, 0x45, 0x2D},
            {0, 0x55, 0x9F}, {0, 0x65, 0x05}, {0, 0x75, 0x02}, {0, 0x85, 0x11},
            {0, 0x39, 0x33}, {0, 0x49, 0x26}, {0, 0x59, 0x9F}, {0, 0x69, 0x05},
            {0, 0x79, 0x02}, {0, 0x89, 0x11}, {0, 0x3d, 0x01}, {0, 0x4d, 0x00},
            {0, 0x5d, 0x9F}, {0, 0x6d, 0x07}, {0, 0x7d, 0x02}, {0, 0x8d, 0x11},
            {0, 0xa5, 0x1d}, {0, 0xa1, 0x6a},
            {0, 0x28, 0xf1},  // OPNB ch1 keyon
        });
    }

    // --- OPM (YM2151) ---
    {
        auto chip = createFmGenChip(FmGenChipType::OPM, 0, SR);
        testChip("OPM", *chip, {
            {0, 0x20, 0xc7}, // RL/FB/CONNECT ch0
            {0, 0x40, 0x01}, {0, 0x60, 0x1b}, {0, 0x80, 0x9f}, {0, 0xa0, 0x00},
            {0, 0xc0, 0x00}, {0, 0xe0, 0x0f},
            {0, 0x48, 0x01}, {0, 0x68, 0x00}, {0, 0x88, 0x9f}, {0, 0xa8, 0x00},
            {0, 0xc8, 0x00}, {0, 0xe8, 0x0f},
            {0, 0x50, 0x01}, {0, 0x70, 0x00}, {0, 0x90, 0x9f}, {0, 0xb0, 0x00},
            {0, 0xd0, 0x00}, {0, 0xf0, 0x0f},
            {0, 0x58, 0x01}, {0, 0x78, 0x00}, {0, 0x98, 0x9f}, {0, 0xb8, 0x00},
            {0, 0xd8, 0x00}, {0, 0xf8, 0x0f},
            {0, 0x28, 0x4a}, // KC ch0 (octave/note)
            {0, 0x08, 0x78}, // key on ch0 (slot mask | ch)
        });
    }

    // --- SSG (YM2149, ExtChip経由) ---
    {
        auto ext = createFmGenExtChip(FmGenExtChipType::SSG, 0, SR);
        FmGenExtChipAdapter adapter(std::move(ext));
        testChip("SSG", adapter, {
            {0, 0x00, 0xef}, {0, 0x01, 0x00}, // tone period ch A
            {0, 0x07, 0x3e}, // mixer: ch A tone on
            {0, 0x08, 0x0f}, // volume ch A = max
        });
    }

    printf("\n=== All chips tested. No crash = pass. ===\n");

    // --- SetMemory (ADPCM) 経路の検証 ---
    printf("\n=== SetMemory (ADPCM) path test ===\n");
    {
        auto chip = createFmGenChip(FmGenChipType::OPNA, 0, SR);
        std::vector<uint8_t> dummyAdpcmB(4096, 0x55);
        chip->setMemory(FmGenAccessClass::ADPCM_B, dummyAdpcmB.data(),
                         static_cast<uint32_t>(dummyAdpcmB.size()));
        printf("[OPNA SetMemory ADPCM_B] memorySize=%u (expected %zu)  %s\n",
               chip->memorySize(FmGenAccessClass::ADPCM_B), dummyAdpcmB.size(),
               chip->memorySize(FmGenAccessClass::ADPCM_B) == dummyAdpcmB.size()
                   ? "OK" : "FAIL");
    }
    {
        auto chip = createFmGenChip(FmGenChipType::OPNB, 0, SR);
        std::vector<uint8_t> dummyAdpcmA(4096, 0xAA);
        std::vector<uint8_t> dummyAdpcmB(4096, 0xBB);
        chip->setMemory(FmGenAccessClass::ADPCM_A, dummyAdpcmA.data(),
                         static_cast<uint32_t>(dummyAdpcmA.size()));
        chip->setMemory(FmGenAccessClass::ADPCM_B, dummyAdpcmB.data(),
                         static_cast<uint32_t>(dummyAdpcmB.size()));
        printf("[OPNB SetMemory ADPCM_A] memorySize=%u (expected %zu)  %s\n",
               chip->memorySize(FmGenAccessClass::ADPCM_A), dummyAdpcmA.size(),
               chip->memorySize(FmGenAccessClass::ADPCM_A) == dummyAdpcmA.size()
                   ? "OK" : "FAIL");
        printf("[OPNB SetMemory ADPCM_B] memorySize=%u (expected %zu)  %s\n",
               chip->memorySize(FmGenAccessClass::ADPCM_B), dummyAdpcmB.size(),
               chip->memorySize(FmGenAccessClass::ADPCM_B) == dummyAdpcmB.size()
                   ? "OK" : "FAIL");
        // SetMemory 後も正常に音を生成できるか (Init再呼び出しでクラッシュしないか)
        testChip("OPNB(post-SetMemory)", *chip, {
            {0, 0xb1, 0x04}, {0, 0xb5, 0x80},
            {0, 0x31, 0x71}, {0, 0x41, 0x23}, {0, 0x51, 0x9F}, {0, 0x61, 0x05},
            {0, 0x71, 0x02}, {0, 0x81, 0x11}, {0, 0x35, 0x0D}, {0, 0x45, 0x2D},
            {0, 0x55, 0x9F}, {0, 0x65, 0x05}, {0, 0x75, 0x02}, {0, 0x85, 0x11},
            {0, 0x39, 0x33}, {0, 0x49, 0x26}, {0, 0x59, 0x9F}, {0, 0x69, 0x05},
            {0, 0x79, 0x02}, {0, 0x89, 0x11}, {0, 0x3d, 0x01}, {0, 0x4d, 0x00},
            {0, 0x5d, 0x9F}, {0, 0x6d, 0x07}, {0, 0x7d, 0x02}, {0, 0x8d, 0x11},
            {0, 0xa5, 0x1d}, {0, 0xa1, 0x6a},
            {0, 0x28, 0xf1},
        });
    }

    // --- ポート1 (OPNB ADPCM拡張レジスタ空間: addr+0x100) の書き込み検証 ---
    printf("\n=== Port1 (addr+0x100) write smoke test ===\n");
    {
        auto chip = createFmGenChip(FmGenChipType::OPNB, 0, SR);
        // port=1 で reg=0xa0 (実際には addr=0x1a0 = ch3 fnum) に書き込む
        chip->write(1, 0xa1, 0x6a);  // -> addr 0x1a1 (ch4 fnum L)
        chip->write(1, 0xa5, 0x1d);  // -> addr 0x1a5 (ch4 fnum H)
        std::vector<float> l(100), r(100);
        chip->generate(l.data(), r.data(), 100); // クラッシュしなければOK
        printf("[OPNB port1 write] no crash -> OK\n");
    }

    // --- FmEngine (複数チップ管理・ゲイン・SPSCキュー) 統合テスト ---
    printf("\n=== FmEngine integration test (multi-chip, gain, queue) ===\n");
    {
        FmEngine eng(SR);
        const uint32_t opnaId = eng.addChip(FmGenChipType::OPNA, 0);
        const uint32_t ssgId  = eng.addExtChip(FmGenExtChipType::SSG, 0);

        printf("chipCount=%zu (expected 2)  %s\n", eng.chipCount(),
               eng.chipCount() == 2 ? "OK" : "FAIL");

        // ゲイン設定・取得の往復確認
        eng.setGain(opnaId, 0.8f, 0.6f);
        printf("getGainL/R(opna)=%.2f/%.2f (expected 0.80/0.60)  %s\n",
               eng.getGainL(opnaId), eng.getGainR(opnaId),
               (eng.getGainL(opnaId) == 0.8f && eng.getGainR(opnaId) == 0.6f)
                   ? "OK" : "FAIL");

        // write() はキュー経由 (非同期)。generate() 呼び出し時に消化される。
        eng.write(opnaId, 0x29, 0x80);
        eng.write(opnaId, 0xb0, 0x04); eng.write(opnaId, 0xb4, 0x80);
        eng.write(opnaId, 0x30, 0x71); eng.write(opnaId, 0x40, 0x23);
        eng.write(opnaId, 0x50, 0x9F); eng.write(opnaId, 0x60, 0x05);
        eng.write(opnaId, 0x70, 0x02); eng.write(opnaId, 0x80, 0x11);
        eng.write(opnaId, 0x34, 0x0D); eng.write(opnaId, 0x44, 0x2D);
        eng.write(opnaId, 0x54, 0x9F); eng.write(opnaId, 0x64, 0x05);
        eng.write(opnaId, 0x74, 0x02); eng.write(opnaId, 0x84, 0x11);
        eng.write(opnaId, 0x38, 0x33); eng.write(opnaId, 0x48, 0x26);
        eng.write(opnaId, 0x58, 0x9F); eng.write(opnaId, 0x68, 0x05);
        eng.write(opnaId, 0x78, 0x02); eng.write(opnaId, 0x88, 0x11);
        eng.write(opnaId, 0x3c, 0x01); eng.write(opnaId, 0x4c, 0x00);
        eng.write(opnaId, 0x5c, 0x9F); eng.write(opnaId, 0x6c, 0x07);
        eng.write(opnaId, 0x7c, 0x02); eng.write(opnaId, 0x8c, 0x11);
        eng.write(opnaId, 0xa4, 0x1c); eng.write(opnaId, 0xa0, 0xd5);
        eng.write(opnaId, 0x28, 0xf0);

        eng.write(ssgId, 0x00, 0xef); eng.write(ssgId, 0x01, 0x00);
        eng.write(ssgId, 0x07, 0x3e); eng.write(ssgId, 0x08, 0x0f);

        std::vector<float> l(4800), r(4800);
        eng.generate(l.data(), r.data(), 4800); // キュー消化+ミックス+ソフトクリップ

        float peak = 0.0f;
        for (uint32_t i = 0; i < 4800; ++i)
            peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        printf("FmEngine.generate() peak=%.5f  %s\n", peak,
               peak > 0.0001f ? "OK (mixed sound generated)" : "WARN (silent!)");

        // ソフトクリップが効いているか (1.0を明確に超えないか)
        printf("softclip bound check: peak<=1.0 -> %s\n", peak <= 1.0f ? "OK" : "FAIL");
    }

    return 0;
}
