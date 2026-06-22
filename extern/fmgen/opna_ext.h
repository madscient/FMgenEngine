// opna_ext.h
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
//
// fmgen 0.08 (cisc) に存在しない OPNBB (YM2610B) および
// OPN2 (YM2612) の宣言。実装は opna_ext.cpp にある。
//
// OPN2 クラスの本体宣言は opna.h (FmGenEngine 修正済み) にある。
// 本ファイルは OPNBB クラスの宣言と、OPN2 メソッドの宣言のみを含む。
// (OPN2 のメンバ変数は opna.h の FM::OPN2 クラス定義に含まれる)

#pragma once
#include "opna.h"

namespace FM {

// ===========================================================================
//  OPNBB (YM2610B)
//  OPNB の派生クラス。レジスタ 0x29 を正しく処理する点のみ異なる。
//  実装は opna_ext.cpp にある。
// ===========================================================================
class OPNBB : public OPNB
{
public:
    OPNBB();
    virtual ~OPNBB() {}
    void SetReg(uint addr, uint data);
};

// ===========================================================================
//  OPN2 (YM2612 / YM3438) — メソッド宣言のみ
//  クラス定義 (メンバ変数を含む) は opna.h の FM::OPN2 にある。
//  実装は opna_ext.cpp にある。
// ===========================================================================
// OPN2 の全メソッドは opna_ext.cpp で定義される (宣言は opna.h に存在)。

}  // namespace FM
