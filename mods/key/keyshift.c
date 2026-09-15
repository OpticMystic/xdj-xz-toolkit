/* SPDX-License-Identifier: MPL-2.0
 * Adapted from rx3_pitch_shift.h. See PROVENANCE.md. */
#include "keyshift.h"
#include <stddef.h>
#include <string.h>

#define XZ_KEY_GRAIN_UP 512u
#define XZ_KEY_GRAIN_DOWN 2048u
#define XZ_KEY_TABLE 1024u
#define XZ_KEY_MASK (XZ_KEY_SHIFT_HISTORY_FRAMES - 1u)
#define XZ_KEY_GLIDE 0.002f
#define XZ_KEY_MARGIN 2.0f
#define XZ_KEY_GAIN (2.0f / (float)XZ_KEY_SHIFT_HEADS)
#define XZ_KEY_ALIGN_MAX 768u
#define XZ_KEY_ALIGN_WINDOW 256u
#define XZ_KEY_ALIGN_STRIDE 4u

static const float semitone_ratio[25] = {
    0.500000f, 0.529732f, 0.561231f, 0.594604f, 0.629961f, 0.667420f,
    0.707107f, 0.749154f, 0.793701f, 0.840896f, 0.890899f, 0.943874f,
    1.000000f,
    1.059463f, 1.122462f, 1.189207f, 1.259921f, 1.334840f, 1.414214f,
    1.498307f, 1.587401f, 1.681793f, 1.781797f, 1.887749f, 2.000000f
};

static float key_cos(float x)
{
    float square = x * x;
    float term = 1.0f;
    float sum = 1.0f;
    unsigned n;
    for (n = 1; n <= 8u; ++n) {
        term *= -square / (float)((2u * n - 1u) * (2u * n));
        sum += term;
    }
    return sum;
}

static void build_hann(struct xz_key_shift *state)
{
    const float two_pi = 6.28318530718f;
    unsigned i;
    for (i = 0; i <= XZ_KEY_TABLE; ++i) {
        float fraction = (float)i / (float)XZ_KEY_TABLE;
        float angle = two_pi * fraction;
        if (angle > 3.14159265359f) angle -= two_pi;
        state->hann[i] = 0.5f - 0.5f * key_cos(angle);
    }
}

static float hann(const struct xz_key_shift *state, float fraction)
{
    float scaled = fraction * (float)XZ_KEY_TABLE;
    unsigned index = (unsigned)scaled;
    if (index >= XZ_KEY_TABLE) index = XZ_KEY_TABLE - 1u;
    float blend = scaled - (float)index;
    return state->hann[index] +
        (state->hann[index + 1u] - state->hann[index]) * blend;
}

static float cubic(float a, float b, float c, float d, float t)
{
    float c0 = b;
    float c1 = 0.5f * (c - a);
    float c2 = a - 2.5f * b + 2.0f * c - 0.5f * d;
    float c3 = 0.5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static float mono(const struct xz_key_shift *state, int64_t position)
{
    uint32_t index = (uint32_t)position & XZ_KEY_MASK;
    return state->history[index * 2u] + state->history[index * 2u + 1u];
}

static float align_head(const struct xz_key_shift *state, int64_t outgoing,
                        int64_t incoming)
{
    int64_t best_lag = 0;
    float best_score = -1.0e30f;
    unsigned lag;
    for (lag = 0; lag < XZ_KEY_ALIGN_MAX; lag += XZ_KEY_ALIGN_STRIDE) {
        float score = 0.0f;
        unsigned i;
        for (i = 0; i < XZ_KEY_ALIGN_WINDOW; i += XZ_KEY_ALIGN_STRIDE)
            score += mono(state, outgoing + (int64_t)i) *
                     mono(state, incoming - (int64_t)lag + (int64_t)i);
        if (score > best_score) {
            best_score = score;
            best_lag = (int64_t)lag;
        }
    }
    if (best_score <= 0.0f) return 0.0f;
    {
        int64_t low = best_lag - (int64_t)XZ_KEY_ALIGN_STRIDE;
        int64_t high = best_lag + (int64_t)XZ_KEY_ALIGN_STRIDE;
        int64_t fine_lag = best_lag;
        float fine_score = -1.0e30f;
        int64_t candidate;
        if (low < 0) low = 0;
        if (high > (int64_t)XZ_KEY_ALIGN_MAX) high = XZ_KEY_ALIGN_MAX;
        for (candidate = low; candidate <= high; ++candidate) {
            float score = 0.0f;
            unsigned i;
            for (i = 0; i < XZ_KEY_ALIGN_WINDOW; ++i)
                score += mono(state, outgoing + (int64_t)i) *
                         mono(state, incoming - candidate + (int64_t)i);
            if (score > fine_score) {
                fine_score = score;
                fine_lag = candidate;
            }
        }
        return (float)fine_lag;
    }
}

static int active(const struct xz_key_shift *state)
{
    float error = state->ratio - 1.0f;
    if (error < 0.0f) error = -error;
    return state->target != 1.0f || error > 0.0001f;
}

static void absorb(struct xz_key_shift *state, const float *stereo,
                   uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; ++i) {
        uint32_t slot = (uint32_t)state->written & XZ_KEY_MASK;
        state->history[slot * 2u] = stereo[i * 2u];
        state->history[slot * 2u + 1u] = stereo[i * 2u + 1u];
        ++state->written;
    }
    state->phase = 0.0f;
    state->ratio = state->target;
    state->align[0] = state->align[1] = 0.0f;
}

void xz_key_shift_init(struct xz_key_shift *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    build_hann(state);
    state->ratio = 1.0f;
    state->target = 1.0f;
    state->grain = (float)XZ_KEY_GRAIN_DOWN;
    state->initialized = 1;
}

int xz_key_shift_set_semitones(struct xz_key_shift *state, int semitones)
{
    if (!state || !state->initialized ||
        semitones < XZ_KEY_SHIFT_MIN_SEMITONES ||
        semitones > XZ_KEY_SHIFT_MAX_SEMITONES) return -1;
    state->semitones = semitones;
    state->target = semitone_ratio[semitones + 12];
    state->grain = state->target > 1.0f ?
        (float)XZ_KEY_GRAIN_UP : (float)XZ_KEY_GRAIN_DOWN;
    return 0;
}

int xz_key_shift_process(struct xz_key_shift *state, float *stereo,
                         uint32_t frames)
{
    uint32_t i;
    if (!state || !state->initialized || (frames && !stereo) ||
        frames > XZ_KEY_SHIFT_MAX_BLOCK_FRAMES) return -1;
    if (!frames) return 0;
    if (!active(state)) {
        absorb(state, stereo, frames);
        return 0;
    }
    for (i = 0; i < frames; ++i) {
        uint32_t slot = (uint32_t)state->written & XZ_KEY_MASK;
        float left = 0.0f;
        float right = 0.0f;
        unsigned head;
        state->history[slot * 2u] = stereo[i * 2u];
        state->history[slot * 2u + 1u] = stereo[i * 2u + 1u];
        ++state->written;
        for (head = 0; head < XZ_KEY_SHIFT_HEADS; ++head) {
            float fraction = state->phase +
                (float)head / (float)XZ_KEY_SHIFT_HEADS;
            float delay, remainder, t, window;
            uint64_t whole;
            int64_t base;
            unsigned channel;
            while (fraction >= 1.0f) fraction -= 1.0f;
            delay = XZ_KEY_MARGIN + fraction * state->grain + state->align[head];
            whole = (uint64_t)delay;
            remainder = delay - (float)whole;
            base = (int64_t)state->written - 2 - (int64_t)whole;
            t = 1.0f - remainder;
            window = hann(state, fraction);
            for (channel = 0; channel < 2u; ++channel) {
                uint32_t i0 = (uint32_t)(base - 1) & XZ_KEY_MASK;
                uint32_t i1 = (uint32_t)base & XZ_KEY_MASK;
                uint32_t i2 = (uint32_t)(base + 1) & XZ_KEY_MASK;
                uint32_t i3 = (uint32_t)(base + 2) & XZ_KEY_MASK;
                float value = cubic(state->history[i0 * 2u + channel],
                    state->history[i1 * 2u + channel],
                    state->history[i2 * 2u + channel],
                    state->history[i3 * 2u + channel], t);
                if (channel == 0u) left += window * value;
                else right += window * value;
            }
        }
        stereo[i * 2u] = left * XZ_KEY_GAIN;
        stereo[i * 2u + 1u] = right * XZ_KEY_GAIN;
        {
            float previous = state->phase;
            state->phase += (1.0f - state->ratio) / state->grain;
            while (state->phase >= 1.0f) state->phase -= 1.0f;
            while (state->phase < 0.0f) state->phase += 1.0f;
            for (head = 0; head < XZ_KEY_SHIFT_HEADS; ++head) {
                float offset = (float)head / (float)XZ_KEY_SHIFT_HEADS;
                float before = previous + offset;
                float after = state->phase + offset;
                int restarted;
                while (before >= 1.0f) before -= 1.0f;
                while (after >= 1.0f) after -= 1.0f;
                restarted = state->ratio > 1.0f ? after > before : after < before;
                if (restarted) {
                    unsigned other = head == 0u ? 1u : 0u;
                    float other_fraction = state->phase +
                        (float)other / (float)XZ_KEY_SHIFT_HEADS;
                    int64_t outgoing, incoming;
                    while (other_fraction >= 1.0f) other_fraction -= 1.0f;
                    outgoing = (int64_t)state->written - 2 -
                        (int64_t)(XZ_KEY_MARGIN + other_fraction * state->grain +
                                  state->align[other]);
                    incoming = (int64_t)state->written - 2 -
                        (int64_t)(XZ_KEY_MARGIN + after * state->grain);
                    state->align[head] = align_head(state, outgoing, incoming);
                }
            }
        }
        state->ratio += (state->target - state->ratio) * XZ_KEY_GLIDE;
    }
    return 0;
}
