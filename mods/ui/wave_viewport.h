#ifndef XZ_WAVE_VIEWPORT_H
#define XZ_WAVE_VIEWPORT_H
#include <stddef.h>
#include <stdint.h>
/* The 536x268 native window holds two compact waveforms with a 48-row stem
   control space after each. Called only after a complete redraw while locked.
   Cue bands retain ten rows each; only the 112 interior rows are reduced. */
#define XZ_WAVE_LANE_HEIGHT 84
#define XZ_WAVE_CONTROL_HEIGHT 48
#define XZ_WAVE_SECOND_LANE_Y (XZ_WAVE_LANE_HEIGHT + XZ_WAVE_CONTROL_HEIGHT)
#define XZ_WAVE_FIRST_CONTROL_Y (18 + XZ_WAVE_LANE_HEIGHT)
#define XZ_WAVE_SECOND_CONTROL_Y (18 + XZ_WAVE_SECOND_LANE_Y + XZ_WAVE_LANE_HEIGHT)
#define XZ_WAVE_INLINE_HEIGHT (XZ_WAVE_CONTROL_HEIGHT * 2)
int xz_wave_compact(uint16_t *pixels,size_t count,size_t stride,unsigned lane_height);
unsigned xz_wave_source_row(unsigned row,unsigned lane_height);
#endif
