# SPDX-License-Identifier: MIT
# Copyright (c) 2026 FmGenEngine contributors
# (詳細はリポジトリルートの LICENSE / README.md を参照。
#  本ビルド設定自体は MIT License。extern/fmgen/ 以下のソースは
#  cisc 氏による別ライセンス)

cmake_minimum_required(VERSION 3.20)
project(FmGenEngineApi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR
        "FmGenEngine は WASAPI / fmgen (Windows API 依存) を使用するため "
        "Windows (MSVC または MinGW) 専用です。")
endif()

# -------------------------------------------------------
#  fmgen ソースのパス
#  オリジナル (cisc, 1998-2003) を無改造のまま配置すること。
#  入手先: http://www retropc net 配布の fmgen008.lzh 等
#  (本リポジトリでは extern/fmgen 以下に展開済みのものを同梱)
# -------------------------------------------------------
set(FMGEN_DIR "${CMAKE_SOURCE_DIR}/extern/fmgen" CACHE PATH "fmgen source dir")

if (NOT EXISTS "${FMGEN_DIR}/opna.cpp")
    message(FATAL_ERROR
        "fmgen ソースが見つかりません: ${FMGEN_DIR}\n"
        "  fmgen008.lzh を展開し、中身一式 (opna.cpp 等) を\n"
        "  ${FMGEN_DIR} に配置してください。")
endif()

# -------------------------------------------------------
#  nlohmann/json (sample_app の JSON パッチ読み込みに使用)
#  git submodule add https://github.com/nlohmann/json extern/nlohmann_json
# -------------------------------------------------------
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/extern/nlohmann_json/single_include" CACHE PATH "nlohmann/json include dir")

# =======================================================
#  ターゲット 1: FmGenEngineApi.dll (fmgen バックエンド)
# =======================================================
add_library(FmEngineApi SHARED
    src/FmGenEngineApi.cpp
    # fmgen ソース一式 (無改造)
    ${FMGEN_DIR}/fmgen.cpp
    ${FMGEN_DIR}/opna.cpp
    ${FMGEN_DIR}/opm.cpp
    ${FMGEN_DIR}/psg.cpp
    ${FMGEN_DIR}/fmtimer.cpp
    ${FMGEN_DIR}/file.cpp
)

# CMake 内部ターゲット名は FmEngineApi のままだが、
# 出力ファイル名は FmGenEngineApi.dll / FmGenEngineApi.lib にする。
# これにより YMEngine の FmEngineApi.dll と共存・切り替えが可能になる。
set_target_properties(FmEngineApi PROPERTIES OUTPUT_NAME "FmGenEngineApi")

target_include_directories(FmEngineApi PRIVATE
    src/
    ${FMGEN_DIR}
    ${CMAKE_SOURCE_DIR}/extern
)

# fmgen.h は types.h (uint/uint8/int32 等の typedef) を自身では
# include しない (fmgen.h 内で `//#include "types.h"` とコメントアウト
# されている) ため、fmgen 関連の全翻訳単位に対して types.h を
# 強制インクルードする。fmgen 本体ソースは無改造のまま解決できる。
if (MSVC)
    set(FMGEN_FORCE_INCLUDE /FI"${FMGEN_DIR}/types.h")
else()
    set(FMGEN_FORCE_INCLUDE -include "${FMGEN_DIR}/types.h")
endif()
target_compile_options(FmEngineApi PRIVATE ${FMGEN_FORCE_INCLUDE})

# FMENGINE_EXPORTS → FmGenEngineApi.cpp 内で dllexport になる
target_compile_definitions(FmEngineApi PRIVATE FMENGINE_EXPORTS)

# .def ファイルでエクスポートシンボルを明示 (MSVC)
if (MSVC)
    set_target_properties(FmEngineApi PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/src/FmGenEngineApi.def\""
    )
endif()

# WASAPI / COM リンク
target_link_libraries(FmEngineApi PRIVATE
    ole32
    uuid
    avrt
)

# バージョン情報を DLL に埋め込む (Windows リソース)
if (MSVC)
    target_sources(FmEngineApi PRIVATE src/FmGenEngineApi.rc)
endif()

# MSVC 最適化・警告設定
# fmgen 本体は古い C++ コードのため警告は抑制気味にする
# (オリジナルソースは無改造方針のため、警告修正のための改変はしない)
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

# MinGW / Clang
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FmEngineApi PRIVATE -O2)
    target_compile_options(FmEngineApi PRIVATE -fvisibility=hidden)
    # ソース (fmgen 本体含む) は UTF-8 で統一されているため、
    # 入力・実行時文字セットを明示的に UTF-8 にする (/utf-8 相当)。
    target_compile_options(FmEngineApi PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    # fmgen は C++03 時代のソースで、{ 0, 1, -1, 0 } のような
    # uint8 配列初期化に負数リテラルを使っている箇所がある。
    # C++11 以降の list-initialization では narrowing conversion が
    # 規格上 ill-formed (GCC/Clang はデフォルトでエラー) になるため、
    # fmgen 本体ソースに限り警告に格下げする。
    # (MSVC は元々これを警告止まりとして扱うため未対応で問題ない)
    set_source_files_properties(
        ${FMGEN_DIR}/fmgen.cpp
        ${FMGEN_DIR}/opna.cpp
        ${FMGEN_DIR}/opm.cpp
        ${FMGEN_DIR}/psg.cpp
        ${FMGEN_DIR}/fmtimer.cpp
        ${FMGEN_DIR}/file.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-narrowing"
    )
endif()

# MinGW は PKEY_Device_FriendlyName 等の PROPERTYKEY/GUID 定数を
# 静的ライブラリとして提供しないため、INITGUID で定義側にまわす。
# KSDATAFORMAT_SUBTYPE_* は libksguid に実体があるため追加リンクする。
# (MSVC では uuid.lib に含まれているため不要・無関係)
if (MINGW)
    target_compile_definitions(FmEngineApi PRIVATE INITGUID)
    target_link_libraries(FmEngineApi PRIVATE ksguid)
endif()

# =======================================================
#  ターゲット 2: sample_app.exe  (使用例。YMEngine 版を流用)
#  DLL の .lib にリンクする形でビルド
# =======================================================
add_executable(sample_app src/main.cpp)

target_include_directories(sample_app PRIVATE
    src/
    ${NLOHMANN_DIR}
)

target_link_libraries(sample_app PRIVATE FmEngineApi)

if (MSVC)
    set_target_properties(sample_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    target_compile_options(sample_app PRIVATE /W3 /wd4100 /utf-8)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sample_app PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# patches/ ディレクトリの JSON ファイルをビルド出力ディレクトリにコピー
set(PATCHES_SRC_DIR "${CMAKE_SOURCE_DIR}/patches")
if (EXISTS "${PATCHES_SRC_DIR}")
    file(GLOB PATCH_FILES "${PATCHES_SRC_DIR}/*.json")
    foreach(PATCH_FILE ${PATCH_FILES})
        add_custom_command(TARGET sample_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:sample_app>/patches"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PATCH_FILE}"
                "$<TARGET_FILE_DIR:sample_app>/patches/"
            COMMENT "Copying ${PATCH_FILE}"
        )
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/patches")
    foreach(PATCH_FILE ${PATCH_FILES})
        configure_file("${PATCH_FILE}" "${CMAKE_BINARY_DIR}/bin/patches/" COPYONLY)
    endforeach()
else()
    message(WARNING "patches/ directory not found at ${PATCHES_SRC_DIR}")
endif()

# =======================================================
#  インストール規則 (任意)
#  cmake --install build --prefix install_dir
# =======================================================
install(TARGETS FmEngineApi
    RUNTIME DESTINATION bin          # .dll
    LIBRARY DESTINATION lib          # .so (未使用、Windows専用のため)
    ARCHIVE DESTINATION lib          # .lib (import library)
)
install(FILES src/FmGenEngineApi.h DESTINATION include)
