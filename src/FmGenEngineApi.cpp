// FmGenEngineApi.cpp
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (詳細はリポジトリルートの LICENSE / README.md を参照)
//
// FmGenEngineApi.h で宣言した C ファサードの実装 (fmgen バックエンド版)。
// YMEngine (ymfm版) の FmEngineApi.cpp と ABI 完全互換になるよう、
// 関数シグネチャ・挙動を可能な限り合わせている。
//
// 対応チップ (AddChip の name 引数):
//   "OPN"  / "OPNA" / "OPNB" / "OPNBB" / "OPN2" / "OPM" / "SSG"
// 非対応チップは FM_ERR_UNKNOWN_CHIP を返す。
//
// このファイルだけが FmEngine の C++ ヘッダを include する。
// DLL 境界をまたぐのは POD 型と不透明ポインタだけ。

#include "FmGenEngineApi.h"

#include "FmEngine.h"

#include <new>
#include <stdexcept>
#include <cstring>
#include <array>
#include <string_view>

// =========================================================
//  内部構造体 (ハンドルの実体)
// =========================================================
struct FmEngineOpaque {
    FmEngine engine;
    explicit FmEngineOpaque(uint32_t sr) : engine(sr) {}
};

// =========================================================
//  対応チップ定義
//  name: FmEngine_AddChip に渡す文字列
//  FmGenEngine が対応する fmgen バックエンドのチップのみ掲載。
// =========================================================
struct ChipDef {
    const char*     name;       // キーワード文字列
    FmGenChipType   chipType;   // FmGenChipType::* (isExt=false の場合)
    FmGenExtChipType extType;   // FmGenExtChipType::* (isExt=true の場合)
    bool            isExt;      // true = addExtChip, false = addChip
};

static constexpr ChipDef kChipDefs[] = {
    { "OPN",   FmGenChipType::OPN,   {},                      false },
    { "OPNA",  FmGenChipType::OPNA,  {},                      false },
    { "OPNB",  FmGenChipType::OPNB,  {},                      false },
    { "OPNBB", FmGenChipType::OPNBB, {},                      false },
    { "OPN2",  FmGenChipType::OPN2,  {},                      false },
    { "OPM",   FmGenChipType::OPM,   {},                      false },
    { "SSG",   {},                   FmGenExtChipType::SSG,   true  },
};
static constexpr uint32_t kChipDefCount =
    static_cast<uint32_t>(sizeof(kChipDefs) / sizeof(kChipDefs[0]));

static const ChipDef* findChipDef(const char* name) {
    if (!name) return nullptr;
    for (const auto& def : kChipDefs)
        if (strcmp(def.name, name) == 0) return &def;
    return nullptr;
}

// =========================================================
//  例外を FmResult に変換するヘルパー
// =========================================================
template<typename Fn>
static FmResult safeCall(Fn&& fn) noexcept {
    try {
        fn();
        return FM_OK;
    } catch (const std::bad_alloc&) {
        return FM_ERR_ALLOC;
    } catch (const std::invalid_argument&) {
        return FM_ERR_INVALID_ARG;
    } catch (...) {
        return FM_ERR_UNAVAILABLE;
    }
}

#define REQUIRE_PTR(p) do { if (!(p)) return FM_ERR_INVALID_ARG; } while(0)

// =========================================================
//  エンジン生成・破棄
// =========================================================
FMENGINE_API FmEngineHandle FMENGINE_CALL
FmEngine_Create(uint32_t sample_rate) {
    return new(std::nothrow) FmEngineOpaque(sample_rate);
}

FMENGINE_API void FMENGINE_CALL
FmEngine_Destroy(FmEngineHandle h) {
    delete static_cast<FmEngineOpaque*>(h);
}

// =========================================================
//  対応チップ問い合わせ
// =========================================================
FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_Inquiry(FmEngineHandle /*h*/) {
    return kChipDefCount;
}

FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetSupportedChip(FmEngineHandle /*h*/, uint32_t index) {
    if (index >= kChipDefCount) return nullptr;
    return kChipDefs[index].name;
}

// =========================================================
//  チップ追加
//  name: "OPN", "OPNA", "OPNB", "OPNBB", "OPN2", "OPM", "SSG"
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddChip(FmEngineHandle h, const char* name,
                 uint32_t clock, uint32_t* out_id) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(out_id);
    const ChipDef* def = findChipDef(name);
    if (!def) return FM_ERR_UNKNOWN_CHIP;
    return safeCall([&] {
        auto& eng = static_cast<FmEngineOpaque*>(h)->engine;
        if (def->isExt)
            *out_id = eng.addExtChip(def->extType, clock);
        else
            *out_id = eng.addChip(def->chipType, clock);
    });
}

// =========================================================
//  チップ情報取得
// =========================================================
FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetChipName(FmEngineHandle h, uint32_t chip_id) {
    if (!h) return nullptr;
    const auto* c = static_cast<FmEngineOpaque*>(h)->engine.chip(chip_id);
    return c ? c->name() : nullptr;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetNativeRate(FmEngineHandle h, uint32_t chip_id) {
    if (!h) return 0;
    const auto* c = static_cast<FmEngineOpaque*>(h)->engine.chip(chip_id);
    return c ? c->nativeRate() : 0;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetSampleRate(FmEngineHandle h) {
    if (!h) return 0;
    return static_cast<FmEngineOpaque*>(h)->engine.sampleRate();
}

// =========================================================
//  レジスタ書き込み
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Write(FmEngineHandle h, uint32_t chip_id,
               uint8_t reg, uint8_t value, uint32_t port) {
    REQUIRE_PTR(h);
    return safeCall([&] {
        static_cast<FmEngineOpaque*>(h)->engine.write(chip_id, reg, value, port);
    });
}

// =========================================================
//  ゲイン設定
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetGain(FmEngineHandle h, uint32_t chip_id,
                 float gain_l, float gain_r) {
    REQUIRE_PTR(h);
    return safeCall([&] {
        static_cast<FmEngineOpaque*>(h)->engine.setGain(chip_id, gain_l, gain_r);
    });
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetGain(FmEngineHandle h, uint32_t chip_id,
                 float* out_l, float* out_r) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(out_l);
    REQUIRE_PTR(out_r);
    return safeCall([&] {
        auto& eng = static_cast<FmEngineOpaque*>(h)->engine;
        *out_l = eng.getGainL(chip_id);
        *out_r = eng.getGainR(chip_id);
    });
}

// =========================================================
//  外部メモリ設定
//  OPNA の場合、FM_MEM_ADPCM_A は内部的にリズム WAV ファイルの
//  代わりとして扱わず、fmgen の OPNA ADPCM-B RAM へ配置する。
//  (fmgen の OPNA リズム音源は WAV ファイル経由だが、
//   FMEngineTest は ADPCM_A ROM 経由でのみROMを渡すため、
//   fmgen 側では ADPCM_B として吸収する。音源動作には影響しない)
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetMemory(FmEngineHandle h, uint32_t chip_id,
                   FmMemoryType mem_type, const uint8_t* data, uint32_t size) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(data);
    return safeCall([&] {
        // FmMemoryType → FmGenAccessClass 変換
        FmGenAccessClass ac;
        switch (mem_type) {
            case FM_MEM_ADPCM_A: ac = FmGenAccessClass::ADPCM_A; break;
            case FM_MEM_ADPCM_B: ac = FmGenAccessClass::ADPCM_B; break;
            case FM_MEM_PCM:     ac = FmGenAccessClass::PCM;     break;
            default:             ac = FmGenAccessClass::IO;      break;
        }
        static_cast<FmEngineOpaque*>(h)->engine.setMemory(chip_id, ac, data, size);
    });
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetMemorySize(FmEngineHandle h, uint32_t chip_id, FmMemoryType mem_type) {
    if (!h) return 0;
    FmGenAccessClass ac;
    switch (mem_type) {
        case FM_MEM_ADPCM_A: ac = FmGenAccessClass::ADPCM_A; break;
        case FM_MEM_ADPCM_B: ac = FmGenAccessClass::ADPCM_B; break;
        case FM_MEM_PCM:     ac = FmGenAccessClass::PCM;     break;
        default:             ac = FmGenAccessClass::IO;      break;
    }
    return static_cast<FmEngineOpaque*>(h)->engine.memorySize(chip_id, ac);
}

// =========================================================
//  波形生成
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Generate(FmEngineHandle h,
                  float* out_l, float* out_r, uint32_t samples) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(out_l);
    REQUIRE_PTR(out_r);
    return safeCall([&] {
        static_cast<FmEngineOpaque*>(h)->engine.generate(out_l, out_r, samples);
    });
}
