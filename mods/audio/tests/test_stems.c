/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error Test assertions must be enabled: compile with -UNDEBUG
#endif
#include "stems.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void mix_tests(void)
{
    const int16_t h[] = {32767, 16384, -32767, 0};
    const int16_t v[] = {16384, -16384, 0, 32767};
    const float original[] = {0.8f, 0.3f, -0.9f, 0.5f, 0.7f, -0.2f};
    float out[6];
    struct xz_stem_pcm pcm = {h, v, 2, 0.5f, 1.0f};
    struct xz_stem_levels unity = {1, 1, 1};
    memcpy(out, original, sizeof(out));
    assert(xz_stem_mix(out, 3, 0, &pcm, unity) == 0);
    assert(memcmp(out, original, sizeof(out)) == 0);
    assert(xz_stem_mix(out, 3, 0, &pcm, (struct xz_stem_levels){0, 1, 0}) == 2);
    assert(out[0] == 2.0f); /* restore peaks above unity, without clipping */
    assert(out[2] == -2.0f);
    assert(memcmp(out + 4, original + 4, 2 * sizeof(float)) == 0);
    memcpy(out, original, sizeof(out));
    assert(xz_stem_mix(out, 2, 1, &pcm, (struct xz_stem_levels){0, 0, 1}) == 1);
    assert(out[0] == 0 && out[1] == 1);
    assert(memcmp(out + 2, original + 2, 4 * sizeof(float)) == 0);
    memcpy(out, original, sizeof(out));
    assert(xz_stem_mix(out, 3, 0, &pcm, (struct xz_stem_levels){1, 0, 0}) == 2);
    assert(fabsf(out[0] - (original[0] - 2.0f - 16384.0f / 32767.0f)) < 1e-6f);
    memcpy(out, original, sizeof(out));
    assert(xz_stem_mix(out, 3, -1, &pcm, unity) == 0);
    assert(xz_stem_mix(out, 3, INT64_MAX, &pcm, unity) == 0);
    pcm.harmonics_gain = NAN;
    assert(xz_stem_mix(out, 3, 0, &pcm, (struct xz_stem_levels){0, 0, 0}) == 0);
    assert(memcmp(out, original, sizeof(out)) == 0);
}

int main(int argc, char **argv)
{
    struct xz_stem_entry entry;
    char key[17];
    int rc;
    mix_tests();
    if (argc == 3 && !strcmp(argv[1], "key")) {
        rc = xz_stem_key(argv[2], 12345678, key);
        if (rc != XZ_STEM_OK) return 2;
        puts(key);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "lookup")) {
        rc = xz_stem_cache_lookup(argv[2], "test-model", argv[3], 12345678, &entry);
        if (!strcmp(argv[4], "valid")) {
            assert(rc == XZ_STEM_OK);
            assert(entry.upload_frames == 12345678);
            assert(entry.harmonics_gain == 0.5f && entry.vocals_gain == 0.75f);
            assert(strstr(entry.harmonics, "harmonics.flac"));
            assert(strstr(entry.vocals, "vocals.wav"));
        } else {
            assert(rc != XZ_STEM_OK);
            assert(!entry.harmonics[0] && !entry.vocals[0]);
        }
        assert(xz_stem_cache_lookup(argv[2], "../test-model", argv[3], 12345678,
                                    &entry) == XZ_STEM_INVALID);
        return 0;
    }
    puts("residual mix tests passed");
    return argc == 1 ? 0 : 2;
}
