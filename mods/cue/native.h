#ifndef XZ_CUE_NATIVE_H
#define XZ_CUE_NATIVE_H
#include "cue.h"
struct xz_cue_native { void *engine_if[2]; int verified; };
struct xz_cue_api xz_cue_native_api(struct xz_cue_native *);
int xz_cue_native_decode(struct xz_cue_native *,void *innards,const void *input,struct xz_cue_event *);
#endif
