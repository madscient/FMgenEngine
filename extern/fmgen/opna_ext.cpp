// opna_ext.cpp
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
//
// OPNBB (YM2610B) と OPN2 (YM2612) の実装。
// Channel4::SetMS/SetFB/Mute 等の inline 関数は fmgeninl.h で定義されるため、
// fmgen.h → fmgeninl.h の順で include してから各実装を記述する。
#include "headers.h"
#include "misc.h"      // Limit()
#include "fmgen.h"     // Channel4, Operator, ISample 等
#include "fmgeninl.h"  // Channel4::SetMS, SetFB, Mute, StoreSample 等 inline 定義
#include "opna_ext.h"  // OPNBB/OPN2 クラス宣言 (opna.h を内部でinclude)

#define BUILD_OPNB     // OPNB の実装 (ADPCMAMix 等) を opna.cpp が提供するため
                       // このファイルでは OPNB 系の静的変数再定義を避けるためのガード

namespace FM {

// ===========================================================================
//  OPNBB
// ===========================================================================
OPNBB::OPNBB() : OPNB() {}

void OPNBB::SetReg(uint addr, uint data)
{
    addr &= 0x1ff;
    if (addr == 0x29) {
        // OPNB は case 0x29: break で reg29 への書き込みを無視するが、
        // OPNBB (YM2610B) は正しく reg29 を更新する。
        // reg29 は OPNABase の protected メンバとして派生クラスから参照可能。
        reg29 = data;
    } else {
        OPNB::SetReg(addr, data);
    }
}

// ===========================================================================
//  OPN2
// ===========================================================================
OPN2::OPN2()
    : dacen(false), dacout(0)
{
    memset(fnum,  0, sizeof(fnum));
    memset(fnum3, 0, sizeof(fnum3));
    memset(fnum2, 0, sizeof(fnum2));

    // OPN 同様、全チャンネルに chip と動作モード (typeN) を設定する。
    // Reset() → Channel4::Reset() → Operator::Reset() → Chip::GetRatio()
    // が呼ばれる前に SetChip を完了させておくことが必須。
    csmch = &ch[2];
    for (int i = 0; i < 6; i++) {
        ch[i].SetChip(&chip);
        ch[i].SetType(FM::typeN);
    }
}

bool OPN2::Init(uint c, uint r, bool ipflag, const char*)
{
    if (!SetRate(c, r, ipflag))
        return false;
    Reset();
    SetVolumeFM(0);
    SetChannelMask(0);
    return true;
}

bool OPN2::SetRate(uint c, uint r, bool /*ipflag*/)
{
    OPNBase::Init(c, r);
    RebuildTimeTable();
    return true;
}

void OPN2::Reset()
{
    for (int i = 0x20; i < 0x28; i++) SetReg(i, 0);
    for (int i = 0x30; i < 0xc0; i++) SetReg(i, 0);
    for (int i = 0x130; i < 0x1c0; i++) SetReg(i, 0);

    dacen  = false;
    dacout = 0;

    OPNBase::Reset();
    for (int i = 0; i < 6; i++)
        ch[i].Reset();
}

void OPN2::SetStatus(uint bits)
{
    if (!(status & bits)) {
        status |= bits;
        Intr(true);
    }
}

void OPN2::ResetStatus(uint bit)
{
    status &= ~bit;
    if (!status)
        Intr(false);
}

void OPN2::SetChannelMask(uint mask)
{
    for (int i = 0; i < 6; i++)
        ch[i].Mute(!!(mask & (1 << i)));
}

void OPN2::SetReg(uint addr, uint data)
{
    if (addr >= 0x200) return;

    const bool port1 = (addr >= 0x100);
    const int  base  = port1 ? 3 : 0;
    int c = addr & 3;

    switch (addr & ~0x100u)
    {
    // タイマー (port0 のみ)
    case 0x24: case 0x25:
        if (!port1) SetTimerA(addr, data);
        break;
    case 0x26:
        if (!port1) SetTimerB(data);
        break;
    case 0x27:
        if (!port1) SetTimerControl(data);
        break;

    // Key On/Off
    case 0x28:
        if (!port1 && (data & 3) < 3) {
            int chi = (data & 3) + ((data & 4) ? 3 : 0);
            ch[chi].KeyControl(data >> 4);
        }
        break;

    // DAC (port0 のみ)
    case 0x2a:
        if (!port1)
            dacout = (static_cast<int>(data) - 0x80) << 6;
        break;
    case 0x2b:
        if (!port1)
            dacen = (data & 0x80) != 0;
        break;

    // プリスケーラ
    case 0x2d: case 0x2e: case 0x2f:
        if (!port1) SetPrescaler((addr & ~0x100u) - 0x2d);
        break;

    // F-Number 下位
    case 0xa0: case 0xa1: case 0xa2:
        if (c < 3) {
            fnum[c + base] = data + fnum2[c + base] * 0x100;
            ch[c + base].SetFNum(fnum[c + base]);
        }
        break;
    // F-Number 上位
    case 0xa4: case 0xa5: case 0xa6:
        if (c < 3)
            fnum2[c + base] = static_cast<uint8>(data);
        break;

    // CSM 効果音モード F-Number (port0 のみ)
    case 0xa8: case 0xa9: case 0xaa:
        if (!port1 && c < 3)
            fnum3[c] = data + fnum2[c + 6] * 0x100;
        break;
    case 0xac: case 0xad: case 0xae:
        if (!port1 && c < 3)
            fnum2[c + 6] = static_cast<uint8>(data);
        break;

    // FB / Algorithm
    case 0xb0: case 0xb1: case 0xb2:
        if (c < 3) {
            ch[c + base].SetFB((data >> 3) & 7);
            ch[c + base].SetAlgorithm(data & 7);
        }
        break;

    // PAN / LFO sensitivity
    case 0xb4: case 0xb5: case 0xb6:
        if (c < 3)
            ch[c + base].SetMS(data);
        break;

    // オペレータパラメータ
    default:
        if (c < 3) {
            if ((addr & 0xf0) == 0x60)
                data &= 0x1f;
            OPNBase::SetParameter(&ch[c + base], addr, data);
        }
        break;
    }
}

void OPN2::Mix(Sample* buffer, int nsamples)
{
#define OPN2_IStoSample(s) ((Limit(s, 0x7fff, -0x8000) * fmvolume) >> 14)

    // F-Number セット (CSM 効果音モード対応)
    for (int i = 0; i < 6; i++) {
        if (i == 2 && (regtc & 0xc0)) {
            ch[2].op[0].SetFNum(fnum3[1]);
            ch[2].op[1].SetFNum(fnum3[2]);
            ch[2].op[2].SetFNum(fnum3[0]);
            ch[2].op[3].SetFNum(fnum[2]);
        } else {
            ch[i].SetFNum(fnum[i]);
        }
    }

    // アクティブチャンネル判定 (DAC 有効時は ch[5]=CH6 を FM として使わない)
    int act = 0;
    const int fm_chs = dacen ? 5 : 6;
    for (int i = 0; i < fm_chs; i++)
        act |= ch[i].Prepare() << (i * 2);

    Sample* limit = buffer + nsamples * 2;

    if (dacen) {
        // DAC モード: CH6 を DAC 出力で置き換え
        for (Sample* dest = buffer; dest < limit; dest += 2) {
            ISample s = 0;
            if (act & 0x001) s  = ch[0].Calc();
            if (act & 0x004) s += ch[1].Calc();
            if (act & 0x010) s += ch[2].Calc();
            if (act & 0x040) s += ch[3].Calc();
            if (act & 0x100) s += ch[4].Calc();
            ISample fm  = OPN2_IStoSample(s);
            ISample dac = OPN2_IStoSample(dacout);
            StoreSample(dest[0], fm + dac);
            StoreSample(dest[1], fm + dac);
        }
    } else if (act) {
        for (Sample* dest = buffer; dest < limit; dest += 2) {
            ISample s = 0;
            if (act & 0x001) s  = ch[0].Calc();
            if (act & 0x004) s += ch[1].Calc();
            if (act & 0x010) s += ch[2].Calc();
            if (act & 0x040) s += ch[3].Calc();
            if (act & 0x100) s += ch[4].Calc();
            if (act & 0x400) s += ch[5].Calc();
            s = OPN2_IStoSample(s);
            StoreSample(dest[0], s);
            StoreSample(dest[1], s);
        }
    }
#undef OPN2_IStoSample
}

}  // namespace FM
