/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_KEY_SHIFT_H
#define XZ_KEY_SHIFT_H

#include <stdint.h>

#define XZ_KEY_SHIFT_CHANNELS 2u
#define XZ_KEY_SHIFT_SAMPLE_RATE 44100u
#define XZ_KEY_SHIFT_MIN_SEMITONES (-12)
#define XZ_KEY_SHIFT_MAX_SEMITONES 12
#define XZ_KEY_SHIFT_MAX_BLOCK_FRAMES 512u
#define XZ_KEY_SHIFT_HISTORY_FRAMES 4096u
#define XZ_KEY_SHIFT_HEADS 2u
#define XZ_KEY_SHIFT_WINDOW_POINTS 1025u

/* One audio thread owns one state. The native adapter transfers control values
 * into it at block boundaries. No function here allocates, locks, or calls a
 * device API. Keep this object out of the audio thread's stack. */
struct xz_key_shift {
    float history[XZ_KEY_SHIFT_HISTORY_FRAMES * XZ_KEY_SHIFT_CHANNELS];
    float hann[XZ_KEY_SHIFT_WINDOW_POINTS];
    uint64_t written;
    float phase;
    float ratio;
    float target;
    float grain;
    float align[XZ_KEY_SHIFT_HEADS];
    int semitones;
    int initialized;
};

/* Clear history after a real stream discontinuity. This is not the musical
 * reset control; use set_semitones(state, 0) to glide back to unity. */
void xz_key_shift_init(struct xz_key_shift *state);

/* Accept one quantized semitone from -12 through 12. Return -1 and leave the
 * state unchanged for invalid input. Call only on the owning audio thread. */
int xz_key_shift_set_semitones(struct xz_key_shift *state, int semitones);

/* Process one sequential 44.1 kHz interleaved-stereo block in place. Zero and
 * neutral blocks remain bit exact while still warming history. */
int xz_key_shift_process(struct xz_key_shift *state, float *stereo,
                         uint32_t frames);

#endif
