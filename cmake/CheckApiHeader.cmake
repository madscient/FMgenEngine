# SPDX-License-Identifier: MIT
# Copyright (c) 2026 FmGenEngine contributors
# (FMEngineTest (MIT License) の cmake/CheckApiSymbols.cmake を基にした)
#
# src/FmEngineApi.h の検査。CMakeLists.txt から include するほか、単独でも
# 走らせられる:
#   cmake -P cmake/CheckApiHeader.cmake
#
# 1. ヘッダが正本の写しのままであること。
#    正本は https://github.com/madscient/FMEngineTest の include/FmEngineApi.h で、
#    写しは直接編集しない。正本が変わったら写し直し、下の
#    FMGEN_API_HEADER_SHA256 を新しい値に書き換える。
# 2. ヘッダが宣言する関数と、src/FmGenEngine.def の EXPORTS が同じ集合であること。
#    食い違うと、宣言はあるのに DLL に無い関数 (直接呼んだアプリがリンクで失敗
#    する) や、ヘッダに無いエクスポートができる。比べるのは名前だけ。

# 改行を LF にそろえた内容の SHA-256 (git の autocrlf に左右されないようにする)
set(FMGEN_API_HEADER_SHA256
    "8c2630b843d35d3e2f0f4cba82494b36303bb37f358261ed909abbf468fb282b")

function(fmgen_check_api_header)
    set(src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")

    file(READ "${src}/FmEngineApi.h" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(SHA256 actual "${text}")
    if (NOT actual STREQUAL FMGEN_API_HEADER_SHA256)
        message(FATAL_ERROR
            "src/FmEngineApi.h が、記録してある正本の写しと違います。\n"
            "  記録: ${FMGEN_API_HEADER_SHA256}\n"
            "  実際: ${actual}\n"
            "このファイルは FMEngineTest の include/FmEngineApi.h の写しです。"
            "直接編集せず、正本から写し直してください。正本の新しい版に"
            "差し替えたのなら、cmake/CheckApiHeader.cmake の "
            "FMGEN_API_HEADER_SHA256 を上の「実際」の値にしてください。")
    endif()

    # コメントの中に出てくる関数名は宣言ではない
    string(REGEX REPLACE "//[^\n]*" "" text "${text}")
    string(REGEX MATCHALL "FmEngine_[A-Za-z0-9]+" header_syms "${text}")

    file(READ "${src}/FmGenEngine.def" text)
    string(REGEX REPLACE ";[^\n]*" "" text "${text}")
    string(REGEX MATCHALL "FmEngine_[A-Za-z0-9]+" def_syms "${text}")

    # どちらかが空なら、下の差分は取り出しの失敗を「一致」と見せてしまう
    if (NOT header_syms OR NOT def_syms)
        message(FATAL_ERROR
            "CheckApiHeader: FmEngine_* の名前を、ヘッダか .def から取り出せません。")
    endif()

    list(REMOVE_DUPLICATES header_syms)
    list(REMOVE_DUPLICATES def_syms)
    set(only_header ${header_syms})
    list(REMOVE_ITEM only_header ${def_syms})
    set(only_def ${def_syms})
    list(REMOVE_ITEM only_def ${header_syms})

    if (only_header OR only_def)
        string(REPLACE ";" " " only_header "${only_header}")
        string(REPLACE ";" " " only_def "${only_def}")
        message(FATAL_ERROR
            "src/FmEngineApi.h の宣言と src/FmGenEngine.def の EXPORTS が食い違います。\n"
            "  ヘッダにだけある: ${only_header}\n"
            "  .def にだけある : ${only_def}")
    endif()

    list(LENGTH header_syms count)
    message(STATUS "FmEngineApi: ヘッダは正本の写しと一致し、.def と同じ ${count} 関数を宣言している")
endfunction()

fmgen_check_api_header()
