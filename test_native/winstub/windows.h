// windows.h (Linux検証ビルド専用スタブ)
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (詳細はリポジトリルートの LICENSE / README.md を参照)
//
// fmgen の DSP コアロジック (fmgen.cpp/opna.cpp/opm.cpp/psg.cpp/fmtimer.cpp)
// が headers.h 経由で要求する最小限の定義のみを提供する。
// 本物の Windows 環境では使われない (本番ビルドでは windows.h 本体が使われる)。
#pragma once
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// x86_64 Linux の GCC は __stdcall を知らないため空文字に展開する。
// (x86_64 では呼び出し規約の区別自体が ABI 上ほぼ意味を持たない)
#ifndef __stdcall
#define __stdcall
#endif

// file.h (FileIO) が要求する最小限の型。
// 検証テストでは FileIO を実際には使わない (OPNA::LoadRhythmSample を
// 呼ばない)ため、リンクできれば中身は使われない。
typedef void* HANDLE;
