// File.h
// [FmGenEngine] 大文字小文字を区別するファイルシステム (Linux/macOS 等) で
// MinGW クロスビルドする際の互換性のために追加したリダイレクトヘッダ。
// fmgen 本体は file.cpp 内で #include "File.h" (大文字F) としている一方、
// 実体ファイルは file.h (小文字) という名前で配布されている。
// Windows (NTFS) では大小文字を区別しないため本来は問題にならないが、
// 大小文字を区別する環境でも同様にビルドできるよう本ファイルを追加した。
// fmgen 本体ソース (file.h/file.cpp) には一切手を入れていない。
#include "file.h"
