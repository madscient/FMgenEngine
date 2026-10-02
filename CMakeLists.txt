# SPDX-License-Identifier: MIT
# Copyright (c) 2026 FmGenEngine contributors
# (詳細はリポジトリルートの LICENSE / README.md を参照。
#  本ビルド設定自体は MIT License。extern/fmgen/ 以下のソースは
#  cisc 氏による別ライセンス)

cmake_minimum_required(VERSION 3.20)
project(FmGenEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
#  fmgen ソースのパス
#  配布元: http://retropc.net/cisc/m88/
#  (本リポジトリでは extern/fmgen 以下に展開済みのものを同梱)
# -------------------------------------------------------
set(FMGEN_DIR "${CMAKE_SOURCE_DIR}/extern/fmgen" CACHE PATH "fmgen source dir")

if (NOT EXISTS "${FMGEN_DIR}/opna.cpp")
    message(FATAL_ERROR
        "fmgen ソースが見つかりません: ${FMGEN_DIR}\n"
        "  fmgen008.lzh を展開し、中身一式 (opna.cpp 等) を\n"
        "  ${FMGEN_DIR} に配置してください。")
endif()

# =======================================================
#  ターゲット: FmGenEngine.dll (fmgen バックエンド)
# =======================================================
add_library(FmEngineApi SHARED
    src/FmGenEngine.cpp
    # fmgen ソース一式 (改変箇所には [FmGenEngine] の印)
    ${FMGEN_DIR}/fmgen.cpp
    ${FMGEN_DIR}/opna.cpp
    ${FMGEN_DIR}/opm.cpp
    ${FMGEN_DIR}/psg.cpp
    ${FMGEN_DIR}/fmtimer.cpp
    ${FMGEN_DIR}/file.cpp
    # FmGenEngine 追加実装 (OPN2 / OPNBB)
    ${FMGEN_DIR}/opna_ext.cpp
)

# CMake 内部ターゲット名は FmEngineApi のままだが、
# 出力ファイル名は FmGenEngine.dll / FmGenEngine.lib にする。
# これにより YMEngine の YMFMEngine.dll と共存・切り替えが可能になる。
set(FMGEN_OUTPUT_NAME "FmGenEngine")
set_target_properties(FmEngineApi PROPERTIES OUTPUT_NAME "${FMGEN_OUTPUT_NAME}")

target_include_directories(FmEngineApi PRIVATE
    src/
    ${FMGEN_DIR}
    ${CMAKE_SOURCE_DIR}/extern
)

# fmgen.h は types.h (uint/uint8/int32 等の typedef) を自身では
# include しない (fmgen.h 内で `//#include "types.h"` とコメントアウト
# されている) ため、fmgen 関連の全翻訳単位に対して types.h を
# 強制インクルードする。fmgen 本体のソースに include を足さずに解決できる。
if (MSVC)
    set(FMGEN_FORCE_INCLUDE /FI"${FMGEN_DIR}/types.h")
else()
    set(FMGEN_FORCE_INCLUDE -include "${FMGEN_DIR}/types.h")
endif()
target_compile_options(FmEngineApi PRIVATE ${FMGEN_FORCE_INCLUDE})

# FMENGINE_EXPORTS → FmGenEngine.cpp 内で dllexport になる
target_compile_definitions(FmEngineApi PRIVATE FMENGINE_EXPORTS)

# .def ファイルでエクスポートシンボルを明示 (MSVC)
if (MSVC)
    # .def の LIBRARY はインポートライブラリが参照する DLL 名になる。
    # OUTPUT_NAME と食い違ってもリンカは警告 (LNK4070) を出すだけで、
    # 利用側のアプリが実行時に DLL を見つけられなくなるため、ここで止める。
    file(STRINGS "${CMAKE_SOURCE_DIR}/src/FmGenEngine.def" FMGEN_DEF_LIBRARY
         REGEX "^LIBRARY ")
    if (NOT FMGEN_DEF_LIBRARY STREQUAL "LIBRARY \"${FMGEN_OUTPUT_NAME}\"")
        message(FATAL_ERROR
            "src/FmGenEngine.def の LIBRARY (${FMGEN_DEF_LIBRARY}) が "
            "出力名 ${FMGEN_OUTPUT_NAME} と一致しません。")
    endif()
    set_target_properties(FmEngineApi PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/src/FmGenEngine.def\""
    )
endif()

# COM リンク (CLSID 等に必要)
if (WIN32)
    target_link_libraries(FmEngineApi PRIVATE ole32 uuid)
endif()

# バージョン情報を DLL に埋め込む (Windows リソース)
if (MSVC)
    target_sources(FmEngineApi PRIVATE src/FmGenEngine.rc)
endif()

# MSVC 最適化・警告設定
if (MSVC)
    target_compile_options(FmEngineApi PRIVATE
        /Oi /GL /utf-8
        /wd4244   # narrowing conversion (fmgen 内、意図的な切り詰めを含む)
        /wd4267   # size_t → int 等の narrowing
        /wd4996   # 旧来の CRT 関数 (strncpy 等)
    )
    target_link_options(FmEngineApi PRIVATE /LTCG)

    set_target_properties(FmEngineApi PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# =======================================================
#  インストール規則 (任意)
#  cmake --install build --prefix install_dir
# =======================================================
install(TARGETS FmEngineApi
    RUNTIME DESTINATION bin          # .dll
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib          # .lib (import library)
)
install(FILES src/FmGenEngine.h DESTINATION include)
