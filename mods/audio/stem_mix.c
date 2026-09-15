/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Residual mixing adapted from cdj3k-mods package/deck/mods/stem/audio_mix.c,
 * e74e199603e2a25567950ca72997c38d17ada4e8. See PROVENANCE.md. */
#include "stems.h"
#include <math.h>

size_t xz_stem_mix(float *dst, size_t frames, int64_t position,
                   const struct xz_stem_pcm *pcm,
                   struct xz_stem_levels levels)
{
    size_t n, i;
    const int16_t *h, *v;
    float ch, cv;
    if (!dst || !pcm || !pcm->harmonics || !pcm->vocals || position < 0 ||
        position >= pcm->frames || frames > SIZE_MAX / (2 * sizeof(float)) ||
        (uint64_t)pcm->frames > SIZE_MAX / (2 * sizeof(int16_t)) ||
        !isfinite(pcm->harmonics_gain) || pcm->harmonics_gain <= 0 ||
        !isfinite(pcm->vocals_gain) || pcm->vocals_gain <= 0 ||
        !isfinite(levels.drums) || !isfinite(levels.harmonics) ||
        !isfinite(levels.vocals))
        return 0;
    if (levels.drums == 1 && levels.harmonics == 1 && levels.vocals == 1)
        return 0;
    n = (size_t)(pcm->frames - position);
    if (n > frames) n = frames;
    ch = (levels.harmonics - levels.drums) *
         ((1.0f / pcm->harmonics_gain) * (1.0f / 32767.0f));
    cv = (levels.vocals - levels.drums) *
         ((1.0f / pcm->vocals_gain) * (1.0f / 32767.0f));
    if (!isfinite(ch) || !isfinite(cv)) return 0;
    h = pcm->harmonics + (size_t)position * 2;
    v = pcm->vocals + (size_t)position * 2;
    for (i = 0; i < n * 2; ++i)
        dst[i] = levels.drums * dst[i] + ch * (float)h[i] + cv * (float)v[i];
    return n;
}
