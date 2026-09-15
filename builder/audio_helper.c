/* SPDX-License-Identifier: MIT
 * Uses the pinned dr_libs in mods/audio/vendor; their licenses remain in headers.
 * This is a desktop helper, not XZ firmware. Output PCM is streamed in 4096 frames.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#ifdef _WIN32
static wchar_t *wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *out = count ? malloc((size_t)count * sizeof(*out)) : NULL;
    if (out) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, count);
    return out;
}
#endif
static FILE *open_path(const char *path, int output) {
#ifdef _WIN32
    wchar_t *w = wide(path);
    if (!w) return NULL;
    int fd = _wopen(w, output ? _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY : _O_RDONLY | _O_BINARY, 0600);
    free(w);
    if (fd < 0) return NULL;
    FILE *f = _fdopen(fd, output ? "wb" : "rb");
    if (!f) _close(fd);
#else
    int fd = open(path, output ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY, 0600);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, output ? "wb" : "rb");
    if (!f) close(fd);
#endif
    return f;
}
static size_t read_bytes(void *context, void *buffer, size_t size) { return fread(buffer, 1, size, context); }
static drflac_bool32 flac_seek(void *context, int offset, drflac_seek_origin origin) {
    return fseek(context, offset, origin == DRFLAC_SEEK_SET ? SEEK_SET : origin == DRFLAC_SEEK_END ? SEEK_END : SEEK_CUR) == 0;
}
static drwav_bool32 wav_seek(void *context, int offset, drwav_seek_origin origin) {
    return fseek(context, offset, origin == DRWAV_SEEK_SET ? SEEK_SET : origin == DRWAV_SEEK_END ? SEEK_END : SEEK_CUR) == 0;
}
static drflac_bool32 flac_tell(void *context, drflac_int64 *position) { *position = ftell(context); return *position >= 0; }
static drwav_bool32 wav_tell(void *context, drwav_int64 *position) { *position = ftell(context); return *position >= 0; }
static void le32(unsigned char *out, uint32_t value) { for (int i=0;i<4;i++) out[i]=(unsigned char)(value>>(8*i)); }
static int run(int argc, char **argv) {
    int convert = argc == 4 && !strcmp(argv[1], "convert");
    if (!convert && !(argc == 3 && !strcmp(argv[1], "inspect"))) {
        fprintf(stderr, "Usage: xz-audio-helper inspect INPUT | convert INPUT NEW.wav\n"); return 2;
    }
    FILE *input = open_path(argv[2], 0), *output = NULL;
    drflac *flac = NULL;
    drwav wav;
    int wav_open = 0, okay = 0;
    unsigned char magic[4];
    uint64_t frames = 0, expected = 0;
    unsigned rate = 0, channels = 0, bits = 0;
    int16_t pcm[4096*2];
    const char *format = NULL;
    if (!input || fread(magic, 1, 4, input) != 4 || fseek(input, 0, SEEK_SET)) goto done;
    if (!memcmp(magic, "fLaC", 4)) {
        flac = drflac_open(read_bytes, flac_seek, flac_tell, input, NULL);
        if (!flac) goto done;
        expected=flac->totalPCMFrameCount; rate=flac->sampleRate; channels=flac->channels; bits=flac->bitsPerSample;
        format="flac";
    } else if (!memcmp(magic, "RIFF", 4)) {
        wav_open = drwav_init(&wav, read_bytes, wav_seek, wav_tell, input, NULL);
        if (!wav_open || wav.translatedFormatTag != DR_WAVE_FORMAT_PCM) goto done;
        expected=wav.totalPCMFrameCount; rate=wav.sampleRate; channels=wav.channels; bits=wav.bitsPerSample;
        format="wav";
    } else goto done;
    if (rate!=44100 || channels!=2 || bits!=16 || !expected || expected > (128u*1024u*1024u)/8) goto done;
    if (convert) {
        output = open_path(argv[3], 1);
        if (!output) goto done;
        unsigned char header[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0";
        le32(header+4, (uint32_t)(expected*4+36)); le32(header+24,44100); le32(header+28,176400);
        header[32]=4; header[34]=16; memcpy(header+36,"data",4); le32(header+40,(uint32_t)(expected*4));
        if (fwrite(header,1,44,output)!=44) goto done;
    }
    for (;;) {
        uint64_t got = flac ? drflac_read_pcm_frames_s16(flac,4096,pcm) : drwav_read_pcm_frames_s16(&wav,4096,pcm);
        if (!got) break;
        frames += got;
        if (frames > expected || (output && fwrite(pcm,4,(size_t)got,output)!=got)) goto done;
    }
    okay = frames==expected && !ferror(input);
done:
    if (flac) drflac_close(flac);
    if (wav_open) drwav_uninit(&wav);
    if (input) fclose(input);
    if (output && fclose(output)) okay=0;
    if (!okay) {
        if (output) {
#ifdef _WIN32
            wchar_t *name=wide(argv[3]);
            if (name) { _wremove(name); free(name); }
#else
            remove(argv[3]);
#endif
        }
        fprintf(stderr,"Invalid, truncated, unsupported or oversized audio, or output already exists\n"); return 1;
    }
    printf("{\"format\":\"%s\",\"frames\":%llu,\"sample_rate\":%u,\"channels\":%u,\"sample_width\":2,\"pcm_bytes\":%llu}\n",
           convert ? "wav" : format,(unsigned long long)frames,rate,channels,(unsigned long long)(frames*4));
    return 0;
}
int main(int argc, char **argv) {
#ifdef _WIN32
    int count;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    char **utf8 = calloc((size_t)count, sizeof(*utf8));
    if (!utf8) { LocalFree(args); return 2; }
    for (int i=0;i<count;i++) {
        int size=WideCharToMultiByte(CP_UTF8,0,args[i],-1,NULL,0,NULL,NULL);
        utf8[i]=malloc((size_t)size);
        if (!utf8[i]) return 2;
        WideCharToMultiByte(CP_UTF8,0,args[i],-1,utf8[i],size,NULL,NULL);
    }
    int result=run(count,utf8);
    for (int i=0;i<count;i++) free(utf8[i]);
    free(utf8); LocalFree(args); return result;
#else
    return run(argc,argv);
#endif
}
