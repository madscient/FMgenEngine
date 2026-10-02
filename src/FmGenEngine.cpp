// FmGenEngine.cpp
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (詳細はリポジトリルートの LICENSE / README.md を参照)
//
// FmGenEngine.h で宣言した C ファサードの実装 (fmgen バックエンド版)。
// YMEngine (ymfm版) の FmEngineApi.cpp と ABI 完全互換になるよう、
// 関数シグネチャ・挙動を可能な限り合わせている。
//
// 対応チップ (AddChip の name 引数):
//   "OPN"  / "OPNA" / "OPNB" / "OPNBB" / "OPN2" / "OPM" / "SSG"
// 非対応チップは FM_ERR_UNKNOWN_CHIP を返す。
//
// このファイルだけが FmEngine の C++ ヘッダを include する。
// DLL 境界をまたぐのは POD 型と不透明ポインタだけ。

#include "FmGenEngine.h"

#include "FmEngine.h"

#include <new>
#include <stdexcept>
#include <cstring>
#include <array>
#include <string_view>

// FmPart は ChipPart にそのままキャストして渡すので、番号を揃えておく。
// ChipPart に無い FmPart (OPLL/OPL3/OPL4 の部位) は FmEngine が範囲外として拒否する。
static_assert(FM_PART_OPN_FM  == static_cast<int>(ChipPart::OPN_FM),  "FM_PART_OPN_FM");
static_assert(FM_PART_OPN_SSG == static_cast<int>(ChipPart::OPN_SSG), "FM_PART_OPN_SSG");
static_assert(FM_PART_OPN_SSG + 1 == kChipPartCount, "ChipPart has parts FmPart lacks");

// FmMemoryType / FmMemoryAccess も同じく C++ 側の enum にキャストして渡す
#define FM_MEM_MATCHES(c, cpp) static_assert(c == static_cast<int>(ChipMemoryType::cpp), #c)
FM_MEM_MATCHES(FM_MEM_ADPCM_A,         ADPCM_A);
FM_MEM_MATCHES(FM_MEM_ADPCM_B,         ADPCM_B);
FM_MEM_MATCHES(FM_MEM_PCM,             PCM);
FM_MEM_MATCHES(FM_MEM_ADPCM_B_ROMMODE, ADPCM_B_ROMMODE);
#undef FM_MEM_MATCHES
static_assert(FM_MEM_ADPCM_B_ROMMODE + 1 == kChipMemoryTypeEnd, "FmMemoryType and ChipMemoryType differ in count");
static_assert(FM_ACCESS_ROM == static_cast<int>(ChipMemoryAccess::ROM), "FM_ACCESS_ROM");
static_assert(FM_ACCESS_RAM == static_cast<int>(ChipMemoryAccess::RAM), "FM_ACCESS_RAM");

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
    if (clock == 0) return FM_ERR_INVALID_ARG;  // 既定のクロックは持たない
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

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetPartGain(FmEngineHandle h, uint32_t chip_id, FmPart part,
                     float gain_l, float gain_r) {
    REQUIRE_PTR(h);
    const bool ok = static_cast<FmEngineOpaque*>(h)->engine.setPartGain(
        chip_id, static_cast<ChipPart>(part), gain_l, gain_r);
    return ok ? FM_OK : FM_ERR_INVALID_ARG;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetPartGain(FmEngineHandle h, uint32_t chip_id, FmPart part,
                     float* out_l, float* out_r) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(out_l);
    REQUIRE_PTR(out_r);
    const bool ok = static_cast<FmEngineOpaque*>(h)->engine.getPartGain(
        chip_id, static_cast<ChipPart>(part), *out_l, *out_r);
    return ok ? FM_OK : FM_ERR_INVALID_ARG;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetPartMask(FmEngineHandle h, uint32_t chip_id, uint32_t* out_mask) {
    REQUIRE_PTR(h);
    REQUIRE_PTR(out_mask);
    const bool ok = static_cast<FmEngineOpaque*>(h)->engine.getPartMask(chip_id, *out_mask);
    return ok ? FM_OK : FM_ERR_INVALID_ARG;
}

// =========================================================
//  外部メモリ設定
//  OPNA の FM_MEM_ADPCM_A (リズムの内蔵 ROM の内容) は受け付けるが読まない。
//  fmgen の OPNA はリズムを WAV ファイルから読むため。
//  エンジンは SetMemory の data に書き込まない。OPNA の ADPCM-B は写しを
//  割り当て、OPNB/OPNBB は参照する (メモリへ書き込む経路が無い)。
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetMemory(FmEngineHandle h, uint32_t chip_id,
                   FmMemoryType mem_type, const uint8_t* data, uint32_t size) {
    REQUIRE_PTR(h);
    bool ok = false;
    const FmResult r = safeCall([&] {
        ok = static_cast<FmEngineOpaque*>(h)->engine.setMemory(
            chip_id, static_cast<ChipMemoryType>(mem_type), data, size);
    });
    if (r != FM_OK) return r;
    return ok ? FM_OK : FM_ERR_INVALID_ARG;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetMemorySize(FmEngineHandle h, uint32_t chip_id, FmMemoryType mem_type) {
    if (!h) return 0;
    return static_cast<FmEngineOpaque*>(h)->engine.memorySize(
        chip_id, static_cast<ChipMemoryType>(mem_type));
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetMemoryEx(FmEngineHandle h, uint32_t chip_id,
                     FmMemoryType mem_type, uint32_t base,
                     uint8_t* data, uint32_t size, FmMemoryAccess access) {
    REQUIRE_PTR(h);
    bool ok = false;
    const FmResult r = safeCall([&] {
        ok = static_cast<FmEngineOpaque*>(h)->engine.mapMemory(
            chip_id, static_cast<ChipMemoryType>(mem_type), base, data, size,
            static_cast<ChipMemoryAccess>(access));
    });
    if (r != FM_OK) return r;
    return ok ? FM_OK : FM_ERR_INVALID_ARG;
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
