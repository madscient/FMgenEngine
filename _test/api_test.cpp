// api_test.cpp
// C API の回帰テスト (部位ごとのゲイン、FM 部のネイティブレート、クロック、外部
// メモリ)。C API を通して見る (default だけはラッパーのチップを直接使う)。
//
//   accept : clock=0 を拒否すること。全チップ × FmPart の番号 0..15 で、受け付ける
//            組み合わせ、既定値、設定した値の読み戻し、GetPartMask。未知の chip_id・
//            null を拒否すること
//   route  : OPN 系で FM_PART_OPN_FM が FM (ADPCM を含む) に、FM_PART_OPN_SSG が SSG
//            に掛かること。片方だけを鳴らし、鳴っていない側の部位を 0 にしても出力が
//            変わらず、鳴っている側を 0 にすると無音になることを見る。L/R を別々に
//            掛けること。チップのゲインと部位のゲインが掛け算になること
//   default: 既定のゲインで、ラッパーの出力が fmgen の Mix (FM と SSG を1本の
//            バッファに足す) を float に直した値と全サンプル一致すること。対照として、
//            SSG のゲインを 0.5 にすると一致しないことも見る
//   native : GetNativeRate の値 (prescale の書き込みを含む)。その値が fmgen が実際に
//            使っているレートであることを、FM の音程 (fnum から計算) と SSG の音程
//            (TP から計算) を測って確かめる
//   memory : SetMemory が FM_MEM_ADPCM_B_ROMMODE・範囲外の種別・size 0・null・未知の
//            chip_id を拒否すること。GetMemorySize。エンジンが data に書き込まない
//            こと (OPNA でチップにメモリを書かせても、渡したバッファは変わらない。
//            書き込みが実際に起きたことは、再生の音が変わることで確かめる)
//
// fmgen には未初期化のまま使われるメンバがある (PSG のカウンタなど) ため、確保した
// メモリを 0 で埋める operator new に差し替えて、インスタンス間で出力を比べられる
// ようにしている。
//
// 全件通れば終了コード 0。

#include "FmGenEngine.h"
#include "FmEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <string>
#include <utility>
#include <vector>

// ---- 0 で埋める operator new ---------------------------------------------
static void* zalloc(std::size_t n) { return std::calloc(1, n ? n : 1); }
static void* zallocAligned(std::size_t n, std::align_val_t a) {
    void* p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a));
    if (p) std::memset(p, 0, n ? n : 1);
    return p;
}
void* operator new(std::size_t n) { if (void* p = zalloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = zalloc(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return zalloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return zalloc(n); }
void* operator new(std::size_t n, std::align_val_t a) { if (void* p = zallocAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = zallocAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return zallocAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return zallocAligned(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }

// ---- 共通 ------------------------------------------------------------------
static int g_fail = 0;

static void check(bool ok, const char* fmt, ...) {
    std::printf("[%s] ", ok ? " OK " : "FAIL");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    if (!ok) ++g_fail;
}

static constexpr uint32_t kRate = 48000;

struct W { uint32_t port; uint8_t reg; uint8_t val; };
using Writes = std::vector<W>;
using Memory = std::vector<std::pair<FmMemoryType, std::vector<uint8_t>>>;

static Writes operator+(Writes a, const Writes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

static std::vector<uint8_t> noiseBytes(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    for (auto& x : v) { seed = seed * 1664525u + 1013904223u; x = static_cast<uint8_t>(seed >> 24); }
    return v;
}

// エンジンは既定のクロックを持たないので、テストはクロックを明示する。
// 値は FMEngineTest のパッチと同じ
static uint32_t clockOf(const char* name) {
    if (!std::strcmp(name, "OPN"))  return 3993600;
    if (!std::strcmp(name, "OPNA")) return 7987200;
    if (!std::strcmp(name, "OPNB") || !std::strcmp(name, "OPNBB")) return 8000000;
    if (!std::strcmp(name, "OPN2")) return 7670453;
    return 3579545;  // OPM, SSG
}

static bool isOpnFamily(const char* name) {
    return !std::strcmp(name, "OPN") || !std::strcmp(name, "OPNA")
        || !std::strcmp(name, "OPNB") || !std::strcmp(name, "OPNBB");
}

// YM2610 には CH1 (と CH4) が無いので、OPNB/OPNBB の FM は CH2 (番号 1) で鳴らす
static int fmChannel(const char* name) {
    return (!std::strcmp(name, "OPNB") || !std::strcmp(name, "OPNBB")) ? 1 : 0;
}

// アルゴリズム 7 で slot1 だけを TL 0 にした正弦波
static Writes fmSine(int ch, int block, int fnum) {
    Writes w;
    w.push_back({ 0, static_cast<uint8_t>(0xB0 + ch), 0x07 });
    for (int s = 0; s < 4; ++s) {
        const uint8_t off = static_cast<uint8_t>(s * 4 + ch);
        w.push_back({ 0, static_cast<uint8_t>(0x30 + off), 0x01 });
        w.push_back({ 0, static_cast<uint8_t>(0x40 + off), static_cast<uint8_t>(s == 0 ? 0x00 : 0x7F) });
        w.push_back({ 0, static_cast<uint8_t>(0x50 + off), 0x1F });
        w.push_back({ 0, static_cast<uint8_t>(0x60 + off), 0x00 });
        w.push_back({ 0, static_cast<uint8_t>(0x70 + off), 0x00 });
        w.push_back({ 0, static_cast<uint8_t>(0x80 + off), 0x0F });
    }
    w.push_back({ 0, static_cast<uint8_t>(0xB4 + ch), 0xC0 });
    w.push_back({ 0, static_cast<uint8_t>(0xA4 + ch), static_cast<uint8_t>((block << 3) | (fnum >> 8)) });
    w.push_back({ 0, static_cast<uint8_t>(0xA0 + ch), static_cast<uint8_t>(fnum & 0xFF) });
    w.push_back({ 0, 0x28, static_cast<uint8_t>(0xF0 | ch) });
    return w;
}

// トーン A だけの矩形波
static Writes ssgTone(int tp) {
    return {
        { 0, 0x00, static_cast<uint8_t>(tp & 0xFF) }, { 0, 0x01, static_cast<uint8_t>(tp >> 8) },
        { 0, 0x07, 0x3E }, { 0, 0x08, 0x0F },
    };
}

static Writes opnaAdpcmB() {
    return {
        { 1, 0x00, 0x01 }, { 1, 0x01, 0xC0 },
        { 1, 0x02, 0x00 }, { 1, 0x03, 0x00 }, { 1, 0x04, 0xFF }, { 1, 0x05, 0x01 },
        { 1, 0x0C, 0xFF }, { 1, 0x0D, 0xFF },
        { 1, 0x09, 0x00 }, { 1, 0x0A, 0x50 }, { 1, 0x0B, 0xFF },
        { 1, 0x00, 0xA0 },
    };
}

static Writes opnbAdpcmA() {
    return {
        { 1, 0x01, 0x3F }, { 1, 0x08, 0xDF },
        { 1, 0x10, 0x00 }, { 1, 0x18, 0x00 }, { 1, 0x20, 0x0F }, { 1, 0x28, 0x00 },
        { 1, 0x00, 0x01 },
    };
}

static Writes opnbAdpcmB() {
    return {
        { 0, 0x10, 0x01 }, { 0, 0x11, 0xC0 },
        { 0, 0x12, 0x00 }, { 0, 0x13, 0x00 }, { 0, 0x14, 0xFF }, { 0, 0x15, 0x00 },
        { 0, 0x19, 0x00 }, { 0, 0x1A, 0x40 }, { 0, 0x1B, 0xFF },
        { 0, 0x10, 0x80 },
    };
}

static Memory adpcmMemory(const char* name) {
    if (!std::strcmp(name, "OPNA"))
        return { { FM_MEM_ADPCM_B, noiseBytes(0x40000, 3) } };
    if (!std::strcmp(name, "OPNB") || !std::strcmp(name, "OPNBB"))
        return { { FM_MEM_ADPCM_A, noiseBytes(0x10000, 1) }, { FM_MEM_ADPCM_B, noiseBytes(0x40000, 2) } };
    return {};
}

struct Gains {
    float chip_l = 1, chip_r = 1;
    float fm_l = 1, fm_r = 1;
    float ssg_l = 1, ssg_r = 1;
};

// L/R を交互に並べた出力
static std::vector<float> render(const char* name, const Writes& writes, const Memory& mem,
                                 const Gains& g, uint32_t samples) {
    FmEngineHandle e = FmEngine_Create(kRate);
    uint32_t id = 0;
    FmEngine_AddChip(e, name, clockOf(name), &id);
    for (const auto& m : mem)
        FmEngine_SetMemory(e, id, m.first, m.second.data(), static_cast<uint32_t>(m.second.size()));
    FmEngine_SetGain(e, id, g.chip_l, g.chip_r);
    FmEngine_SetPartGain(e, id, FM_PART_OPN_FM,  g.fm_l,  g.fm_r);
    FmEngine_SetPartGain(e, id, FM_PART_OPN_SSG, g.ssg_l, g.ssg_r);
    for (const auto& w : writes) FmEngine_Write(e, id, w.reg, w.val, w.port);

    std::vector<float> out;
    std::vector<float> l(480), r(480);
    for (uint32_t done = 0; done < samples; ) {
        const uint32_t n = std::min<uint32_t>(480, samples - done);
        FmEngine_Generate(e, l.data(), r.data(), n);
        for (uint32_t i = 0; i < n; ++i) { out.push_back(l[i]); out.push_back(r[i]); }
        done += n;
    }
    FmEngine_Destroy(e);
    return out;
}

static size_t countNonZero(const std::vector<float>& v, int ch) {
    size_t n = 0;
    for (size_t i = ch; i < v.size(); i += 2) n += v[i] != 0.0f;
    return n;
}

static bool sameBits(const std::vector<float>& a, const std::vector<float>& b, int ch) {
    if (a.size() != b.size()) return false;
    for (size_t i = ch; i < a.size(); i += 2)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return false;
    return true;
}

static bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return sameBits(a, b, 0) && sameBits(a, b, 1);
}

// ---- accept ----------------------------------------------------------------
static void testAccept() {
    static const char* const kChips[] = { "OPN", "OPNA", "OPNB", "OPNBB", "OPN2", "OPM", "SSG" };

    // clock=0 は拒否し、チップを追加しない。名前を引く前に拒否する
    {
        FmEngineHandle e0 = FmEngine_Create(kRate);
        bool ok = true;
        for (const char* name : kChips) {
            uint32_t id = 12345;
            ok &= FmEngine_AddChip(e0, name, 0, &id) == FM_ERR_INVALID_ARG && id == 12345;
        }
        uint32_t id = 0;
        ok &= FmEngine_GetChipName(e0, 0) == nullptr;
        ok &= FmEngine_AddChip(e0, "NOSUCHCHIP", 0, &id) == FM_ERR_INVALID_ARG;
        ok &= FmEngine_AddChip(e0, "NOSUCHCHIP", 1, &id) == FM_ERR_UNKNOWN_CHIP;
        check(ok, "accept: clock=0 is rejected for every chip and adds nothing");
        FmEngine_Destroy(e0);
    }

    FmEngineHandle e = FmEngine_Create(kRate);
    for (const char* name : kChips) {
        uint32_t id = 0;
        FmEngine_AddChip(e, name, clockOf(name), &id);
        const uint32_t expect = isOpnFamily(name) ? 0x3u : 0u;
        uint32_t mask = 0xFFFFFFFFu;
        const FmResult rm = FmEngine_GetPartMask(e, id, &mask);
        check(rm == FM_OK && mask == expect, "accept %s: GetPartMask = 0x%X (expect 0x%X)", name, mask, expect);

        bool ok = true;
        for (int p = 0; p <= 15; ++p) {
            const FmPart part = static_cast<FmPart>(p);
            const bool has = (expect >> p) & 1u;
            float l = -1, r = -1, l2 = -1, r2 = -1;
            const FmResult g1 = FmEngine_GetPartGain(e, id, part, &l, &r);
            const FmResult s  = FmEngine_SetPartGain(e, id, part, 0.25f, 0.75f);
            const FmResult g2 = FmEngine_GetPartGain(e, id, part, &l2, &r2);
            if (has)
                ok &= g1 == FM_OK && l == 1.0f && r == 1.0f
                   && s == FM_OK && g2 == FM_OK && l2 == 0.25f && r2 == 0.75f;
            else
                ok &= g1 == FM_ERR_INVALID_ARG && s == FM_ERR_INVALID_ARG && g2 == FM_ERR_INVALID_ARG;
        }
        check(ok, "accept %s: FmPart 0..15 (default 1.0, read back, others rejected)", name);
    }

    const uint32_t unknown = sizeof(kChips) / sizeof(kChips[0]);
    uint32_t mask = 0;
    float l = 0, r = 0;
    check(FmEngine_GetPartMask(e, unknown, &mask) == FM_ERR_INVALID_ARG
       && FmEngine_SetPartGain(e, unknown, FM_PART_OPN_FM, 1, 1) == FM_ERR_INVALID_ARG
       && FmEngine_GetPartGain(e, unknown, FM_PART_OPN_FM, &l, &r) == FM_ERR_INVALID_ARG,
          "accept: unknown chip_id is rejected");
    check(FmEngine_GetPartMask(e, 0, nullptr) == FM_ERR_INVALID_ARG
       && FmEngine_GetPartGain(e, 0, FM_PART_OPN_FM, nullptr, &r) == FM_ERR_INVALID_ARG
       && FmEngine_GetPartGain(e, 0, FM_PART_OPN_FM, &l, nullptr) == FM_ERR_INVALID_ARG,
          "accept: null output pointer is rejected");
    check(FmEngine_GetPartMask(nullptr, 0, &mask) == FM_ERR_INVALID_ARG
       && FmEngine_SetPartGain(nullptr, 0, FM_PART_OPN_FM, 1, 1) == FM_ERR_INVALID_ARG
       && FmEngine_GetPartGain(nullptr, 0, FM_PART_OPN_FM, &l, &r) == FM_ERR_INVALID_ARG,
          "accept: null handle is rejected");
    FmEngine_Destroy(e);
}

// ---- route -----------------------------------------------------------------
// 鳴らすのが on 側の部位だけのとき、off 側を 0 にしても変わらず、on 側を 0 にすると無音
static void routeOne(const char* name, const char* what, const Writes& w, bool on_is_fm) {
    const Memory mem = adpcmMemory(name);
    const uint32_t n = 9600;
    Gains fm0;  fm0.fm_l  = fm0.fm_r  = 0;
    Gains ssg0; ssg0.ssg_l = ssg0.ssg_r = 0;
    const auto a  = render(name, w, mem, Gains{}, n);
    const auto zf = render(name, w, mem, fm0, n);
    const auto zs = render(name, w, mem, ssg0, n);
    const auto& off_zero = on_is_fm ? zs : zf;
    const auto& on_zero  = on_is_fm ? zf : zs;
    const size_t sound = countNonZero(a, 0);
    check(sound > n / 4 && sameBits(a, off_zero) && countNonZero(on_zero, 0) == 0
                        && countNonZero(on_zero, 1) == 0,
          "route %s %s: on %s part (nonzero %zu/%u)", name, what, on_is_fm ? "FM" : "SSG", sound, n);
}

static void testRoute() {
    for (const char* name : { "OPN", "OPNA", "OPNB", "OPNBB" }) {
        const Writes fm  = fmSine(fmChannel(name), 4, 0x26A);
        const Writes ssg = ssgTone(0x11C);
        routeOne(name, "fm",  fm,  true);
        routeOne(name, "ssg", ssg, false);
        if (!std::strcmp(name, "OPNA")) routeOne(name, "adpcmB", opnaAdpcmB(), true);
        if (std::strcmp(name, "OPNB") == 0 || std::strcmp(name, "OPNBB") == 0) {
            routeOne(name, "adpcmA", opnbAdpcmA(), true);
            routeOne(name, "adpcmB", opnbAdpcmB(), true);
        }

        // L/R を別々に掛ける。ソフトクリップがあるので、変えなかった側の一致で見る
        const uint32_t n = 4800;
        const Memory mem = adpcmMemory(name);
        const auto base = render(name, fm + ssg, mem, Gains{}, n);
        Gains gs; gs.ssg_r = 0.5f;
        Gains gf; gf.fm_l  = 0.5f;
        const auto rs = render(name, fm + ssg, mem, gs, n);
        const auto rf = render(name, fm + ssg, mem, gf, n);
        check(sameBits(base, rs, 0) && !sameBits(base, rs, 1)
           && sameBits(base, rf, 1) && !sameBits(base, rf, 0),
              "route %s: L/R part gains are independent", name);

        // チップのゲイン × 部位のゲイン (2 の冪なので丸めずに一致する)
        Gains chipHalf; chipHalf.chip_l = chipHalf.chip_r = 0.5f;
        Gains ssgHalf;  ssgHalf.ssg_l  = ssgHalf.ssg_r  = 0.5f;
        Gains fmHalf;   fmHalf.fm_l    = fmHalf.fm_r    = 0.5f;
        check(sameBits(render(name, ssg, mem, chipHalf, n), render(name, ssg, mem, ssgHalf, n))
           && sameBits(render(name, fm,  mem, chipHalf, n), render(name, fm,  mem, fmHalf,  n)),
              "route %s: chip gain and part gain multiply", name);
    }
}

// ---- default ---------------------------------------------------------------
template<typename Raw>
static void defaultOne(FmGenChipType type, const char* name, uint32_t clock,
                       const Writes& w, const Memory& mem,
                       bool (*initRaw)(Raw&, uint32_t, const Memory&)) {
    auto wrapped = createFmGenChip(type, clock, kRate);
    auto control = createFmGenChip(type, clock, kRate);
    auto raw = std::make_unique<Raw>();
    initRaw(*raw, clock, mem);
    for (const auto& m : mem) {
        const auto ac = m.first == FM_MEM_ADPCM_A ? FmGenAccessClass::ADPCM_A : FmGenAccessClass::ADPCM_B;
        wrapped->setMemory(ac, m.second.data(), static_cast<uint32_t>(m.second.size()));
        control->setMemory(ac, m.second.data(), static_cast<uint32_t>(m.second.size()));
    }
    for (const auto& x : w) {
        wrapped->write(x.port, x.reg, x.val);
        control->write(x.port, x.reg, x.val);
        raw->SetReg(x.port ? x.reg + 0x100u : x.reg, x.val);
    }

    const PartGains unity   = { { 1.0f, 1.0f }, { 1.0f, 1.0f } };
    const PartGains ssgHalf = { { 1.0f, 0.5f }, { 1.0f, 0.5f } };
    const uint32_t chunk = 480, chunks = 20;
    std::vector<float> wl(chunk), wr(chunk), cl(chunk), cr(chunk);
    std::vector<FM::Sample> buf(chunk * 2);
    size_t mismatch = 0, controlMismatch = 0, sound = 0;
    for (uint32_t c = 0; c < chunks; ++c) {
        wrapped->generate(wl.data(), wr.data(), chunk, unity);
        control->generate(cl.data(), cr.data(), chunk, ssgHalf);
        std::fill(buf.begin(), buf.end(), 0);
        raw->Mix(buf.data(), static_cast<int>(chunk));
        for (uint32_t i = 0; i < chunk; ++i) {
            const float rl = static_cast<float>(buf[i * 2 + 0]) * fmgen_detail::kScale;
            const float rr = static_cast<float>(buf[i * 2 + 1]) * fmgen_detail::kScale;
            mismatch += std::memcmp(&rl, &wl[i], sizeof(float)) != 0;
            mismatch += std::memcmp(&rr, &wr[i], sizeof(float)) != 0;
            controlMismatch += rl != cl[i];
            sound += rl != 0.0f;
        }
    }
    check(mismatch == 0 && controlMismatch > 0 && sound > chunk * chunks / 2,
          "default %s: equals fmgen Mix (mismatch %zu, control mismatch %zu, nonzero %zu)",
          name, mismatch, controlMismatch, sound);
}

static bool initOpn(FM::OPN& c, uint32_t clock, const Memory&) {
    return c.Init(clock, kRate, false, nullptr);
}
static bool initOpna(FM::OPNA& c, uint32_t clock, const Memory& mem) {
    // リズムの WAV はラッパーと同じ手順で探させる
    if (!c.Init(clock, kRate, false, nullptr)) return false;
    const std::string dir = fmgen_detail::getDllDir();
    c.LoadRhythmSample(dir.empty() ? nullptr : dir.c_str());
    for (const auto& m : mem)
        if (m.first == FM_MEM_ADPCM_B) std::memcpy(c.GetADPCMBuffer(), m.second.data(), 0x40000);
    return true;
}
template<typename Opnb>
static bool initOpnb(Opnb& c, uint32_t clock, const Memory& mem) {
    uint8_t* a = nullptr; int as = 0;
    uint8_t* b = nullptr; int bs = 0;
    for (const auto& m : mem) {
        auto* p = const_cast<uint8_t*>(m.second.data());
        if (m.first == FM_MEM_ADPCM_A) { a = p; as = static_cast<int>(m.second.size()); }
        else                           { b = p; bs = static_cast<int>(m.second.size()); }
    }
    return c.Init(clock, kRate, false, a, as, b, bs);
}

static void testDefault() {
    const Writes ssg = ssgTone(0x11C) + Writes{ { 0, 0x02, 0x80 }, { 0, 0x07, 0x3C }, { 0, 0x09, 0x0C } };
    defaultOne<FM::OPN>(FmGenChipType::OPN, "OPN", clockOf("OPN"),
                        fmSine(0, 4, 0x26A) + ssg, {}, initOpn);
    const Memory ma = adpcmMemory("OPNA");
    defaultOne<FM::OPNA>(FmGenChipType::OPNA, "OPNA", clockOf("OPNA"),
                         fmSine(0, 4, 0x26A) + ssg + opnaAdpcmB(), ma, initOpna);
    const Memory mb = adpcmMemory("OPNB");
    const Writes wb = fmSine(1, 4, 0x26A) + ssg + opnbAdpcmA() + opnbAdpcmB();
    defaultOne<FM::OPNB>(FmGenChipType::OPNB, "OPNB", clockOf("OPNB"), wb, mb, initOpnb<FM::OPNB>);
    defaultOne<FM::OPNBB>(FmGenChipType::OPNBB, "OPNBB", clockOf("OPNBB"), wb, mb, initOpnb<FM::OPNBB>);
}

// ---- native ----------------------------------------------------------------
// 負から 0 以上に変わる点を線形補間で求め、最初と最後の間の周期数から周波数を出す
static double measureFreq(const std::vector<float>& lr) {
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = 1; i < lr.size() / 2; ++i) {
        const float a = lr[(i - 1) * 2], b = lr[i * 2];
        if (a < 0.0f && b >= 0.0f) {
            const double t = static_cast<double>(i - 1) + a / static_cast<double>(a - b);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * static_cast<double>(kRate) / (last - first) : 0.0;
}

static uint32_t nativeRateAfter(const char* name, uint32_t clock, const Writes& w) {
    FmEngineHandle e = FmEngine_Create(kRate);
    uint32_t id = 0;
    FmEngine_AddChip(e, name, clock, &id);
    for (const auto& x : w) FmEngine_Write(e, id, x.reg, x.val, x.port);
    float l = 0, r = 0;
    FmEngine_Generate(e, &l, &r, 1);  // 書き込みはここで適用される
    const uint32_t rate = FmEngine_GetNativeRate(e, id);
    FmEngine_Destroy(e);
    return rate;
}

static void testNative() {
    struct Def { const char* name; uint32_t clock; uint32_t div; };
    const Def defs[] = {
        { "OPN",   3993600, 72 }, { "OPNA", 7987200, 144 }, { "OPNB", 8000000, 144 },
        { "OPNBB", 8000000, 144 }, { "OPN2", 7670453, 144 }, { "OPM", 3579545, 64 },
        { "SSG",   3579545, 16 },
    };
    for (const auto& d : defs) {
        const uint32_t got = nativeRateAfter(d.name, d.clock, {});
        check(got == d.clock / d.div, "native %s: clock %u -> %u (expect %u)",
              d.name, d.clock, got, d.clock / d.div);
    }
    check(nativeRateAfter("OPNA", 8000000, {}) == 8000000 / 144, "native OPNA: clock 8000000 -> 55555");

    // prescale: 0x2D/0x2E/0x2F の書き込みで分周比が変わるか、変わらないか
    struct Pre { const char* name; uint32_t clock; uint32_t div[4]; };  // 書き込み無し, 2D, 2E, 2F
    const Pre pres[] = {
        { "OPN",   3993600, {  72,  72,  36,  24 } },
        { "OPNA",  7987200, { 144, 144,  72,  48 } },
        { "OPNB",  8000000, { 144, 144, 144, 144 } },
        { "OPNBB", 8000000, { 144, 144, 144, 144 } },
        { "OPN2",  7670453, { 144, 144, 144, 144 } },  // YM2612 のプリスケーラは 1/6 固定
    };
    const int block = 4, fnum = 0x26A;
    for (const auto& p : pres) {
        for (int k = 0; k < 4; ++k) {
            const Writes pw = k == 0 ? Writes{} : Writes{ { 0, static_cast<uint8_t>(0x2C + k), 0 } };
            const uint32_t expect = p.clock / p.div[k];
            const uint32_t got = nativeRateAfter(p.name, p.clock, pw);

            // 同じ prescale で鳴らした FM の音程から、fmgen が使っているレートを確かめる
            const auto out = render(p.name, pw + fmSine(fmChannel(p.name), block, fnum), {}, Gains{}, 4800 + kRate);
            const std::vector<float> tail(out.begin() + 4800 * 2, out.end());
            const double f = measureFreq(tail);
            const double fexp = fnum * std::ldexp(1.0, block - 1) * got / 1048576.0;
            check(got == expect && std::fabs(f / fexp - 1.0) < 0.005,
                  "native %s prescale %s: rate %u (expect %u), tone %.2f Hz (from rate %.2f Hz)",
                  p.name, k == 0 ? "none" : (k == 1 ? "2D" : (k == 2 ? "2E" : "2F")),
                  got, expect, f, fexp);
        }
    }
    for (const char* name : { "OPN", "OPNA" }) {
        const uint32_t base = nativeRateAfter(name, clockOf(name), {});
        check(nativeRateAfter(name, clockOf(name), { { 1, 0x2F, 0 } }) == base, "native %s: prescale on port1 is ignored", name);
    }

    // SSG のトーン周波数。単体の SSG は YM2149 の SEL を Low (クロック÷2) とした
    // AY-3-8910 相当の clock / (32 × TP)。レートとの関係は レート / (2 × TP)。
    // OPN 系の SSG 部は YMEngine (ymfm) の SSG のレートと同じ式になること
    // (OPN は clock / (32 × TP)、OPNA/OPNB/OPNBB は clock / (64 × TP))
    struct Ssg { const char* name; uint32_t clock; uint32_t div; };
    const Ssg ssgs[] = {
        { "SSG", 3579545, 32 }, { "OPN", 3993600, 32 }, { "OPNA", 7987200, 64 },
        { "OPNB", 8000000, 64 }, { "OPNBB", 8000000, 64 },
    };
    const int tp = 1000;
    for (const auto& s : ssgs) {
        const auto out = render(s.name, ssgTone(tp), {}, Gains{}, 4800 + kRate);
        const double f = measureFreq(std::vector<float>(out.begin() + 4800 * 2, out.end()));
        const double fexp = static_cast<double>(s.clock) / (s.div * tp);
        bool ok = std::fabs(f / fexp - 1.0) < 0.005;
        if (!std::strcmp(s.name, "SSG"))
            ok &= std::fabs(f / (nativeRateAfter("SSG", clockOf("SSG"), {}) / (2.0 * tp)) - 1.0) < 0.005;
        check(ok, "native %s: SSG tone %.2f Hz at TP=%d (expect clock/(%u*TP) = %.2f Hz)",
              s.name, f, tp, s.div, fexp);
    }
}

// ---- memory ----------------------------------------------------------------
// OPNA の ADPCM-B のメモリへ、チップ経由で pattern を書く (port1 の 0x08)
static Writes opnaWriteRam(uint8_t pattern, int bytes) {
    Writes w = {
        { 1, 0x00, 0x01 }, { 1, 0x01, 0x00 },
        { 1, 0x02, 0x00 }, { 1, 0x03, 0x00 }, { 1, 0x04, 0xFF }, { 1, 0x05, 0x01 },
        { 1, 0x0C, 0xFF }, { 1, 0x0D, 0xFF },
        { 1, 0x00, 0x60 },  // メモリへの書き込み
    };
    for (int i = 0; i < bytes; ++i) w.push_back({ 1, 0x08, pattern });
    w.push_back({ 1, 0x00, 0x01 });
    return w;
}

static void testMemory() {
    const std::vector<uint8_t> buf = noiseBytes(0x1000, 7);
    const uint32_t n = static_cast<uint32_t>(buf.size());

    {
        FmEngineHandle e = FmEngine_Create(kRate);
        bool ok = true;
        for (const char* name : { "OPN", "OPNA", "OPNB" }) {
            uint32_t id = 0;
            FmEngine_AddChip(e, name, clockOf(name), &id);
            ok &= FmEngine_SetMemory(e, id, FM_MEM_ADPCM_B_ROMMODE, buf.data(), n) == FM_ERR_INVALID_ARG;
            ok &= FmEngine_SetMemory(e, id, static_cast<FmMemoryType>(0), buf.data(), n) == FM_ERR_INVALID_ARG;
            ok &= FmEngine_SetMemory(e, id, static_cast<FmMemoryType>(5), buf.data(), n) == FM_ERR_INVALID_ARG;
            ok &= FmEngine_SetMemory(e, id, FM_MEM_ADPCM_B, buf.data(), 0) == FM_ERR_INVALID_ARG;
            ok &= FmEngine_SetMemory(e, id, FM_MEM_ADPCM_B, nullptr, n) == FM_ERR_INVALID_ARG;
        }
        check(ok, "memory: ROMMODE, out-of-range types, size 0 and null data are rejected");
        FmEngine_Destroy(e);
    }

    {
        FmEngineHandle e = FmEngine_Create(kRate);
        uint32_t opn = 0, opna = 0, opnb = 0;
        FmEngine_AddChip(e, "OPN",  clockOf("OPN"),  &opn);
        FmEngine_AddChip(e, "OPNA", clockOf("OPNA"), &opna);
        FmEngine_AddChip(e, "OPNB", clockOf("OPNB"), &opnb);
        bool ok = true;
        ok &= FmEngine_SetMemory(e, opna, FM_MEM_ADPCM_B, buf.data(), n) == FM_OK;
        ok &= FmEngine_GetMemorySize(e, opna, FM_MEM_ADPCM_B) == n;
        ok &= FmEngine_GetMemorySize(e, opna, FM_MEM_ADPCM_B_ROMMODE) == 0;
        // OPNA のリズムは WAV から読むので ADPCM-A は受け付けて使わない。
        // チップが持たない種別も受け付けて使わない (YMEngine と同じ)
        ok &= FmEngine_SetMemory(e, opna, FM_MEM_ADPCM_A, buf.data(), n) == FM_OK;
        ok &= FmEngine_GetMemorySize(e, opna, FM_MEM_ADPCM_A) == 0;
        ok &= FmEngine_SetMemory(e, opn, FM_MEM_PCM, buf.data(), n) == FM_OK;
        ok &= FmEngine_SetMemory(e, opnb, FM_MEM_ADPCM_A, buf.data(), n) == FM_OK;
        ok &= FmEngine_SetMemory(e, opnb, FM_MEM_ADPCM_B, buf.data(), n / 2) == FM_OK;
        ok &= FmEngine_GetMemorySize(e, opnb, FM_MEM_ADPCM_A) == n;
        ok &= FmEngine_GetMemorySize(e, opnb, FM_MEM_ADPCM_B) == n / 2;
        check(ok, "memory: accepted types and GetMemorySize");
        FmEngine_Destroy(e);
    }

    // OPNA: チップにメモリを書かせても、渡したバッファは変わらない
    {
        const Memory mem = adpcmMemory("OPNA");
        const std::vector<uint8_t> before = mem[0].second;
        const auto written = render("OPNA", opnaWriteRam(0x77, 256) + opnaAdpcmB(), mem, Gains{}, 9600);
        const auto plain   = render("OPNA", opnaAdpcmB(), mem, Gains{}, 9600);
        check(mem[0].second == before && !sameBits(written, plain),
              "memory: OPNA chip writes do not reach the caller's data (and do change playback)");
    }

    // OPNB: ADPCM-A/B を鳴らしても、渡したバッファは変わらない
    {
        const Memory mem = adpcmMemory("OPNB");
        const std::vector<uint8_t> a = mem[0].second, b = mem[1].second;
        const auto out = render("OPNB", opnbAdpcmA() + opnbAdpcmB(), mem, Gains{}, 9600);
        check(mem[0].second == a && mem[1].second == b && countNonZero(out, 0) > 0,
              "memory: OPNB playback does not modify the caller's data");
    }

    // 未知の chip_id (直す前は範囲外アクセスになるので最後に置く)
    {
        FmEngineHandle e = FmEngine_Create(kRate);
        check(FmEngine_SetMemory(e, 0, FM_MEM_ADPCM_B, buf.data(), n) == FM_ERR_INVALID_ARG,
              "memory: unknown chip_id is rejected");
        FmEngine_Destroy(e);
    }
}

int main() {
    // 途中で異常終了しても、それまでの結果が残るようにする
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testAccept();
    testRoute();
    testDefault();
    testNative();
    testMemory();
    std::printf(g_fail ? "FAILED %d\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
