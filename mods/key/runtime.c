/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include "keyshift.h"
#include "../audio/runtime.h"
#include "../runtime.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define XZ_KEY_DECKS 2
#define XZ_TSM_OPERATE UINT32_C(0x00076284)
#define XZ_TSM_ACTIVE_OFFSET 0x10u

typedef int32_t (*xz_tsm_operate_fn)(void *, int32_t, float *, int32_t, int32_t);

struct xz_key_deck {
    struct xz_key_shift shift;
    uint32_t generation;
    uint32_t rate;
    int applied_semitones;
    int have_last_end;
    int32_t last_end;
};

static struct xz_key_deck decks[XZ_KEY_DECKS];
static int desired_semitones[XZ_KEY_DECKS];
static int started;
static xz_tsm_operate_fn original_operate;

static uint32_t active_engine(const void *manager)
{
    return __atomic_load_n((const uint32_t *)((const unsigned char *)manager +
                           XZ_TSM_ACTIVE_OFFSET), __ATOMIC_RELAXED);
}

static int32_t operate_hook(void *manager, int32_t foreground_position,
                            float *output, int32_t frames,
                            int32_t background_position)
{
    uint32_t active_before = 0, rate = 0, generation = 0;
    int deck = -1, desired, eligible;
    struct xz_key_deck *state = NULL;
    eligible = __atomic_load_n(&started, __ATOMIC_ACQUIRE) && manager && output &&
        frames > 0 && frames <= (int32_t)XZ_KEY_SHIFT_MAX_BLOCK_FRAMES;
    if (eligible) {
        active_before = active_engine(manager);
        deck = xz_audio_output_deck(manager, &rate, &generation);
        if (deck >= 0 && deck < XZ_KEY_DECKS) state = &decks[deck];
    }
    int32_t result = original_operate(manager, foreground_position, output,
                                      frames, background_position);
    /* The stock no-active-DSP branch returns without writing output. Require a
     * stable nonzero engine and the same published owner snapshot across the
     * original call. Transition blocks are skipped and break continuity. */
    if (!eligible || !__atomic_load_n(&started, __ATOMIC_ACQUIRE) ||
        !state || !active_before ||
        active_before != active_engine(manager)) {
        if (state) state->have_last_end = 0;
        return result;
    }
    {
        uint32_t rate_after, generation_after;
        int deck_after = xz_audio_output_deck(manager, &rate_after,
                                              &generation_after);
        if (deck_after != deck || rate_after != rate ||
            generation_after != generation || rate != XZ_KEY_SHIFT_SAMPLE_RATE) {
            state->have_last_end = 0;
            return result;
        }
    }
    if (state->generation != generation || state->rate != rate) {
        xz_key_shift_init(&state->shift);
        state->generation = generation;
        state->rate = rate;
        state->applied_semitones = INT_MIN;
        state->have_last_end = 0;
    } else if (state->have_last_end && foreground_position != state->last_end) {
        /* Foreground position is the stream-continuity token. Background/slip
         * movement alone does not discard the warmed pitch history. */
        xz_key_shift_init(&state->shift);
        state->applied_semitones = INT_MIN;
        state->have_last_end = 0;
    }
    desired = __atomic_load_n(&desired_semitones[deck], __ATOMIC_ACQUIRE);
    if (desired != state->applied_semitones) {
        if (xz_key_shift_set_semitones(&state->shift, desired) != 0)
            return result;
        state->applied_semitones = desired;
    }
    (void)xz_key_shift_process(&state->shift, output, (uint32_t)frames);
    state->last_end = result;
    state->have_last_end = 1;
    return result;
}

int xz_key_set_desired_semitones(int deck, int semitones)
{
    if (deck < 0 || deck >= XZ_KEY_DECKS ||
        semitones < XZ_KEY_SHIFT_MIN_SEMITONES ||
        semitones > XZ_KEY_SHIFT_MAX_SEMITONES) return -1;
    __atomic_store_n(&desired_semitones[deck], semitones, __ATOMIC_RELEASE);
    return 0;
}

int xz_key_get_desired_semitones(int deck, int *semitones_out)
{
    if (deck < 0 || deck >= XZ_KEY_DECKS || !semitones_out) return -1;
    *semitones_out = __atomic_load_n(&desired_semitones[deck], __ATOMIC_ACQUIRE);
    return 0;
}

int xz_key_runtime_start(void)
{
    static const unsigned char guard[8] =
        {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    int i;
    if (__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return 0;
    if (!original_operate &&
        xz_hook_arm(XZ_TSM_OPERATE, guard, (void *)operate_hook,
                    (void **)&original_operate) != 0) return -1;
    for (i = 0; i < XZ_KEY_DECKS; ++i) {
        xz_key_shift_init(&decks[i].shift);
        decks[i].generation = 0;
        decks[i].rate = 0;
        decks[i].applied_semitones = INT_MIN;
        decks[i].have_last_end = 0;
    }
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    return 0;
}

void xz_key_runtime_stop(void)
{
    int i;
    __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
    for (i = 0; i < XZ_KEY_DECKS; ++i)
        __atomic_store_n(&desired_semitones[i], 0, __ATOMIC_RELEASE);
}
