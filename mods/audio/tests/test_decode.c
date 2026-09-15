/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error Test assertions must be enabled: compile with -UNDEBUG
#endif
#include "stems.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int cancel(void *ctx) { (void)ctx; return 1; }

int main(int argc, char **argv)
{
    struct xz_stem_entry entry = {0};
    struct xz_stem_decoded decoded = {0};
    size_t i;
    assert(argc == 3 || argc == 4);
    snprintf(entry.harmonics, sizeof(entry.harmonics), "%s", argv[1]);
    snprintf(entry.vocals, sizeof(entry.vocals), "%s", argv[2]);
    entry.harmonics_gain = 0.5f;
    entry.vocals_gain = 0.75f;
    if (argc == 4) {
        assert(!strcmp(argv[3], "invalid"));
        assert(xz_stem_decode(&entry, 44100, 44100 * 8, NULL, NULL, &decoded) == XZ_STEM_DECODE_INVALID);
        assert(!decoded.pcm.harmonics && !decoded.pcm.vocals);
        return 0;
    }
    assert(xz_stem_decode(&entry, 44100, 44100 * 8, NULL, NULL, &decoded) == 0);
    assert(decoded.sample_rate == 44100 && decoded.pcm.frames == 44100);
    assert(decoded.pcm_bytes == 44100 * 8);
    for (i = 0; i < 44100 * 2; ++i) {
        int16_t expected = (int16_t)((int)(i % 60001) - 30000);
        assert(decoded.pcm.harmonics[i] == expected);
        assert(decoded.pcm.vocals[i] == expected);
    }
    xz_stem_decoded_free(&decoded);
    assert(!decoded.pcm.harmonics && !decoded.pcm.vocals);
    assert(xz_stem_decode(&entry, 44100, 44100 * 8 - 1, NULL, NULL, &decoded) == XZ_STEM_DECODE_BUDGET);
    assert(xz_stem_decode(&entry, 48000, 44100 * 8, NULL, NULL, &decoded) == XZ_STEM_DECODE_RATE);
    assert(xz_stem_decode(&entry, 44100, 44100 * 8, cancel, NULL, &decoded) == XZ_STEM_DECODE_CANCELLED);
    assert(!decoded.pcm.harmonics && !decoded.pcm.vocals);
    puts("real FLAC/WAV samples identical; memory cap, rate gate, cancellation passed");
    return 0;
}
