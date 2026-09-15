/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error Test assertions must be enabled
#endif
#include "performance.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int16_t samples[4000];
static void oracle_checks(FILE *oracle)
{
    const float semitones[] = {-12, 0, 12};
    struct xz_perf_sample banks[8] = {{0}};
    banks[0] = (struct xz_perf_sample){samples, 2000, 44100};
    for (unsigned test = 0; test < 3; ++test) {
        struct xz_xpad pad;
        struct xz_xpad_controls controls = {-1, 0, semitones[test], 1, 1};
        xz_xpad_reset(&pad);
        for (unsigned block = 0; block < 2; ++block) {
            float actual[512] = {0}, expected[512];
            if (block) { controls.roll = -1; controls.manual_hits = 0; controls.semitones = 0; }
            assert(xz_xpad_mix_post(&pad, banks, actual, 256, 44100, 0, 0, controls, NULL, 0) == 256);
            assert(fread(expected, sizeof(float), 512, oracle) == 512);
            for (unsigned i = 0; i < 512; ++i) assert(fabsf(actual[i] - expected[i]) < 2e-6f);
        }
        assert(pad.voice[0].position == 512);
        assert(pad.voice[0].ratio == exp2f(semitones[test] / 12));
    }
    for (int ratio = 1; ratio <= 4; ++ratio)
        for (int at = -100; at <= 100; at += 25) {
            double expected;
            assert(fread(&expected, sizeof(expected), 1, oracle) == 1);
            assert(fabs(xz_perf_loop_phase(-10, 97, at, ratio * 0.25) - expected) < 1e-9);
        }
    {
        int64_t beats[] = {100, 200, 325, 500};
        int64_t positions[] = {0, 99, 100, 199, 200, 300, 499, 500, 700};
        struct xz_perf_grid grid = {beats, 4, 100, 100};
        int32_t cursor = INT32_MAX;
        assert(xz_perf_grid_valid(&grid));
        for (unsigned i = 0; i < 9; ++i) {
            double expected;
            assert(fread(&expected, sizeof(expected), 1, oracle) == 1);
            assert(fabs(xz_perf_beat_at(&grid, positions[i], &cursor) - expected) < 1e-9);
        }
    }
    assert(fgetc(oracle) == EOF);
}

static void groove_checks(void)
{
    const int16_t h[] = {32767,32767,32767,32767};
    const int16_t v[] = {0,0,0,0};
    const int16_t replacement[] = {16384,-16384,-16384,16384};
    struct xz_stem_pcm stems = {h,v,2,1,1};
    struct xz_groove groove = {{replacement,2,44100},0,0,0};
    for (int part = 0; part < 3; ++part) {
        float out[] = {2,2,2,2,7,9};
        float f = 16384.0f / 32767.0f;
        groove.part = part;
        assert(xz_groove_mix_pre(out,3,0,44100,&stems,(struct xz_stem_levels){1,1,1},&groove,NULL)==2);
        assert(fabsf(out[0] - ((part == 2 ? 2 : 1) + f)) < 1e-6f);
        assert(fabsf(out[1] - ((part == 2 ? 2 : 1) - f)) < 1e-6f);
        assert(out[4] == 7 && out[5] == 9);
    }
}

static void roll_and_clock_checks(void)
{
    struct xz_xpad pad;
    struct xz_perf_sample banks[8] = {{samples,2000,44100}};
    struct xz_xpad_controls controls = {0,4,0,1,0};
    float out[128] = {0};
    xz_xpad_reset(&pad);
    xz_xpad_mix_post(&pad,banks,out,64,44100,0.97,0.98,controls,NULL,0);
    assert(pad.voice[0].position==64);
    xz_xpad_mix_post(&pad,banks,out,64,44100,0.98,1.01,controls,NULL,0);
    assert(pad.voice[0].position==128); /* first hit claimed the nearby boundary */
    xz_xpad_mix_post(&pad,banks,out,64,44100,1.98,2.01,controls,NULL,0);
    assert(pad.voice[0].position==22); /* next roll starts at frame 42 */
    xz_xpad_reset(&pad);
    for (unsigned i=0;i<8;++i) banks[i]=banks[0];
    memset(out,0,sizeof(out));
    controls=(struct xz_xpad_controls){0,-1,0,1,255};
    assert(xz_xpad_mix_post(&pad,banks,out,64,44100,0,0,controls,NULL,0)==512);
    assert(fabsf(out[0]-8.0f*samples[0]/32767.0f)<1e-6f);
    {
        struct xz_perf_clock clock;
        struct xz_perf_grid grid={NULL,0,22050,0};
        double b0,b1;
        xz_perf_clock_reset(&clock);
        for(int i=0;i<30;++i) xz_perf_clock_step(&clock,&grid,0,64,&b0,&b1);
        assert(b0==b1);
        xz_perf_clock_step(&clock,&grid,0,64,&b0,&b1);
        assert(fabs((b1-b0)-64.0/22050)<1e-12);
    }
}

int main(int argc,char **argv)
{
    FILE *oracle;
    assert(argc==2);
    for(unsigned i=0;i<4000;++i) samples[i]=(int16_t)((i*37)%20001-10000);
    oracle=fopen(argv[1],"rb");assert(oracle);
    oracle_checks(oracle);fclose(oracle);
    groove_checks();roll_and_clock_checks();
    puts("upstream pitch/phase oracle, all three groove replacements, eight voices, rolls and paused clock passed");
    return 0;
}
