// main.cpp
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// 本ファイルは YMEngine (https://github.com/madscient/YMEngine、MIT
// License) の src/main.cpp (sample_app) をベースに、fmgen が対応する
// 5 チップ向けに改変したもの。ライセンス上の扱いは YMEngine 側と
// 同じ MIT License とする。詳細はリポジトリルートの LICENSE /
// README.md を参照。
//
// FmGenEngine チップテスト (fmgen バックエンド版)
// YMEngine (ymfm版) の sample_app (src/main.cpp) を流用し、
// fmgen が対応する 5 チップ (OPN/OPNA/OPNB/OPM/SSG) 向けに調整したもの。
//
// 使い方:
//   sample_app.exe [オプション] [file1.json] [file2.json] ...
//
//   引数なし         : patches/all.json を使用
//   JSON ファイル指定 : 指定したファイルを順に処理 (複数指定可)
//   -r <rate>        : サンプルレートを指定 (省略時 48000)
//   -d <name>        : デバイス名を部分一致で指定 (省略時デフォルトデバイス)
//
// JSON フォーマットは YMEngine sample_app と同一 (README.md 参照)。
//
// fmgen バックエンド固有の ROM/サンプル自動ロード:
//   実行ファイルと同じフォルダに以下が存在すれば自動的にロードする。
//     2608_BD.WAV 等 6 種     : OPNA リズムサンプル (FmEngine_LoadRhythmSamples)
//     ym2608_adpcmb.bin       : OPNA ADPCM-B RAM 初期値 (任意)
//     ym2610_adpcma.rom       : OPNB ADPCM-A ROM (任意)
//     ym2610_adpcmb.rom       : OPNB ADPCM-B ROM (任意)
//   いずれも見つからない場合は該当チャンネルが無音になるだけで、
//   FM/SSG チャンネルには影響しない。
//
// 注意: AddChip は Wasapi_Start より前に全て完了させること。
//       (WASAPI スレッドとの競合防止)

#include "FmEngineApi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <windows.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// =========================================================
//  ヘルパー
// =========================================================

static uint32_t parseVal(const json& j) {
    if (j.is_string())
        return static_cast<uint32_t>(std::stoul(j.get<std::string>(), nullptr, 0));
    return j.get<uint32_t>();
}

static void check(FmResult r, const char* msg) {
    if (r != FM_OK) {
        fprintf(stderr, "ERROR %s: code=%d\n", msg, (int)r);
        ExitProcess(1);
    }
}

static std::string toMB(const wchar_t* wstr) {
    if (!wstr || wstr[0] == L'\0') return {};
    const UINT cp = GetACP();
    const int  n  = WideCharToMultiByte(cp, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string buf(n, '\0');
    WideCharToMultiByte(cp, 0, wstr, -1, buf.data(), n, nullptr, nullptr);
    buf.resize(n - 1);
    return buf;
}

static void applyRegs(FmEngineHandle eng, uint32_t chip_id,
                      const json& regs, uint32_t default_port = 0)
{
    if (!regs.is_array()) return;
    for (const auto& r : regs) {
        const uint32_t reg  = parseVal(r["reg"]);
        const uint32_t val  = parseVal(r["val"]);
        const uint32_t port = r.contains("port") ? parseVal(r["port"]) : default_port;
        FmEngine_Write(eng, chip_id,
                       static_cast<uint8_t>(reg),
                       static_cast<uint8_t>(val), port);
    }
}

// =========================================================
//  ROM/サンプル ファイルロード
//  実行ファイルと同じフォルダから検索する。
//  見つからなくても続行する (ROM が必要なチャンネルは無音になる)。
// =========================================================

// 実行ファイルのフォルダを取得 ('\' 終端)
static std::string getExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    const auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

// ROM ファイルをロードしてバッファに格納。失敗時は空を返す。
static std::vector<uint8_t> loadRomFile(const std::string& filename) {
    const std::string path = getExeDir() + filename;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return {};
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return {}; }
    std::vector<uint8_t> buf(sz);
    fread(buf.data(), 1, sz, fp);
    fclose(fp);
    return buf;
}

// チップ種別ごとの ROM 情報テーブル (ADPCM 系。ポインタ渡しするもののみ)
// OPNA のリズム WAV はファイル名が固定 (2608_*.WAV) のため別処理 (loadRhythmIfOpna)。
struct RomEntry {
    std::string chipName;       // kChipTable の name と一致
    FmMemoryType memType;
    std::string filename;       // 実行ファイルと同じフォルダに置くファイル名
    std::string description;    // ログ用説明
};

static const RomEntry kRomTable[] = {
    // OPNA (YM2608): ADPCM-B RAM 初期値 (任意。256KBのRAM領域にコピーされる)
    { "OPNA",  FM_MEM_ADPCM_B, "ym2608_adpcmb.bin",  "YM2608 ADPCM-B RAM 初期値" },
    // OPNB (YM2610): ADPCM-A ROM + ADPCM-B ROM
    { "OPNB",  FM_MEM_ADPCM_A, "ym2610_adpcma.rom",  "YM2610 ADPCM-A ROM" },
    { "OPNB",  FM_MEM_ADPCM_B, "ym2610_adpcmb.rom",  "YM2610 ADPCM-B ROM" },
};

struct ChipEntry { std::string name; bool isExt; int type; };

// fmgen バックエンドが対応するチップのみ (OPN/OPNA/OPNB/OPM/SSG)
static const ChipEntry kChipTable[] = {
    {"OPN",  false, FM_CHIP_OPN  },
    {"OPNA", false, FM_CHIP_OPNA },
    {"OPNB", false, FM_CHIP_OPNB },
    {"OPM",  false, FM_CHIP_OPM  },
    {"SSG",  true,  FM_CHIP_EXT_SSG },
};

static const ChipEntry* findChipEntry(const std::string& name) {
    for (const auto& e : kChipTable)
        if (e.name == name) return &e;
    return nullptr;
}

// =========================================================
//  JSON ファイル読み込み
// =========================================================
static bool loadJson(const char* path, json& out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "Cannot open: %s\n", path); return false; }
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp); rewind(fp);
    std::string buf(sz, '\0');
    fread(buf.data(), 1, sz, fp);
    fclose(fp);
    try   { out = json::parse(buf); return true; }
    catch (const std::exception& e) {
        fprintf(stderr, "JSON parse error (%s): %s\n", path, e.what());
        return false;
    }
}

// basePath と同じディレクトリを基準に相対パス refPath を解決する
static std::string resolveRefPath(const std::string& basePath, const std::string& refPath) {
    const auto pos = basePath.find_last_of("\\/");
    if (pos == std::string::npos) return refPath;
    return basePath.substr(0, pos + 1) + refPath;
}

// "chips" オブジェクト内の各チップ定義について
// {"$ref": "other.json"} 形式の参照を解決し、参照先ファイル内の
// "chips".<同名チップ> の定義に置き換える。
// 参照解決の循環防止のため visitedPaths に自分自身のパスを積んでから呼ぶこと。
static void resolveChipRefs(json& chips, const std::string& currentPath,
                             std::vector<std::string>& visitedPaths,
                             int depth = 0) {
    if (depth > 8) {
        fprintf(stderr, "  [WARN] $ref nesting too deep (>8), aborting resolution\n");
        return;
    }
    for (auto it = chips.begin(); it != chips.end(); ++it) {
        const std::string chipName = it.key();
        json& chipDef = it.value();
        if (!chipDef.is_object() || !chipDef.contains("$ref")) continue;

        const std::string refRelPath = chipDef["$ref"].get<std::string>();
        const std::string refFullPath = resolveRefPath(currentPath, refRelPath);

        // 循環参照チェック
        bool cyclic = false;
        for (const auto& p : visitedPaths) {
            if (p == refFullPath) { cyclic = true; break; }
        }
        if (cyclic) {
            fprintf(stderr, "  [WARN] %s: circular $ref to %s, skipping\n",
                    chipName.c_str(), refFullPath.c_str());
            chipDef = json::object();
            continue;
        }

        json refRoot;
        if (!loadJson(refFullPath.c_str(), refRoot)) {
            fprintf(stderr, "  [WARN] %s: failed to load $ref %s\n",
                    chipName.c_str(), refFullPath.c_str());
            chipDef = json::object();
            continue;
        }
        if (!refRoot.contains("chips") || !refRoot["chips"].contains(chipName)) {
            fprintf(stderr, "  [WARN] %s: $ref %s has no chips.\"%s\"\n",
                    chipName.c_str(), refFullPath.c_str(), chipName.c_str());
            chipDef = json::object();
            continue;
        }

        // 参照先がさらに $ref を含む場合に備えて再帰的に解決
        visitedPaths.push_back(refFullPath);
        resolveChipRefs(refRoot["chips"], refFullPath, visitedPaths, depth + 1);
        visitedPaths.pop_back();

        chipDef = refRoot["chips"][chipName];
    }
}

// =========================================================
//  1 ファイル処理 (チップ追加フェーズ)
//  WASAPI_Start より前に呼ぶこと。
// =========================================================
struct FileContext {
    json     root;
    std::string path;
    bool     valid = false;
    struct Slot { uint32_t id = 0; bool valid = false; };
    std::vector<Slot> slots;
    // ROM バッファ: エンジンが参照している間 (プロセス終了まで) 保持する
    std::vector<std::vector<uint8_t>> romBuffers;
};

static void addChipsFromFile(FileContext& ctx, FmEngineHandle eng) {
    if (!ctx.valid) return;
    if (!ctx.root.contains("chips") || !ctx.root["chips"].is_object()) return;

    const auto& chips = ctx.root["chips"];
    ctx.slots.reserve(chips.size());

    for (auto it = chips.begin(); it != chips.end(); ++it) {
        const std::string chipName = it.key();
        const auto& chipDef = it.value();
        const ChipEntry* entry = findChipEntry(chipName);

        if (!entry) {
            printf("  [SKIP] %s : unsupported on fmgen backend "
                   "(supported: OPN/OPNA/OPNB/OPM/SSG)\n", chipName.c_str());
            ctx.slots.push_back({0, false});
            continue;
        }

        uint32_t chip_id = 0;
        FmResult res;
        if (entry->isExt)
            res = FmEngine_AddExtChip(eng, (FmChipTypeExt)entry->type, 0, &chip_id);
        else
            res = FmEngine_AddChip(eng, (FmChipType)entry->type, 0, &chip_id);

        if (res != FM_OK) {
            printf("  [SKIP] %s : AddChip failed (code=%d)\n", chipName.c_str(), (int)res);
            ctx.slots.push_back({0, false});
            continue;
        }

        // gain: 単一値 "gain" (L/R 共通、後方互換) または
        //       "gain_l" / "gain_r" で左右個別に指定可能。
        //       両方ある場合は gain_l/gain_r が優先。
        {
            const float baseGain = chipDef.value("gain", 1.0f);
            const float gainL = chipDef.value("gain_l", baseGain);
            const float gainR = chipDef.value("gain_r", baseGain);
            FmEngine_SetGain(eng, chip_id, gainL, gainR);
        }

        // OPNA: リズムサンプル(WAV)を実行ファイルと同じフォルダから読み込む。
        // fmgen 固有拡張 API。見つからなくてもリズムchが無音になるだけ。
        if (chipName == "OPNA") {
            const std::string dir = getExeDir();
            const FmResult rr = FmEngine_LoadRhythmSamples(eng, chip_id, dir.c_str());
            if (rr == FM_OK)
                printf("    [Rhythm] OPNA: tried loading 2608_*.WAV from \"%s\"\n",
                       dir.c_str());
            else
                printf("    [Rhythm] OPNA: LoadRhythmSamples failed (code=%d)\n", (int)rr);
        }

        // ROM/RAM が必要なチップは実行ファイルと同じフォルダから読み込む
        for (const auto& rom : kRomTable) {
            if (rom.chipName != chipName) continue;
            ctx.romBuffers.push_back(loadRomFile(rom.filename));
            const auto& buf = ctx.romBuffers.back();
            if (buf.empty()) {
                printf("    [ROM] %s: not found (%s) — channel will be silent\n",
                       rom.description.c_str(), rom.filename.c_str());
                continue;
            }
            const FmResult res2 = FmEngine_SetMemory(
                eng, chip_id, rom.memType, buf.data(), (uint32_t)buf.size());
            if (res2 == FM_OK)
                printf("    [ROM] %s: loaded %zu bytes\n",
                       rom.description.c_str(), buf.size());
            else
                printf("    [ROM] %s: SetMemory failed (code=%d)\n",
                       rom.description.c_str(), (int)res2);
        }
        ctx.slots.push_back({chip_id, true});
    }
}

// =========================================================
//  1 ファイル処理 (発音フェーズ)
//  WASAPI_Start 後に呼ぶこと。
// =========================================================
static void playChipsFromFile(const FileContext& ctx, FmEngineHandle eng) {
    if (!ctx.valid) return;
    if (!ctx.root.contains("chips")) return;

    const uint32_t NOTE_MS = ctx.root.value("global", json{}).value("note_ms", 800u);
    const uint32_t REST_MS = ctx.root.value("global", json{}).value("rest_ms", 200u);

    printf("=== %s ===\n", ctx.path.c_str());

    const auto& chips = ctx.root["chips"];
    uint32_t slotIdx = 0;
    for (auto it = chips.begin(); it != chips.end(); ++it, ++slotIdx) {
        if (!ctx.slots[slotIdx].valid) continue;

        const std::string chipName = it.key();
        const auto&  chipDef = it.value();
        const uint32_t chip_id = ctx.slots[slotIdx].id;

        if (chipDef.contains("init"))
            applyRegs(eng, chip_id, chipDef["init"]);

        printf("  %s [id=%u]  native=%u Hz\n",
               FmEngine_GetChipName(eng, chip_id),
               chip_id, FmEngine_GetNativeRate(eng, chip_id));

        if (!chipDef.contains("channels") || !chipDef["channels"].is_array()) {
            printf("    (no channels)\n");
            continue;
        }

        for (const auto& chDef : chipDef["channels"]) {
            const int      ch      = chDef.value("ch", 0);
            const uint32_t note_ms = chDef.value("note_ms", NOTE_MS);
            const uint32_t rest_ms = chDef.value("rest_ms", REST_MS);
            const uint32_t chPort  = chDef.value("port", 0u);
            const std::string cmt  = chDef.value("_comment", "");

            printf("    ch%-2d %s\n", ch, cmt.c_str());
            fflush(stdout);

            if (chDef.contains("init"))
                applyRegs(eng, chip_id, chDef["init"], chPort);
            if (chDef.contains("key_on"))
                applyRegs(eng, chip_id, chDef["key_on"], chPort);

            Sleep(note_ms);

            if (chDef.contains("key_off"))
                applyRegs(eng, chip_id, chDef["key_off"], chPort);

            Sleep(rest_ms);
        }
    }
    printf("\n");
}

// =========================================================
//  main
// =========================================================
int main(int argc, char* argv[]) {

    // 引数解析
    std::vector<const char*> jsonFiles;
    uint32_t    sampleRate = 48000;
    const char* deviceName = nullptr;

    for (int i = 1; i < argc; ++i) {
        if      (strcmp(argv[i], "-r") == 0 && i+1 < argc) sampleRate = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) deviceName = argv[++i];
        else                                                jsonFiles.push_back(argv[i]);
    }
    if (jsonFiles.empty())
        jsonFiles.push_back("patches/all.json");

    printf("FmGenEngine Test (fmgen backend, YMEngine API compatible)\n");
    printf("Sample rate: %u Hz\n\n", sampleRate);

    // ① エンジン作成
    FmEngineHandle eng = FmEngine_Create(sampleRate);
    if (!eng) { fputs("FmEngine_Create failed\n", stderr); return 1; }

    // ② デバイス列挙・選択
    const uint32_t devCount = Wasapi_GetDeviceCount();
    wchar_t selectedId[256] = {};

    for (uint32_t i = 0; i < devCount; ++i) {
        wchar_t id[256] = {}, name[256] = {};
        Wasapi_GetDeviceId(i, id, 256);
        Wasapi_GetDeviceName(i, name, 256);
        const bool isDef = Wasapi_IsDefaultDevice(i) != 0;
        const std::string mb = toMB(name);
        printf("  [%u] %s%s\n", i, mb.c_str(), isDef ? " (default)" : "");

        if (deviceName) {
            if (mb.find(deviceName) != std::string::npos)
                wcscpy_s(selectedId, id);
        } else if (isDef && selectedId[0] == L'\0') {
            wcscpy_s(selectedId, id);
        }
    }
    printf("\n");

    WasapiHandle wasapi = (selectedId[0] != L'\0')
        ? Wasapi_CreateWithDevice(eng, 0, selectedId)
        : Wasapi_Create(eng, 0);
    if (!wasapi) {
        fputs("Wasapi_Create failed\n", stderr);
        FmEngine_Destroy(eng);
        return 1;
    }

    // ③ 全ファイルを読み込み、全チップを先に追加 (WASAPI 開始前)
    std::vector<FileContext> ctxs(jsonFiles.size());
    for (size_t i = 0; i < jsonFiles.size(); ++i) {
        ctxs[i].path  = jsonFiles[i];
        ctxs[i].valid = loadJson(jsonFiles[i], ctxs[i].root);
        if (ctxs[i].valid && ctxs[i].root.contains("chips") &&
            ctxs[i].root["chips"].is_object()) {
            std::vector<std::string> visited{ ctxs[i].path };
            resolveChipRefs(ctxs[i].root["chips"], ctxs[i].path, visited);
        }
        addChipsFromFile(ctxs[i], eng);
    }

    // ④ WASAPI 開始 (全 AddChip 完了後)
    check(Wasapi_Start(wasapi), "Wasapi_Start");

    // ⑤ 各ファイルを発音テスト
    for (const auto& ctx : ctxs)
        playChipsFromFile(ctx, eng);

    // ⑥ 停止・解放
    Wasapi_Stop(wasapi);
    Wasapi_Destroy(wasapi);
    FmEngine_Destroy(eng);
    printf("Done.\n");
    return 0;
}
