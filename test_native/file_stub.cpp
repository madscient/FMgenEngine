// file_stub.cpp (Linux検証ビルド専用)
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 FmGenEngine contributors
// (詳細はリポジトリルートの LICENSE / README.md を参照)
//
// fmgen file.cpp (file.h の FileIO) の最小スタブ実装。
// 検証テストでは実際のファイル I/O を行わないため、
// 常に失敗 (Open() が false) を返すだけのダミー実装で十分。
// 本番 Windows ビルドでは本物の file.cpp (HANDLE ベース) を使用する。
#include "windows.h"  // winstub/windows.h (検証ビルド専用スタブ)
#include "file.h"

FileIO::FileIO() { flags = 0; }
FileIO::FileIO(const char*, uint) { flags = 0; }
FileIO::~FileIO() {}

bool FileIO::Open(const char*, uint) { return false; }
bool FileIO::CreateNew(const char*) { return false; }
bool FileIO::Reopen(uint) { return false; }
void FileIO::Close() {}

int32 FileIO::Read(void*, int32) { return -1; }
int32 FileIO::Write(const void*, int32) { return -1; }
bool FileIO::Seek(int32, SeekMethod) { return false; }
int32 FileIO::Tellp() { return -1; }
bool FileIO::SetEndOfFile() { return false; }
