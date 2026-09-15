#ifndef XZ_STEM_PADS_H
#define XZ_STEM_PADS_H
#include "../cue/cue.h"
struct xz_stem_pads { unsigned down[2], owned[2], muted[2]; };
/* Returns ownership, including release after leaving the view. toggle is -1
 * except for a newly claimed press (0..2 mute, 3 bypass). */
int xz_stem_pad_event(struct xz_stem_pads *, const struct xz_cue_event *, int active, int *toggle);
int xz_stem_control_event(struct xz_stem_pads *, const struct xz_cue_event *, int active,
    int selected_page, int shift_pages, int *toggle);
#endif
