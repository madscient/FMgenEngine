#!/bin/sh
# build_and_run.sh
#
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 FmGenEngine contributors
# (詳細はリポジトリルートの LICENSE / README.md を参照)
#
# fmgen コア DSP ロジックの Linux ネイティブ動作検証ビルド。
#
# Windows / WASAPI 抜きで、fmgen (OPN/OPNA/OPNB/OPM/SSG) が実際に
# レジスタ書き込みに応じた音声サンプルを生成することを検証する
# スモークテスト。実機 Windows がなくても CI 等で fmgen 連携部分の
# 健全性を確認できる。
#
# 使い方:
#   sh test_native/build_and_run.sh
#
# 必要なもの: g++ (C++17 対応), 通常の Linux 開発環境
# (MinGW や Windows SDK は不要。windows.h は test_native/winstub/ の
#  最小スタブで代替する)

set -e
cd "$(dirname "$0")/.."

SRC_DIR="extern/fmgen"
OUT="test_native/test_fmgen_native"

g++ -std=c++17 -O2 -Wall \
    -I test_native/winstub \
    -I src -I "${SRC_DIR}" -I extern \
    -include "${SRC_DIR}/types.h" \
    test_native/test_fmgen_native.cpp \
    test_native/file_stub.cpp \
    "${SRC_DIR}/fmgen.cpp" \
    "${SRC_DIR}/opna.cpp" \
    "${SRC_DIR}/opna_ext.cpp" \
    "${SRC_DIR}/opm.cpp" \
    "${SRC_DIR}/psg.cpp" \
    "${SRC_DIR}/fmtimer.cpp" \
    -Wno-narrowing \
    -o "${OUT}"

echo "--- running ${OUT} ---"
"${OUT}"
