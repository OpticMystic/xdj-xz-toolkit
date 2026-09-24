/* SPDX-License-Identifier: MIT */
#include "native_wave_runtime.h"
#include "../runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int (*stock_lock)(void *,void **,int *);
static int (*stock_unlock)(void *);
static int (*requested)(void);
static xz_native_wave_render renderer;
static void *renderer_context;
static _Thread_local struct xz_native_wave_pair pair;
static _Thread_local uint16_t scratch[XZ_NATIVE_WAVE_PIXELS];
static int transformed, observed, observe_only, focus_verified;
struct xz_inline_proof { uint32_t version,captures,frames,skipped; };
__attribute__((visibility("default"))) struct xz_inline_proof xz_inline_proof_v1={1,0,0,0};

static int word(uint32_t address,uint32_t *out){return xz_read_memory(address,out,4);}
static int scene(struct xz_native_wave_scene *s){
    uint32_t table,controls,index,visible,gr,set,clear;
    memset(s,0,sizeof(*s));
    if(!requested||!requested()||word(0x3eba54c,&table)||table!=0x4d200c||
       word(0x3eba548,&controls)||controls!=0x4d1f30||word(controls+12,&set)||word(controls+16,&clear)||
       word(0x3c6fc7c,&index)||word(0x1b31f4c,&visible)||word(0x1b31f88,&gr)||!gr)return 0;
    unsigned char data[64];if(xz_read_memory(gr,data,sizeof(data)))return 0;
    uint32_t values[16];memcpy(values,data,sizeof(data));
    s->enabled=1;s->window_table=table;s->control_table=controls;s->control_set=set;s->control_clear=clear;
    s->scene_index=index;s->visible=visible;s->gr=gr;s->hw=values[7];
    s->width=values[0]&65535;s->height=values[0]>>16;s->pixel_type=values[2];s->flags=values[6];
    s->x=(int32_t)values[12];s->y=(int32_t)values[13];s->right=(int32_t)values[14];s->bottom=(int32_t)values[15];
    s->lifecycle_epoch=gr;
    return xz_native_wave_scene_valid(s);
}
int xz_native_focus_deck(void){
    uint32_t deck;
    if(!focus_verified||word(0x02205d10,&deck)||deck<1||deck>2)return -1;
    return (int)deck-1;
}
int xz_native_inline_active(void){
    struct xz_native_wave_scene current;
    if(!scene(&current)){__atomic_store_n(&transformed,0,__ATOMIC_RELEASE);return 0;}
    return __atomic_load_n(&transformed,__ATOMIC_ACQUIRE);
}
static int lock_hook(void *window,void **pixels,int *pitch){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    int result=stock_lock(window,pixels,pitch);
    struct xz_native_wave_scene current;
    if(caller==0x1fd058&&scene(&current)&&current.gr==(uintptr_t)window&&pixels&&pitch&&
       xz_native_wave_capture(&pair,&current,caller,(uintptr_t)pthread_self(),*pixels,*pitch)){
        __atomic_fetch_add(&xz_inline_proof_v1.captures,1,__ATOMIC_RELAXED);
        if(!__atomic_exchange_n(&observed,1,__ATOMIC_ACQ_REL))
            xz_log("INLINE_WAVE_PROOF: native play scene=2 window=536x268 at=131,18 pitch=1072 caller=1fd058");
    }
    return result;
}
static int unlock_hook(void *window){
    if(pair.captured&&pair.scene.hw==(uintptr_t)window){
        struct xz_native_wave_scene current;int drawn=0;
        if(scene(&current)&&!observe_only)
            drawn=xz_native_wave_finish(&pair,&current,(uintptr_t)pthread_self(),(uintptr_t)window,
                scratch,XZ_NATIVE_WAVE_PIXELS,renderer,renderer_context);
        else xz_native_wave_reset(&pair);
        __atomic_store_n(&transformed,drawn,__ATOMIC_RELEASE);
        if(drawn)__atomic_fetch_add(&xz_inline_proof_v1.frames,1,__ATOMIC_RELAXED);
        else __atomic_fetch_add(&xz_inline_proof_v1.skipped,1,__ATOMIC_RELAXED);
    }
    return stock_unlock(window);
}
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context){
    static const unsigned char lock_guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char unlock_guard[8]={0x10,0x40,0x2d,0xe9,0x58,0xd0,0x4d,0xe2};
    static const unsigned char focus_guard[16]={0x10,0x3d,0x05,0xe3,0x20,0x32,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    unsigned char actual[16];
    focus_verified=!xz_read_memory(0xf3cc4,actual,sizeof(actual))&&!memcmp(actual,focus_guard,sizeof(actual));
    requested=enabled;renderer=render;renderer_context=context;
    const char *mode=getenv("XZ_MODS_INLINE");observe_only=mode&&!strcmp(mode,"observe");
    if(!stock_lock&&xz_hook_arm(0x156958,lock_guard,(void *)lock_hook,(void **)&stock_lock))return -1;
    if(!stock_unlock&&xz_hook_arm(0x1656c4,unlock_guard,(void *)unlock_hook,(void **)&stock_unlock))return -1;
    return 0;
}
