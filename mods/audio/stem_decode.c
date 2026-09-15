/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "stems.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "vendor/dr_libs/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "vendor/dr_libs/dr_wav.h"

struct decoder {
    drflac *flac;
    drwav wav;
    int wav_open;
    uint64_t frames;
    uint32_t rate;
    unsigned channels;
};

static void close_decoder(struct decoder *d)
{
    if (d->flac) drflac_close(d->flac);
    if (d->wav_open) drwav_uninit(&d->wav);
}

static int open_decoder(const char *path, struct decoder *d)
{
    unsigned char magic[4];
    FILE *f = fopen(path, "rb");
    if (!f) return XZ_STEM_DECODE_INVALID;
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return XZ_STEM_DECODE_INVALID; }
    fclose(f);
    memset(d, 0, sizeof(*d));
    if (!memcmp(magic, "fLaC", 4)) {
        d->flac = drflac_open_file(path, NULL);
        if (!d->flac) return XZ_STEM_DECODE_INVALID;
        d->frames = d->flac->totalPCMFrameCount;
        d->rate = d->flac->sampleRate;
        d->channels = d->flac->channels;
    } else {
        if (!drwav_init_file(&d->wav, path, NULL)) return XZ_STEM_DECODE_INVALID;
        d->wav_open = 1;
        d->frames = d->wav.totalPCMFrameCount;
        d->rate = d->wav.sampleRate;
        d->channels = d->wav.channels;
    }
    if (!d->frames || d->channels != 2 || !d->rate) {
        close_decoder(d);
        memset(d, 0, sizeof(*d));
        return XZ_STEM_DECODE_INVALID;
    }
    return XZ_STEM_DECODE_OK;
}

static int decode_part(struct decoder *d, int16_t *pcm,
                       xz_stem_cancel_fn cancelled, void *context)
{
    uint64_t position = 0;
    while (position < d->frames) {
        uint64_t n = d->frames - position, read;
        if (cancelled && cancelled(context)) return XZ_STEM_DECODE_CANCELLED;
        if (n > 4096) n = 4096;
        read = d->flac ? drflac_read_pcm_frames_s16(d->flac, n, pcm + position * 2) :
                        drwav_read_pcm_frames_s16(&d->wav, n, pcm + position * 2);
        if (read != n) return XZ_STEM_DECODE_INVALID;
        position += n;
    }
    return XZ_STEM_DECODE_OK;
}

void xz_stem_decoded_free(struct xz_stem_decoded *decoded)
{
    if (!decoded) return;
    free((void *)decoded->pcm.harmonics);
    free((void *)decoded->pcm.vocals);
    memset(decoded, 0, sizeof(*decoded));
}

int xz_stem_decode(const struct xz_stem_entry *entry, uint32_t reader_rate,
                    size_t max_pcm_bytes, xz_stem_cancel_fn cancelled,
                    void *context, struct xz_stem_decoded *out)
{
    struct decoder h = {0}, v = {0};
    struct xz_stem_decoded decoded = {0};
    int rc;
    size_t bytes;
    if (!out) return XZ_STEM_DECODE_INVALID;
    memset(out, 0, sizeof(*out));
    if (!entry || !reader_rate || !isfinite(entry->harmonics_gain) ||
        entry->harmonics_gain <= 0 || !isfinite(entry->vocals_gain) ||
        entry->vocals_gain <= 0) return XZ_STEM_DECODE_INVALID;
    rc = open_decoder(entry->harmonics, &h);
    if (rc) goto done;
    rc = open_decoder(entry->vocals, &v);
    if (rc) goto done;
    if (h.rate != reader_rate || v.rate != reader_rate) {
        rc = XZ_STEM_DECODE_RATE; goto done;
    }
    /* No full-size temporary buffers. Decode directly into the published form. */
    if (h.frames != v.frames || h.frames > INT64_MAX) {
        rc = XZ_STEM_DECODE_INVALID; goto done;
    }
    if (h.frames > SIZE_MAX / 8 || h.frames > max_pcm_bytes / 8) {
        rc = XZ_STEM_DECODE_BUDGET; goto done;
    }
    bytes = (size_t)h.frames * 4;
    decoded.pcm.harmonics = malloc(bytes);
    decoded.pcm.vocals = malloc(bytes);
    if (!decoded.pcm.harmonics || !decoded.pcm.vocals) {
        rc = XZ_STEM_DECODE_BUDGET; goto done;
    }
    rc = decode_part(&h, (int16_t *)decoded.pcm.harmonics, cancelled, context);
    if (rc) goto done;
    rc = decode_part(&v, (int16_t *)decoded.pcm.vocals, cancelled, context);
    if (rc) goto done;
    decoded.pcm.frames = (int64_t)h.frames;
    decoded.pcm.harmonics_gain = entry->harmonics_gain;
    decoded.pcm.vocals_gain = entry->vocals_gain;
    decoded.sample_rate = reader_rate;
    decoded.pcm_bytes = bytes * 2;
    *out = decoded;
    memset(&decoded, 0, sizeof(decoded));
done:
    close_decoder(&h);
    close_decoder(&v);
    xz_stem_decoded_free(&decoded);
    return rc;
}
