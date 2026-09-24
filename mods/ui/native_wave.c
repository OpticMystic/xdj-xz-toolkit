/* SPDX-License-Identifier: MIT */
#include "native_wave.h"
#include "wave_viewport.h"
#include <string.h>

static _Thread_local uint16_t rows[XZ_NATIVE_WAVE_ROWS_PIXELS];

int xz_native_wave_scene_valid(const struct xz_native_wave_scene *s) {
    return s && s->enabled && !s->panel_open &&
        s->window_table == 0x4d200cu && s->control_table == 0x4d1f30u &&
        s->control_set == 0x1fc2fcu && s->control_clear == 0x1fdbe4u &&
        s->scene_index == 2 && s->visible && s->gr && s->hw &&
        s->width == 536 && s->height == 268 && s->pixel_type == 9 &&
        (s->flags & 0x40000000u) != 0 &&
        s->x == 131 && s->y == 18 && s->right == 667 && s->bottom == 286;
}
void xz_native_wave_reset(struct xz_native_wave_pair *p) {
    if(p)memset(p,0,sizeof(*p));
}
int xz_native_wave_capture(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                           uintptr_t caller,uintptr_t thread,uint16_t *pixels,int pitch) {
    if(!p)return 0;
    xz_native_wave_reset(p);
    if(caller != 0x1fd058u || !xz_native_wave_scene_valid(s) || !(s->flags & 0x01000000u) || !pixels ||
        ((uintptr_t)pixels & 1u) || pitch != 1072)return 0;
    p->captured=1;p->thread=thread;p->scene=*s;p->pixels=pixels;return 1;
}
int xz_native_wave_finish(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render render,void *context) {
    if(!p)return 0;
    struct xz_native_wave_pair saved=*p;
    xz_native_wave_reset(p);
    if(!saved.captured || !xz_native_wave_scene_valid(s) || !(s->flags & 0x01000000u) || thread != saved.thread ||
        hw != saved.scene.hw || hw != s->hw || s->gr != saved.scene.gr ||
        s->lifecycle_epoch != saved.scene.lifecycle_epoch || !scratch ||
        ((uintptr_t)scratch & 1u) || count < XZ_NATIVE_WAVE_PIXELS || !render)return 0;
    uintptr_t a=(uintptr_t)scratch,b=(uintptr_t)saved.pixels;
    size_t bytes=XZ_NATIVE_WAVE_PIXELS*sizeof(*scratch);
    if(a>UINTPTR_MAX-bytes || b>UINTPTR_MAX-bytes || (a<b+bytes && b<a+bytes))return 0;
    memcpy(scratch,saved.pixels,bytes);
    if(!xz_wave_compact(scratch,XZ_NATIVE_WAVE_PIXELS,536,100) ||
        !render(context,rows,XZ_NATIVE_WAVE_ROWS_PIXELS,536,536,64))return 0;
    memcpy(scratch+536*XZ_NATIVE_WAVE_ROW1_Y,rows,536*XZ_NATIVE_WAVE_ROW_HEIGHT*sizeof(*rows));
    memcpy(scratch+536*XZ_NATIVE_WAVE_ROW2_Y,rows+536*XZ_NATIVE_WAVE_ROW_HEIGHT,
           536*XZ_NATIVE_WAVE_ROW_HEIGHT*sizeof(*rows));
    memcpy(saved.pixels,scratch,bytes);
    return 1;
}
