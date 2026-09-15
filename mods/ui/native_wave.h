/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_H
#define XZ_NATIVE_WAVE_H
#include <stddef.h>
#include <stdint.h>

#define XZ_NATIVE_WAVE_PIXELS (536u * 268u)
#define XZ_NATIVE_WAVE_STRIP_Y 204u
#define XZ_NATIVE_WAVE_STRIP_HEIGHT 64u

/* Values read by the hook adapter from the qualified 1.26 addresses.
   The adapter must revoke touch geometry whenever this snapshot stops passing
   the gate, even when no new waveform frame arrives. */
struct xz_native_wave_scene {
    int enabled, panel_open;
    uintptr_t window_table, control_table, control_set, control_clear;
    unsigned scene_index, visible;
    uintptr_t gr, hw;
    unsigned width, height, pixel_type, flags;
    int x, y, right, bottom;
    uint64_t lifecycle_epoch;
};
struct xz_native_wave_pair {
    int captured;
    uintptr_t thread;
    struct xz_native_wave_scene scene;
    uint16_t *pixels;
};
typedef int (*xz_native_wave_render)(void *context,uint16_t *pixels,
                                    size_t count,size_t stride,int width,int height);
int xz_native_wave_scene_valid(const struct xz_native_wave_scene *);
void xz_native_wave_reset(struct xz_native_wave_pair *);
/* Call after stock GR lock. The adapter supplies only freshly returned outputs,
   never stale outputs from a failed lock. It must preserve stock's return. */
int xz_native_wave_capture(struct xz_native_wave_pair *,const struct xz_native_wave_scene *,
                           uintptr_t caller,uintptr_t thread,uint16_t *pixels,int pitch);
/* Call before stock Task unlock, then preserve its return. Every call consumes
   the pair. Scratch must be private to this invocation and not overlap pixels.
   Snapshot reads and pixels require the native window's still-held lock. This
   helper does not prove concurrent destroy/recreate safety on the device. */
int xz_native_wave_finish(struct xz_native_wave_pair *,const struct xz_native_wave_scene *,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render,void *context);
#endif
