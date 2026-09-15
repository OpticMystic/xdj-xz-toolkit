#ifndef XZ_WAVE_VIEWPORT_H
#define XZ_WAVE_VIEWPORT_H
#include <stddef.h>
#include <stdint.h>
/* Qualified 536x268 native window, two 132-row lanes and a four-row separator.
   Called only after a complete redraw while locked. Cue bands retain all ten
   rows each; only the 112 interior rows are reduced. Remaining rows are cleared. */
int xz_wave_compact(uint16_t *pixels,size_t count,size_t stride,unsigned lane_height);
unsigned xz_wave_source_row(unsigned row,unsigned lane_height);
#endif
