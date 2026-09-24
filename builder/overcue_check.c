/* SPDX-License-Identifier: MIT */
#include "../mods/audio/overcue.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char **argv) {
    const char *source = argc == 2 ? argv[1] : NULL;
#ifdef _WIN32
    int count = 0;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count);
    char utf8[4096];
    if (wide && count == 2 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide[1], -1, utf8, sizeof(utf8), NULL, NULL)) source = utf8;
    else source = NULL;
    if (wide) LocalFree(wide);
#endif
    if (!source) { fputs("Choose one original track on an OverCue USB.\n", stderr); return 2; }
    struct xz_oc_assets assets;
    char error[256];
    if (xz_oc_open(source, &assets, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); return 1;
    }
    int16_t *samples = malloc(XZ_OC_PAGE);
    if (!samples) { xz_oc_close(&assets); return 1; }
    unsigned files = 0;
    for (unsigned mask = 1; mask <= 7; mask++) {
        struct xz_oc_file *file = xz_oc_mix_file(&assets, mask);
        if (!file || !file->file) {
            fputs("All seven OverCue prepared mixes are required.\n", stderr);
            free(samples); xz_oc_close(&assets); return 1;
        }
        for (uint64_t first = 0; first < assets.frames; first += XZ_OC_PAGE / 4) {
            size_t frames = assets.frames - first < XZ_OC_PAGE / 4
                ? (size_t)(assets.frames - first) : XZ_OC_PAGE / 4;
            if (xz_oc_read(file, (int64_t)first, frames, samples)) {
                fputs("OverCue audio page failed decompression or checksum verification.\n", stderr);
                free(samples); xz_oc_close(&assets); return 1;
            }
        }
        files++;
    }
    printf("{\"format\":\"overcue-stems/4\",\"sample_rate\":96000,\"channels\":2,"
           "\"frames\":%llu,\"verified_mixes\":%u,\"all_pages_verified\":true,"
           "\"source_identity_verified\":true,\"audio_alignment_verified\":false}\n",
           (unsigned long long)assets.frames, files);
    free(samples); xz_oc_close(&assets); return 0;
}
