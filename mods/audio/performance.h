/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_AUDIO_PERFORMANCE_H
#define XZ_AUDIO_PERFORMANCE_H
#include "stems.h"
#define XZ_XPAD_BANKS 8
#define XZ_XPAD_ROLLS 6

struct xz_perf_grid {
    const int64_t *beats;
    int32_t count;
    double samples_per_beat;
    int64_t beat_zero;
};
/* Worker boundary. Grid storage must remain valid for the entire audio call. */
int xz_perf_grid_valid(const struct xz_perf_grid *grid);
double xz_perf_beat_at(const struct xz_perf_grid *grid, int64_t position, int32_t *cursor);
double xz_perf_loop_phase(int64_t engaged_at, int64_t span, int64_t at, double ratio);

struct xz_perf_clock { int64_t last_position; int still_blocks; double beat; };
void xz_perf_clock_reset(struct xz_perf_clock *clock);
/* Keep separate clocks for pre-stretch source reads and post-stretch output.
 * output_position must describe this output block, not the input read-ahead. */
void xz_perf_clock_step(struct xz_perf_clock *clock, const struct xz_perf_grid *grid,
                         int64_t output_position, uint32_t frames,
                         double *beat_from, double *beat_to);

struct xz_perf_sample { const int16_t *pcm; int64_t frames; uint32_t sample_rate; };
struct xz_groove {
    struct xz_perf_sample sample;
    int part; /* 0 drums, 1 harmonics, 2 vocals; negative disables replacement */
    int64_t engaged_at;
    double samples_per_beat;
};
/* PRE-STRETCH. Replacement follows track/grid phase through seeking, loops,
 * scratch and tempo changes. Borrowed sample and grid lifetimes are caller-owned.
 * Stems and replacement must already share source_rate and aligned frame zero. */
size_t xz_groove_mix_pre(float *dst, size_t frames, int64_t position,
                         uint32_t source_rate, const struct xz_stem_pcm *stems,
                         struct xz_stem_levels levels, const struct xz_groove *groove,
                         const struct xz_perf_grid *grid);

struct xz_xpad_voice {
    uint64_t position;
    uint32_t phase;
    float ratio, window;
    uint32_t start;
    int fresh, sounding;
};
struct xz_xpad {
    struct xz_xpad_voice voice[XZ_XPAD_BANKS];
    int previous_roll;
    int64_t claimed_boundary;
    int claim_valid;
    uint32_t rate, pole_frames;
    float pole;
};
struct xz_xpad_controls {
    int selected_bank;
    int roll; /* -1 off; 0..5 = 1/16,1/8,1/4,1/2,1,2 beats */
    float semitones; /* -12..12; a released gesture retains each voice's bend */
    float volume; /* 0..1 */
    uint32_t manual_hits; /* One immediate hit per bank per block, as upstream */
};
struct xz_xpad_event { unsigned bank; double beat; };
void xz_xpad_reset(struct xz_xpad *pad);
/* POST-STRETCH. Call once per output block, after any engine crossfade. Voices
 * advance in output frames independently of track speed and paused transport.
 * All state is audio-thread-owned: the adapter must deliver controls/events at
 * block boundaries and retain bank PCM. Sequence events run before rolls, then
 * manual hits win. Returns the sum of rendered frames across sounding voices.
 * Sequencer recording/storage and UI are separate adapters. */
size_t xz_xpad_mix_post(struct xz_xpad *pad,
                         const struct xz_perf_sample banks[XZ_XPAD_BANKS],
                         float *dst, uint32_t frames, uint32_t sample_rate,
                         double beat_from, double beat_to,
                         struct xz_xpad_controls controls,
                         const struct xz_xpad_event *events, size_t event_count);
#endif
