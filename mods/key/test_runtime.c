/* SPDX-License-Identifier: MPL-2.0 */
#ifdef NDEBUG
#error Key runtime acceptance requires active assertions
#endif
#include "runtime.h"
#include "keyshift.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t (*operate_fn)(void *, int32_t, float *, int32_t, int32_t);
struct fake_manager { unsigned char bytes[0x20]; };
static struct fake_manager managers[2];
static uint32_t rates[2] = {44100, 44100};
static uint32_t generations[2] = {1, 10};
static int mapped[2] = {1, 1};
static operate_fn installed;
static unsigned hook_calls, original_calls;
static void *last_manager;
static int32_t last_foreground, last_frames, last_background;
static uint32_t mutate_active = UINT32_MAX;
static int mutate_generation_deck = -1;

static void set_active(struct fake_manager *manager, uint32_t value)
{
    memcpy(manager->bytes + 0x10, &value, sizeof(value));
}

static int32_t original(void *manager, int32_t foreground, float *output,
                        int32_t frames, int32_t background)
{
    (void)output;
    last_manager = manager;
    last_foreground = foreground;
    last_frames = frames;
    last_background = background;
    if (mutate_active != UINT32_MAX) {
        set_active((struct fake_manager *)manager, mutate_active);
        mutate_active = UINT32_MAX;
    }
    if (mutate_generation_deck >= 0) {
        ++generations[mutate_generation_deck];
        mutate_generation_deck = -1;
    }
    ++original_calls;
    return foreground + 37;
}

int xz_hook_arm(uint32_t address, const unsigned char expected[8],
                void *replacement, void **saved)
{
    static const unsigned char guard[8] =
        {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    assert(address == 0x76284 && !memcmp(expected, guard, sizeof(guard)));
    installed = (operate_fn)replacement;
    *saved = (void *)original;
    ++hook_calls;
    return 0;
}

int xz_audio_output_deck(const void *manager, uint32_t *rate,
                         uint32_t *generation)
{
    int i;
    for (i = 0; i < 2; ++i) if (mapped[i] && manager == &managers[i]) {
        *rate = rates[i]; *generation = generations[i]; return i;
    }
    return -1;
}

static void fill(float *buffer, unsigned frames, float seed)
{
    unsigned i;
    for (i = 0; i < frames * 2u; ++i) buffer[i] = seed + (float)i / 997.0f;
}

int main(void)
{
    float a[512 * 2], b[512 * 2], original_a[512 * 2];
    int desired;
    memset(managers, 0, sizeof(managers));
    set_active(&managers[0], 1); set_active(&managers[1], 1);
    assert(xz_key_get_desired_semitones(0, &desired) == 0 && desired == 0);
    assert(xz_key_runtime_start() == 0 && installed && hook_calls == 1);
    assert(xz_key_runtime_start() == 0 && hook_calls == 1);

    fill(a, 512, 0.25f); memcpy(original_a, a, sizeof(a));
    assert(installed(&managers[0], 100, a, 512, 80) == 137);
    assert(last_manager == &managers[0] && last_foreground == 100 &&
           last_frames == 512 && last_background == 80);
    assert(!memcmp(a, original_a, sizeof(a)));

    /* Stock may establish, remove, or swap the active DSP. Each transition is
     * stock-only because ownership did not remain stable across the call. */
    assert(xz_key_set_desired_semitones(0, 12) == 0);
    set_active(&managers[0], 0); mutate_active = 1;
    fill(a, 512, 0.35f); memcpy(original_a, a, sizeof(a));
    assert(installed(&managers[0], 137, a, 512, 90) == 174);
    assert(!memcmp(a, original_a, sizeof(a)));
    mutate_active = 2;
    assert(installed(&managers[0], 174, a, 512, 91) == 211);
    assert(!memcmp(a, original_a, sizeof(a)));
    set_active(&managers[0], 1); mutate_generation_deck = 0;
    assert(installed(&managers[0], 211, a, 512, 92) == 248);
    assert(!memcmp(a, original_a, sizeof(a)));

    assert(xz_key_set_desired_semitones(1, -12) == 0);
    fill(a, 512, 0.5f); memcpy(b, a, sizeof(a));
    assert(installed(&managers[0], 200, a, 512, 160) == 237);
    assert(installed(&managers[1], 300, b, 512, 260) == 337);
    assert(memcmp(a, b, sizeof(a)) != 0);

    /* A new captured generation resets deck 0. Its first block must equal the
     * first block of a separately owned deck with the same requested shift. */
    generations[0] = 3; generations[1] = 11;
    assert(xz_key_set_desired_semitones(1, 12) == 0);
    fill(a, 512, 0.75f); memcpy(b, a, sizeof(a));
    assert(installed(&managers[0], 400, a, 512, 360) == 437);
    assert(installed(&managers[1], 500, b, 512, 460) == 537);
    assert(!memcmp(a, b, sizeof(a)));

    /* Background/slip motion is not the foreground stream-continuity token. */
    fill(a, 512, 0.82f); memcpy(b, a, sizeof(a));
    assert(installed(&managers[0], 437, a, 512, -500) == 474);
    assert(installed(&managers[1], 537, b, 512, 9000) == 574);
    assert(!memcmp(a, b, sizeof(a)));

    /* Same epoch, but a foreground jump breaks the sequential grain history.
     * Background/slip movement alone does not enter the reset decision. */
    generations[1] = 12;
    fill(a, 512, 0.9f); memcpy(b, a, sizeof(a));
    assert(installed(&managers[0], 999, a, 512, 461) == 1036);
    assert(installed(&managers[1], 700, b, 512, -123) == 737);
    assert(!memcmp(a, b, sizeof(a)));

    /* No active DSP means stock did not populate this buffer. Never touch it. */
    set_active(&managers[0], 0);
    fill(a, 512, 1.0f); memcpy(original_a, a, sizeof(a));
    assert(installed(&managers[0], 600, a, 512, 560) == 637);
    assert(!memcmp(a, original_a, sizeof(a)));

    /* An invalid owner/rate and an oversized block also stay stock-only. */
    set_active(&managers[0], 1); rates[0] = 48000;
    fill(a, 512, 1.25f); memcpy(original_a, a, sizeof(a));
    assert(installed(&managers[0], 700, a, 512, 660) == 737);
    assert(!memcmp(a, original_a, sizeof(a)));
    rates[0] = 44100; mapped[0] = 0;
    assert(installed(&managers[0], 800, a, 513, 760) == 837);
    assert(!memcmp(a, original_a, sizeof(a)));

    xz_key_runtime_stop();
    assert(xz_key_get_desired_semitones(0, &desired) == 0 && desired == 0);
    mapped[0] = 1;
    assert(installed(&managers[0], 900, a, 512, 860) == 937);
    assert(!memcmp(a, original_a, sizeof(a)));
    assert(original_calls == 16);
    assert(xz_key_set_desired_semitones(0, 13) == -1);
    assert(xz_key_get_desired_semitones(2, &desired) == -1);
    puts("PASS exact hook guard/ABI, original return, neutral bypass, two decks, generation reset and no-active/rate/owner/stop gates");
    return 0;
}
