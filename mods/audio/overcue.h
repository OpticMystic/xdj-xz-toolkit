/* SPDX-License-Identifier: MIT */
#ifndef XZ_OVERCUE_H
#define XZ_OVERCUE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define XZ_OC_PAGE 131072u
#define XZ_OC_RATE 96000u
#define XZ_OC_PAGE_SLOTS 8u
#define XZ_OC_WINDOW_FRAMES (XZ_OC_RATE * 2u)
struct xz_oc_page { uint64_t offset; uint32_t compressed, bytes; unsigned char sha[32]; };
struct xz_oc_file {
    FILE *file;
    struct xz_oc_page *pages;
    uint32_t page_count, cached_page[XZ_OC_PAGE_SLOTS], age[XZ_OC_PAGE_SLOTS], clock;
    uint64_t bytes;
    unsigned char *raw, *compressed;
};
struct xz_oc_assets {
    struct xz_oc_file role[3], reference, combination[3];
    uint64_t frames;
    float gain[3];
    float combination_gain[3];
    unsigned char *wave[3];
    uint32_t wave_count[3];
};
/* Worker-only immutable USB reader. Opens only the source's exact indexed bundle,
 * verifies source identity, metadata/table hashes, and each decoded page. */
int xz_oc_open(const char *source, struct xz_oc_assets *, char *error, size_t capacity);
void xz_oc_close(struct xz_oc_assets *);
int xz_oc_read(struct xz_oc_file *, int64_t first, size_t frames, int16_t *stereo);
struct xz_oc_file *xz_oc_mix_file(struct xz_oc_assets *, unsigned mask);
float xz_oc_mix_gain(const struct xz_oc_assets *, unsigned mask);
struct xz_oc_plan { float source, gain[2]; unsigned mask[2]; };
int xz_oc_plan_levels(const float levels[3], struct xz_oc_plan *);
/* Call only on an owned background worker, never a native/audio thread. */
int xz_oc_worker_schedule(void);

struct xz_oc_stream;
enum xz_oc_state { XZ_OC_ALIGNING, XZ_OC_READY, XZ_OC_BUFFERING, XZ_OC_ERROR };
struct xz_oc_status { enum xz_oc_state state; int lag; float correlation; uint32_t blocks, misses; };
struct xz_oc_stream *xz_oc_stream_open(const char *source, uint32_t rate, char *error, size_t capacity);
void xz_oc_stream_close(struct xz_oc_stream *);
/* Audio thread: no allocation, I/O, mutex or waiting. Stock unity is bit-exact.
 * An unverified timeline stays on stock audio. Pending selections retain the
 * current covered mix, or native audio after a seek; missing data never mutes
 * the whole output. Readiness remains buffering until the requested mix fits. */
void xz_oc_stream_loop_start(struct xz_oc_stream *,int32_t position);
size_t xz_oc_stream_mix(struct xz_oc_stream *, float *stereo, size_t frames, int64_t position, const float levels[3]);
void xz_oc_stream_status(struct xz_oc_stream *, struct xz_oc_status *);
void xz_oc_stream_wave(struct xz_oc_stream *, unsigned role, unsigned char *bins, size_t count, float *progress);
#endif
