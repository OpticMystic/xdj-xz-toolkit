/* SPDX-License-Identifier: MIT */
#ifdef _WIN32
#include <stdio.h>
#include <windows.h>
static FILE *utf8_fopen(const char *path, const char *mode) {
    wchar_t wide_path[4096], wide_mode[16];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, 4096) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wide_mode, 16)) return NULL;
    return _wfopen(wide_path, wide_mode);
}
#define fopen utf8_fopen
#define WORD XZ_SHA256_WORD
#endif
#include "../mods/audio/overcue_file.c"
