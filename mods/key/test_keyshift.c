/* SPDX-License-Identifier: MPL-2.0 */
#ifdef NDEBUG
#error Key-shift acceptance requires active assertions
#endif
#include "keyshift.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define TEST_FRAMES XZ_KEY_SHIFT_MAX_BLOCK_FRAMES

static struct xz_key_shift states[4];
static float capture[XZ_KEY_SHIFT_SAMPLE_RATE * 2u];

static void tone(float *out, uint32_t frames, double *phase,
                 float left_hz, float right_hz)
{
    uint32_t i;
    for (i = 0; i < frames; ++i) {
        out[i * 2u] = sinf((float)(*phase * left_hz * 2.0 * PI_F));
        out[i * 2u + 1u] = sinf((float)(*phase * right_hz * 2.0 * PI_F));
        *phase += 1.0 / XZ_KEY_SHIFT_SAMPLE_RATE;
    }
}

static double dominant_frequency(const float *stereo, uint32_t frames,
                                 unsigned channel, double expected)
{
    double best_frequency = 0, best_power = -1;
    double frequency;
    for (frequency = expected - 30.0; frequency <= expected + 30.0;
         frequency += 0.25) {
        double coefficient = 2.0 * cos(2.0 * 3.14159265358979323846 *
                                       frequency / XZ_KEY_SHIFT_SAMPLE_RATE);
        double previous = 0, before_previous = 0;
        uint32_t i;
        for (i = 0; i < frames; ++i) {
            double current = stereo[i * 2u + channel] +
                             coefficient * previous - before_previous;
            before_previous = previous;
            previous = current;
        }
        {
            double power = previous * previous + before_previous * before_previous -
                           coefficient * previous * before_previous;
            if (power > best_power) {
                best_power = power;
                best_frequency = frequency;
            }
        }
    }
    return best_frequency;
}

static void bypass_and_bounds(void)
{
    struct guarded {
        uint32_t before[4];
        float audio[TEST_FRAMES * 2u];
        uint32_t after[4];
    } block, original;
    struct xz_key_shift snapshot;
    uint32_t i;
    memset(&block, 0, sizeof(block));
    for (i = 0; i < 4; ++i) block.before[i] = block.after[i] = 0xa5a50000u + i;
    for (i = 0; i < TEST_FRAMES * 2u; ++i) block.audio[i] = (float)((int)i - 400) / 401.0f;
    original = block;
    xz_key_shift_init(&states[0]);
    assert(xz_key_shift_process(&states[0], block.audio, TEST_FRAMES) == 0);
    assert(!memcmp(&block, &original, sizeof(block)));
    assert(states[0].written == TEST_FRAMES);
    assert(xz_key_shift_process(&states[0], NULL, 0) == 0);
    assert(xz_key_shift_process(&states[0], block.audio, TEST_FRAMES + 1u) == -1);
    assert(!memcmp(&block, &original, sizeof(block)));
    snapshot = states[0];
    assert(xz_key_shift_set_semitones(&states[0], -13) == -1);
    assert(xz_key_shift_set_semitones(&states[0], 13) == -1);
    assert(!memcmp(&snapshot, &states[0], sizeof(snapshot)));
    for (int semitone = -12; semitone <= 12; ++semitone) {
        double expected = pow(2.0, semitone / 12.0);
        assert(xz_key_shift_set_semitones(&states[0], semitone) == 0);
        assert(fabs(states[0].target - expected) < 0.000002);
    }
    assert(xz_key_shift_process(NULL, block.audio, 1) == -1);
    puts("bit-exact neutral bypass, warm history, frame bounds and semitone bounds passed");
}

static void channel_isolation(void)
{
    float block[TEST_FRAMES * 2u];
    double phase = 0;
    unsigned iteration, i;
    xz_key_shift_init(&states[0]);
    for (iteration = 0; iteration < 100; ++iteration) {
        tone(block, TEST_FRAMES, &phase, 440, 0);
        for (i = 0; i < TEST_FRAMES; ++i) block[i * 2u + 1u] = 0;
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    }
    assert(xz_key_shift_set_semitones(&states[0], 7) == 0);
    for (iteration = 0; iteration < 80; ++iteration) {
        tone(block, TEST_FRAMES, &phase, 440, 0);
        for (i = 0; i < TEST_FRAMES; ++i) block[i * 2u + 1u] = 0;
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
        for (i = 0; i < TEST_FRAMES; ++i) assert(block[i * 2u + 1u] == 0);
    }
    puts("stereo channels do not bleed into each other");
}

static void partition_and_deck_isolation(void)
{
    float whole[TEST_FRAMES * 2u], split[TEST_FRAMES * 2u];
    float other[TEST_FRAMES * 2u], reference[TEST_FRAMES * 2u];
    const uint32_t pieces[] = {7, 31, 113, 2, 211, 148};
    double phase = 0;
    uint32_t offset = 0, i;
    xz_key_shift_init(&states[0]);
    xz_key_shift_init(&states[1]);
    xz_key_shift_init(&states[2]);
    assert(xz_key_shift_set_semitones(&states[0], 5) == 0);
    assert(xz_key_shift_set_semitones(&states[1], 5) == 0);
    assert(xz_key_shift_set_semitones(&states[2], -5) == 0);
    tone(whole, TEST_FRAMES, &phase, 440, 330);
    memcpy(split, whole, sizeof(whole));
    assert(xz_key_shift_process(&states[0], whole, TEST_FRAMES) == 0);
    for (i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
        assert(xz_key_shift_process(&states[1], split + offset * 2u, pieces[i]) == 0);
        offset += pieces[i];
    }
    assert(offset == TEST_FRAMES);
    assert(!memcmp(whole, split, sizeof(whole)));
    for (i = 0; i < 40; ++i) {
        tone(whole, TEST_FRAMES, &phase, 440, 330);
        memcpy(reference, whole, sizeof(whole));
        memset(other, 0, sizeof(other));
        assert(xz_key_shift_process(&states[0], whole, TEST_FRAMES) == 0);
        assert(xz_key_shift_process(&states[2], other, TEST_FRAMES) == 0);
        assert(xz_key_shift_process(&states[1], reference, TEST_FRAMES) == 0);
        assert(!memcmp(whole, reference, sizeof(whole)));
    }
    puts("arbitrary block partitions and two-deck state isolation passed");
}

static void smooth_zero_reset(void)
{
    float block[TEST_FRAMES * 2u], input[TEST_FRAMES * 2u];
    double phase = 0;
    float shifted;
    unsigned blocks = 0;
    xz_key_shift_init(&states[0]);
    for (unsigned i = 0; i < 100; ++i) {
        tone(block, TEST_FRAMES, &phase, 440, 440);
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    }
    assert(xz_key_shift_set_semitones(&states[0], 12) == 0);
    for (unsigned i = 0; i < 20; ++i) {
        tone(block, TEST_FRAMES, &phase, 440, 440);
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    }
    shifted = states[0].ratio;
    assert(shifted > 1.9f && shifted <= 2.0f);
    assert(xz_key_shift_set_semitones(&states[0], 0) == 0);
    tone(block, TEST_FRAMES, &phase, 440, 440);
    memcpy(input, block, sizeof(block));
    assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    assert(states[0].ratio < shifted && states[0].ratio > 1.0f);
    assert(memcmp(block, input, sizeof(block)) != 0);
    while (states[0].ratio - 1.0f > 0.0001f && blocks++ < 100u) {
        tone(block, TEST_FRAMES, &phase, 440, 440);
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    }
    assert(blocks < 100u);
    tone(block, TEST_FRAMES, &phase, 440, 440);
    memcpy(input, block, sizeof(block));
    assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    assert(states[0].ratio == 1.0f && !memcmp(block, input, sizeof(block)));
    puts("nonzero changes and zero reset glide before exact bypass");
}

static void frequency_case(int semitones, double expected)
{
    float block[TEST_FRAMES * 2u];
    double phase = 0;
    uint32_t written = 0;
    unsigned blocks, i;
    xz_key_shift_init(&states[0]);
    for (blocks = 0; blocks < 100; ++blocks) {
        tone(block, TEST_FRAMES, &phase, 440, 440);
        assert(xz_key_shift_process(&states[0], block, TEST_FRAMES) == 0);
    }
    assert(xz_key_shift_set_semitones(&states[0], semitones) == 0);
    for (blocks = 0; blocks < 360; ++blocks) {
        uint32_t n = TEST_FRAMES;
        tone(block, n, &phase, 440, 440);
        assert(xz_key_shift_process(&states[0], block, n) == 0);
        if (blocks >= 260 && written < XZ_KEY_SHIFT_SAMPLE_RATE) {
            uint32_t take = XZ_KEY_SHIFT_SAMPLE_RATE - written;
            if (take > n) take = n;
            for (i = 0; i < take; ++i) {
                capture[(written + i) * 2u] = block[i * 2u];
                capture[(written + i) * 2u + 1u] = block[i * 2u + 1u];
            }
            written += take;
        }
    }
    assert(written == XZ_KEY_SHIFT_SAMPLE_RATE);
    {
        double left = dominant_frequency(capture, written, 0, expected);
        double right = dominant_frequency(capture, written, 1, expected);
        printf("%+d semitones: left %.2f Hz, right %.2f Hz\n", semitones, left, right);
        fflush(stdout);
        assert(fabs(left - expected) < 3.0);
        assert(fabs(right - expected) < 3.0);
    }
}

int main(void)
{
    bypass_and_bounds();
    channel_isolation();
    partition_and_deck_isolation();
    smooth_zero_reset();
    frequency_case(12, 880.0);
    frequency_case(-12, 220.0);
    puts("PASS portable 44.1 kHz stereo key shifter; no native hook exercised");
    return 0;
}
