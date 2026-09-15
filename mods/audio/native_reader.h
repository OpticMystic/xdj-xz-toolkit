/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ126_NATIVE_READER_H
#define XZ126_NATIVE_READER_H
#include <stddef.h>
#include <stdint.h>

#define XZ126_PLAY_ENGINE_CELL UINT32_C(0x010e9688)
#define XZ126_READER_PATH_MAX 1024

/* Return zero only when all requested bytes were copied. The native adapter
 * supplies a guarded process reader. This helper never dereferences addresses
 * itself, installs hooks, calls firmware functions or writes native objects. */
typedef int (*xz126_read_fn)(void *context, uint32_t address, void *out, size_t n);
struct xz126_deck_source {
    uint32_t player;
    uint32_t manager;
    uint32_t reader;
    uint32_t reader_impl;
    uint32_t file_reader;
    uint32_t converter;
    uint32_t reader_rate;
    uint32_t reader_frames;
    uint32_t file_rate;
    uint32_t file_frames;
    unsigned deck_count;
    int manager_bound;
    char path[XZ126_READER_PATH_MAX];
};

/* Worker only. play_engine is *XZ126_PLAY_ENGINE_CELL, not DjEngineIF `this`.
 * Deck indices are zero-based. Returns 0 for a complete consistent snapshot;
 * -1 for missing, invalid or changing state. A loaded source is not proof that
 * its PCM ring currently covers an audio read after a seek. */
int xz126_deck_source_read(xz126_read_fn read, void *context,
                           uint32_t play_engine, unsigned deck,
                           struct xz126_deck_source *out);

/* Compute source interval that stock TimeStretch::getStreamAt fills with track
 * content. Leading/trailing samples outside [0,length) are stock zero fill.
 * Does not establish PcmReader readiness. frame_count is the requested count,
 * not the original function's return register. */
struct xz126_source_extent { uint32_t skip, frames, source_position; };
struct xz126_source_extent xz126_source_extent(int32_t position,
                                               uint32_t frame_count,
                                               uint32_t source_length);
#endif
