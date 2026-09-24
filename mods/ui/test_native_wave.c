/* SPDX-License-Identifier: MIT */
#include "native_wave.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Native wave acceptance requires active assertions"
#endif
static uint16_t pixels[XZ_NATIVE_WAVE_PIXELS+8],scratch[XZ_NATIVE_WAVE_PIXELS+8];
static int calls;
static int draw(void *context,uint16_t *out,size_t count,size_t stride,int width,int height) {
    assert(count==536*64&&stride==536&&width==536&&height==64);calls++;
    for(size_t i=0;i<count;i++)out[i]=0xabcd;
    return *(int *)context;
}
static struct xz_native_wave_scene scene(void) {
    return (struct xz_native_wave_scene){1,0,0x4d200c,0x4d1f30,0x1fc2fc,0x1fdbe4,
        2,1,100,200,536,268,9,0x41000000,131,18,667,286,1};
}
static void fill(void) {
    for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++)pixels[y*536+x]=(uint16_t)y;
    for(unsigned i=0;i<8;i++)pixels[XZ_NATIVE_WAVE_PIXELS+i]=scratch[XZ_NATIVE_WAVE_PIXELS+i]=0xdead;
}
int main(void) {
    struct xz_native_wave_pair pair={0};struct xz_native_wave_scene s=scene();int success=1;
    fill();assert(xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1072));
    assert(xz_native_wave_finish(&pair,&s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,draw,&success));
    assert(!pair.captured&&!pair.pixels&&calls==1);
    for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++){
        uint16_t expected=(y>=100&&y<132)||(y>=232&&y<264)?0xabcd:
            y<100||(y>=132&&y<232)?xz_wave_source_row(y,100):0;
        assert(pixels[y*536+x]==expected);
    }
    for(unsigned i=0;i<8;i++)assert(pixels[XZ_NATIVE_WAVE_PIXELS+i]==0xdead&&scratch[XZ_NATIVE_WAVE_PIXELS+i]==0xdead);
    assert(!xz_native_wave_finish(&pair,&s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,draw,&success));
    /* Every changed scene or lock identity rejects before modifying the frame. */
    for(int mode=0;mode<19;mode++) {
        fill();s=scene();assert(xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1072));
        uintptr_t thread=7,hw=200;size_t count=XZ_NATIVE_WAVE_PIXELS;uint16_t *buffer=scratch;
        switch(mode) {
        case 0:s.enabled=0;break;case 1:s.panel_open=1;break;
        case 2:s.window_table++;break;case 3:s.control_table++;break;
        case 4:s.control_set++;break;case 5:s.control_clear++;break;
        case 6:s.scene_index=13;break;case 7:s.visible=0;break;
        case 8:s.gr++;break;case 9:s.hw++;break;case 10:s.width--;break;
        case 11:s.pixel_type=8;break;case 12:s.flags=0x40000000;break;
        case 13:s.y++;break;case 14:s.lifecycle_epoch++;break;
        case 15:thread++;break;case 16:hw++;break;case 17:count--;break;
        case 18:buffer=pixels+1;break;
        }
        assert(!xz_native_wave_finish(&pair,&s,thread,hw,buffer,count,draw,&success));
        assert(!pair.captured&&!pair.pixels&&calls==1);
        for(unsigned y=0;y<268;y++)assert(pixels[y*536]==y);
    }
    s=scene();success=0;fill();
    assert(xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1072));
    assert(!xz_native_wave_finish(&pair,&s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,draw,&success));
    for(unsigned y=0;y<268;y++)assert(pixels[y*536]==y);
    assert(!xz_native_wave_capture(&pair,&s,0x1fd054,7,pixels,1072));
    assert(!xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1074));
    assert(!xz_native_wave_capture(&pair,&s,0x1fd058,7,NULL,1072));
    assert(xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1072));
    assert(!xz_native_wave_capture(&pair,&s,0,7,pixels,1072));
    assert(!pair.captured&&!pair.pixels);
    puts("PASS native wave paired ownership, scene gates, transactional rendering, cue bands and bounds");
}
