/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_AUDIO_RUNTIME_H
#define XZ_AUDIO_RUNTIME_H
#include "stems.h"
#include <stdint.h>

enum xz_audio_state {
    XZ_AUDIO_STOPPED, XZ_AUDIO_WAITING_TRACK, XZ_AUDIO_LOADING_TRACK,
    XZ_AUDIO_SOURCE_UNSUPPORTED, XZ_AUDIO_CACHE_MISSING,
    XZ_AUDIO_CACHE_AMBIGUOUS, XZ_AUDIO_DECODING, XZ_AUDIO_DECODE_FAILED,
    XZ_AUDIO_ALIGNMENT_BLOCKED, XZ_AUDIO_EXPERIMENTAL_READY,
    XZ_AUDIO_HOOK_FAILED, XZ_AUDIO_DISABLED, XZ_AUDIO_CACHE_CHOICE_INVALID
};
struct xz_audio_status {
    enum xz_audio_state state;
    uint32_t generation;
    uint32_t reader_rate;
    uint32_t reader_frames;
    uint32_t mixed_blocks;
    uint32_t skipped_blocks;
    size_t pcm_bytes;
    char path[1024];
};
int xz_audio_start(void);
void xz_audio_stop(void);
void xz_audio_set_enabled(int enabled);
void xz_audio_set_levels(int deck, struct xz_stem_levels levels);
int xz_audio_get_status(int deck, struct xz_audio_status *out);
/* Audio-thread lookup for a manager captured by the successful load snapshot.
 * Atomic 32-bit reads only. The returned generation changes only for a true
 * stream discontinuity: load, unload, or runtime stop. Stem enable/disable does
 * not invalidate the identity. */
int xz_audio_output_deck(const void *manager, uint32_t *rate_out,
                         uint32_t *generation_out);
const char *xz_audio_state_name(enum xz_audio_state state);
/* Host/target self-test for publication and load-generation cancellation.
 * Does not install hooks, read firmware or access any device. */
int xz_audio_lifecycle_self_test(void);
#endif
