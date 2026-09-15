/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_MOD_STEMS_H
#define XZ_MOD_STEMS_H
#include <stddef.h>
#include <stdint.h>

#define XZ_STEM_PATH_MAX 4096
#define XZ_STEM_UPLOAD_RATE 44100

enum xz_stem_result { XZ_STEM_OK = 0, XZ_STEM_MISSING = 1, XZ_STEM_INVALID = -1, XZ_STEM_IO = -2 };
enum xz_stem_decode_result {
    XZ_STEM_DECODE_OK = 0, XZ_STEM_DECODE_INVALID = -1,
    XZ_STEM_DECODE_BUDGET = -2, XZ_STEM_DECODE_RATE = -3,
    XZ_STEM_DECODE_CANCELLED = -4
};
struct xz_stem_entry {
    char harmonics[XZ_STEM_PATH_MAX];
    char vocals[XZ_STEM_PATH_MAX];
    int64_t upload_frames;
    float harmonics_gain;
    float vocals_gain;
};

/* Worker only. Frames MUST use upstream's decoded 44100 Hz length, including
 * decoder padding. This never writes to source tracks or their cache. */
int xz_stem_key(const char *track, int64_t upload_frames, char key[17]);
/* Optional app-owned selection, outside upstream stemd-cache. Missing means
 * use unique-model discovery; malformed selections must not silently fall back. */
int xz_stem_cache_choice(const char *volume, const char *track,
                         int64_t upload_frames, char *id, size_t capacity);
int xz_stem_cache_lookup(const char *volume, const char *separation_id,
                         const char *track, int64_t upload_frames,
                         struct xz_stem_entry *out);

/* The adapter owns PCM lifetime and must resample both parts to the stock
 * reader's timeline before publication. All lengths count stereo frames.
 * Gain fields are the normalization gains read from the upstream meta file. */
struct xz_stem_pcm {
    const int16_t *harmonics;
    const int16_t *vocals;
    int64_t frames;
    float harmonics_gain;
    float vocals_gain;
};
struct xz_stem_levels { float drums, harmonics, vocals; };

/* Audio thread. No allocation, I/O or locks. Call AFTER the original source
 * read and BEFORE stock stretching. Unity and uncovered samples stay bit exact.
 * Returns the number of covered frames changed. Invalid input leaves dst alone. */
size_t xz_stem_mix(float *dst, size_t frames, int64_t position,
                   const struct xz_stem_pcm *pcm,
                   struct xz_stem_levels levels);

struct xz_stem_decoded {
    struct xz_stem_pcm pcm;
    uint32_t sample_rate;
    size_t pcm_bytes;
};
typedef int (*xz_stem_cancel_fn)(void *context);

/* Worker only. Decode both original cached files into bounded resident s16 PCM.
 * The cap applies to the combined two-part PCM allocation, excluding decoder
 * state. Require stereo and exactly the measured stock reader rate. This avoids
 * silently moving cached samples onto the wrong timeline. No resampling occurs.
 * out must be empty; release only after audio readers have stopped using it. */
int xz_stem_decode(const struct xz_stem_entry *entry, uint32_t reader_rate,
                    size_t max_pcm_bytes, xz_stem_cancel_fn cancelled,
                    void *context, struct xz_stem_decoded *out);
void xz_stem_decoded_free(struct xz_stem_decoded *decoded);
#endif
