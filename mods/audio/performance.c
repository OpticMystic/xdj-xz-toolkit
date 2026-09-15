/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Loop, beat, Groove Circuit and X-PAD DSP adapted from cdj3k-mods commit
 * e74e199603e2a25567950ca72997c38d17ada4e8. See PROVENANCE.md. */
#include "performance.h"
#include <math.h>
#include <string.h>

static double loop_wrap(double u, int64_t span)
{
    double s;
    if (span <= 0 || !isfinite(u)) return 0;
    s = (double)span;
    u = fmod(u, s);
    if (u < 0) u += s;
    return u >= s ? 0 : u;
}

double xz_perf_loop_phase(int64_t engaged_at, int64_t span, int64_t at, double ratio)
{
    if (!(ratio > 0) || !isfinite(ratio)) return 0;
    return loop_wrap(((double)at - (double)engaged_at) * ratio, span);
}

int xz_perf_grid_valid(const struct xz_perf_grid *g)
{
    if (!g || !isfinite(g->samples_per_beat) || g->samples_per_beat < 0) return 0;
    if (!g->beats) return g->count == 0 && g->samples_per_beat > 0;
    if (g->count < 2 || g->count > 1000000) return 0;
    for (int32_t i = 1; i < g->count; ++i)
        if (g->beats[i] <= g->beats[i - 1]) return 0;
    return 1;
}

double xz_perf_beat_at(const struct xz_perf_grid *g, int64_t at, int32_t *cursor)
{
    int32_t lo = -1, hi;
    double step;
    const int64_t *beats;
    int32_t count;
    if (!g) return 0;
    if (!g->beats) return g->samples_per_beat > 0 ?
        ((double)at - (double)g->beat_zero) / g->samples_per_beat : 0;
    beats = g->beats; count = g->count;
    if (count < 2) return 0;
    if (at < beats[0]) {
        step = (double)beats[1] - (double)beats[0];
        return step > 0 ? ((double)at - (double)beats[0]) / step : 0;
    }
    if (at >= beats[count - 1]) {
        step = (double)beats[count - 1] - (double)beats[count - 2];
        return step > 0 ? (double)(count - 1) + ((double)at - (double)beats[count - 1]) / step : count - 1;
    }
    if (cursor) {
        int32_t i = *cursor;
        if (i >= 0 && i <= count - 2 && beats[i] <= at) {
            for (int n = 0; n < 4 && i < count - 2 && at >= beats[i + 1]; ++n) ++i;
            if (at < beats[i + 1]) lo = i;
        }
    }
    if (lo < 0) {
        lo = 0; hi = count - 1;
        while (hi - lo > 1) {
            int32_t mid = lo + (hi - lo) / 2;
            if (beats[mid] <= at) lo = mid; else hi = mid;
        }
    }
    if (cursor) *cursor = lo;
    step = (double)beats[lo + 1] - (double)beats[lo];
    return step > 0 ? (double)lo + ((double)at - (double)beats[lo]) / step : lo;
}

void xz_perf_clock_reset(struct xz_perf_clock *c)
{
    if (c) { memset(c, 0, sizeof(*c)); c->last_position = -1; }
}

void xz_perf_clock_step(struct xz_perf_clock *c, const struct xz_perf_grid *grid,
                         int64_t pos, uint32_t frames, double *b0, double *b1)
{
    int64_t previous, advance = 0;
    if (!c || !b0 || !b1) return;
    *b0 = *b1 = c->beat;
    if (!frames || frames > 65536 || pos > INT64_MAX - (int64_t)frames * 8) return;
    previous = c->last_position;
    if (pos >= 0) {
        if (previous >= 0 && pos > previous) advance = pos - previous;
        c->last_position = pos;
    }
    if (advance <= 0 || advance > (int64_t)frames * 8) advance = frames;
    if (pos >= 0 && previous >= 0 && pos != previous) {
        c->still_blocks = 0;
        *b0 = xz_perf_beat_at(grid, pos, NULL);
        *b1 = xz_perf_beat_at(grid, pos + advance, NULL);
        if (!(*b1 > *b0)) *b1 = *b0;
    } else if (c->still_blocks < 30) ++c->still_blocks;
    else if (grid && grid->samples_per_beat > 0)
        *b1 = *b0 + (double)frames / grid->samples_per_beat;
    c->beat = *b1;
}

static void loop_frame(const struct xz_perf_sample *sample, double u, float *l, float *r)
{
    int64_t i = (int64_t)u, next = i + 1 < sample->frames ? i + 1 : 0;
    float b = (float)(u - (double)i);
    float wb = b * (1.0f / 32767.0f), wa = (1 - b) * (1.0f / 32767.0f);
    *l = wa * sample->pcm[i * 2] + wb * sample->pcm[next * 2];
    *r = wa * sample->pcm[i * 2 + 1] + wb * sample->pcm[next * 2 + 1];
}

size_t xz_groove_mix_pre(float *dst, size_t frames, int64_t position,
                         uint32_t rate, const struct xz_stem_pcm *stems,
                         struct xz_stem_levels levels, const struct xz_groove *groove,
                         const struct xz_perf_grid *grid)
{
    size_t n;
    double ratio = 1, engaged_beat = 0;
    int32_t cursor = -1;
    int beat_route;
    float hs, vs;
    if (!groove || groove->part < 0 || groove->part > 2 || !groove->sample.pcm ||
        groove->sample.frames <= 0 || (uint64_t)groove->sample.frames > SIZE_MAX / 4 ||
        groove->sample.sample_rate != rate)
        return xz_stem_mix(dst, frames, position, stems, levels);
    if (!dst || !stems || !stems->harmonics || !stems->vocals || position < 0 ||
        position >= stems->frames || frames > 65536 ||
        (uint64_t)stems->frames > SIZE_MAX / 4 ||
        !isfinite(levels.drums) || !isfinite(levels.harmonics) || !isfinite(levels.vocals) ||
        !isfinite(stems->harmonics_gain) || stems->harmonics_gain <= 0 ||
        !isfinite(stems->vocals_gain) || stems->vocals_gain <= 0 ||
        !isfinite(groove->samples_per_beat)) return 0;
    hs = (1.0f / stems->harmonics_gain) * (1.0f / 32767.0f);
    vs = (1.0f / stems->vocals_gain) * (1.0f / 32767.0f);
    if (!isfinite(hs) || !isfinite(vs)) return 0;
    n = (size_t)(stems->frames - position);
    if (n > frames) n = frames;
    beat_route = grid && grid->beats && grid->count >= 2 && groove->samples_per_beat > 0;
    if (beat_route) engaged_beat = xz_perf_beat_at(grid, groove->engaged_at, NULL);
    else if (grid && grid->samples_per_beat > 0 && groove->samples_per_beat > 0) {
        ratio = groove->samples_per_beat / grid->samples_per_beat;
        if (!(ratio >= 0.25 && ratio <= 4)) ratio = 1;
    }
    for (size_t k = 0; k < n; ++k) {
        float l, r;
        int64_t at = position + (int64_t)k;
        double u = beat_route ? loop_wrap((xz_perf_beat_at(grid, at, &cursor) - engaged_beat) *
            groove->samples_per_beat, groove->sample.frames) :
            xz_perf_loop_phase(groove->engaged_at, groove->sample.frames, at, ratio);
        loop_frame(&groove->sample, u, &l, &r);
        for (unsigned channel = 0; channel < 2; ++channel) {
            size_t out = k * 2 + channel, source = (size_t)at * 2 + channel;
            float h = hs * stems->harmonics[source], v = vs * stems->vocals[source];
            float replacement = channel ? r : l;
            switch (groove->part) {
            case 0: dst[out] = levels.drums * replacement + levels.harmonics * h + levels.vocals * v; break;
            case 1: dst[out] = levels.drums * (dst[out] - h - v) + levels.harmonics * replacement + levels.vocals * v; break;
            default: dst[out] = levels.drums * (dst[out] - h - v) + levels.harmonics * h + levels.vocals * replacement; break;
            }
        }
    }
    return n;
}

static float xp_xfade_window(float x)
{
    if (x < 0.28348f) return 18.16515f * x * x * (1 - 2 * x);
    { float u = 1 - x; return 1 - u * u * u; }
}
struct xp_pitch { float w, wq, half; uint32_t step; };
static void xp_tap(const int16_t *pcm, int64_t frames, int64_t base,
                    float delay, float *l, float *r)
{
    const float q = 1.0f / 32767.0f;
    int32_t di = (int32_t)floorf(delay);
    float df = delay - (float)di;
    int64_t i = base - di;
    if (i < 0 || i >= frames) { *l = *r = 0; return; }
    if (i >= 1) {
        float a = pcm[i * 2], b = pcm[(i - 1) * 2];
        *l = q * (a + df * (b - a));
        a = pcm[i * 2 + 1]; b = pcm[(i - 1) * 2 + 1];
        *r = q * (a + df * (b - a));
    } else {
        *l = q * (1 - df) * pcm[0]; *r = q * (1 - df) * pcm[1];
    }
}
static void xp_heads(const int16_t *pcm, int64_t frames, int64_t base,
                      uint32_t phase, const struct xp_pitch *pitch, float *l, float *r)
{
    float al, ar, bl, br, ga, gb;
    xp_tap(pcm, frames, base, (float)phase * pitch->wq - pitch->half, &al, &ar);
    xp_tap(pcm, frames, base, (float)(phase + 0x80000000u) * pitch->wq - pitch->half, &bl, &br);
    ga = xp_xfade_window((float)(phase << 1) * (1.0f / 4294967296.0f));
    if (phase & 0x80000000u) ga = 1 - ga;
    gb = 1 - ga;
    *l = al * ga + bl * gb; *r = ar * ga + br * gb;
}
static void xp_pitch_block(struct xp_pitch *p, float *window, float ratio, float a, uint32_t rate)
{
    float target = (ratio >= 1 ? 26.67f : 31.35f) * 0.001f * (float)rate;
    if (!(*window > 0)) *window = target;
    *window = a * *window + (1 - a) * target;
    p->w = *window; p->wq = *window * (1.0f / 4294967296.0f);
    p->step = (uint32_t)(int32_t)((1.0 - (double)ratio) / (double)*window * 4294967296.0);
    p->half = 26.67f * 0.0005f * (float)rate;
}

void xz_xpad_reset(struct xz_xpad *pad)
{
    if (pad) { memset(pad, 0, sizeof(*pad)); pad->previous_roll = -1; }
}
static void trigger(struct xz_xpad *pad, unsigned bank, double beat,
                      double b0, double b1, uint32_t frames)
{
    struct xz_xpad_voice *voice = &pad->voice[bank];
    double at = b1 > b0 ? (beat - b0) / (b1 - b0) * frames : 0;
    voice->position = 0; voice->phase = 0;
    voice->start = !(at > 0) ? 0 : at >= frames ? frames - 1 : (uint32_t)at;
    voice->fresh = 1; voice->sounding = 1;
}

size_t xz_xpad_mix_post(struct xz_xpad *pad,
                         const struct xz_perf_sample banks[XZ_XPAD_BANKS],
                         float *dst, uint32_t frames, uint32_t rate,
                         double b0, double b1, struct xz_xpad_controls controls,
                         const struct xz_xpad_event *events, size_t event_count)
{
    static const double lengths[] = {0.0625, 0.125, 0.25, 0.5, 1, 2};
    float target, a;
    size_t mixed = 0;
    int fresh_roll;
    if (!pad || !banks || !dst || !frames || frames > 65536 || rate < 8000 || rate > 384000 ||
        !isfinite(b0) || !isfinite(b1) || fabs(b0) > 1e12 || fabs(b1) > 1e12 ||
        !isfinite(controls.semitones) || fabsf(controls.semitones) > 12 ||
        !isfinite(controls.volume) || controls.volume < 0 || controls.volume > 1 ||
        controls.roll < -1 || controls.roll >= XZ_XPAD_ROLLS ||
        event_count > 256 || (event_count && !events)) return 0;
    if (pad->rate && pad->rate != rate) xz_xpad_reset(pad);
    if (pad->rate != rate || pad->pole_frames != frames) {
        pad->rate = rate; pad->pole_frames = frames;
        pad->pole = expf(-((float)frames / (float)rate) / (49.5f * 0.001f));
    }
    a = pad->pole; target = exp2f(controls.semitones / 12);
    for (size_t i = 0; i < event_count; ++i)
        if (events[i].bank < XZ_XPAD_BANKS && isfinite(events[i].beat))
            trigger(pad, events[i].bank, events[i].beat, b0, b1, frames);
    fresh_roll = controls.roll != pad->previous_roll;
    pad->previous_roll = controls.roll;
    if (controls.roll >= 0 && controls.selected_bank >= 0 && controls.selected_bank < XZ_XPAD_BANKS) {
        double length = lengths[controls.roll];
        int64_t boundary = (int64_t)floor(b1 / length);
        if (fresh_roll) {
            pad->claimed_boundary = (int64_t)floor(b1 / length + 0.5);
            pad->claim_valid = 1;
            trigger(pad, (unsigned)controls.selected_bank, b0, b0, b1, frames);
        } else if (b1 > b0 && floor(b1 / length) > floor(b0 / length)) {
            if (!(pad->claim_valid && boundary == pad->claimed_boundary))
                trigger(pad, (unsigned)controls.selected_bank, (double)boundary * length, b0, b1, frames);
            pad->claim_valid = 0;
        }
    }
    for (unsigned i = 0; i < XZ_XPAD_BANKS; ++i)
        if (controls.manual_hits & (1u << i)) trigger(pad, i, b0, b0, b1, frames);
    if (controls.manual_hits && controls.roll >= 0) {
        pad->claimed_boundary = (int64_t)floor(b1 / lengths[controls.roll] + 0.5);
        pad->claim_valid = 1;
    }
    for (unsigned i = 0; i < XZ_XPAD_BANKS; ++i) {
        struct xz_xpad_voice *v = &pad->voice[i];
        const struct xz_perf_sample *bank = &banks[i];
        struct xp_pitch pitch;
        int64_t length, tail;
        if (!v->sounding) continue;
        if (!bank->pcm || bank->frames <= 0 || bank->sample_rate != rate) { v->sounding = 0; continue; }
        length = bank->frames < (int64_t)rate * 10 ? bank->frames : (int64_t)rate * 10;
        if (v->fresh) { v->fresh = 0; v->ratio = controls.roll >= 0 ? target : 1; v->window = 0; }
        else if (controls.roll >= 0) v->ratio = a * v->ratio + (1 - a) * target;
        xp_pitch_block(&pitch, &v->window, v->ratio, a, rate);
        tail = length + (int64_t)(pitch.w - pitch.half) + 2;
        for (uint32_t k = v->start; k < frames; ++k) {
            float l, r;
            if (v->position >= (uint64_t)tail) { v->sounding = 0; break; }
            xp_heads(bank->pcm, length, (int64_t)v->position, v->phase, &pitch, &l, &r);
            v->phase += pitch.step; ++v->position;
            dst[k * 2] += l * controls.volume; dst[k * 2 + 1] += r * controls.volume;
            ++mixed;
        }
        v->start = 0;
    }
    return mixed;
}
