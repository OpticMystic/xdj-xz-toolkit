/* SPDX-License-Identifier: MIT */
#include "native_text_theme.h"
#include <assert.h>
#include <stdio.h>

#ifdef NDEBUG
#error "Native text acceptance requires active assertions"
#endif

static const uint32_t palette[192] = {
    0,0x848684,0xc6c7c6,0xffffff,0,0xb5b6bd,0x5a595a,0,
    0,0x404040,0x606060,0x808080,0,0xc6c7ce,0xadaeb5,0x9c9a9c,
    0,0x104910,0x08ba08,0x00ff00,0,0xadcbad,0x429e42,0x008200,
    0,0x213021,0x314d39,0x395d42,0,0xadcbad,0x429e42,0x008200,
    0,0x632c52,0xbd3c84,0xff49ad,0,0xf7c7e7,0xff86c6,0xff49ad,
    0,0x521818,0x941010,0xff0000,0,0xf7babd,0xff595a,0xff0000,
    0,0x5a3c18,0xbd6508,0xff8600,0,0xf7cfa5,0xffa642,0xff8600,
    0,0x635d29,0xdec731,0xffe331,0,0xf7ebad,0xffe763,0xffe331,
    0,0x105d18,0x08b208,0x00e300,0,0xb5efb5,0x5aeb5a,0x00e300,
    0,0x105973,0x089ace,0x00c3ff,0,0xb5e7f7,0x5ad3ff,0x00c3ff,
    0,0x103073,0x0845ce,0x0051ff,0,0xbdcbf7,0x7ba2f7,0x0051ff,
    0,0x4a1873,0x840cd6,0x9c08ff,0,0xdebaf7,0xbd5dff,0x9c08ff,
    0,0x848684,0xc6c7c6,0xffffff,0,0x7b868c,0x52595a,0x080c08,
    0,0x874790,0x6d6a80,0x808080,0,0,0,0,
    0,0x784145,0x6b656c,0x808080,0,0,0,0,
    0,0x7d5627,0x73634d,0x808080,0,0,0,0,
    0,0x7d772c,0x737650,0x808080,0,0,0,0,
    0,0x397445,0x59746c,0x808080,0,0,0,0,
    0,0x396f7d,0x59727c,0x808080,0,0,0,0,
    0,0x2d437e,0x53637c,0x808080,0,0,0,0,
    0,0x622c83,0x64507f,0x808080,0,0,0,0,
    0,0x383b3d,0x4f575c,0x808080,0,0,0,0,
    0,0x313031,0x393839,0x394142,0,0x7b868c,0x52595a,0x080c08,
    0,0x8c969c,0x7b827b,0x7b797b,0,0x8c969c,0x7b827b,0x7b797b
};
static uint32_t calls, observed_color, observed_opaque;
static uint8_t *observed_destination;
static uint16_t table[65536], alternate[65536];

static uint16_t pack(uint32_t r, uint32_t g, uint32_t b)
{ return (uint16_t)((r << 11) | (g << 5) | b); }

/* Executable specification of firmware 1.26 GS_DEFAULT_Gamma_argb16. */
static uint32_t stock_argb(uint32_t coverage, uint32_t color,
                           uint8_t *dst, uint32_t opaque)
{
    calls++; observed_color=color; observed_destination=dst; observed_opaque=opaque;
    if (coverage - 1 > 2) return 0;
    unsigned index=coverage+((color>>8)&255)*8+(color&255);
    assert(index<192);
    uint32_t c=palette[index];
    uint16_t out=pack((c>>19)&31,(c>>10)&63,(c>>3)&31);
    memcpy(dst,&out,2);
    return 1;
}

/* Keep stock /256 blending, including its non-identity alpha=0 behavior. */
static uint32_t stock_clut(uint32_t coverage, uint32_t color,
                           uint8_t *dst, uint32_t opaque)
{
    calls++; observed_color=color; observed_destination=dst; observed_opaque=opaque;
    if (!coverage) return 0;
    uint32_t a=color>>24, r=(color&255)>>3, g=((color>>8)&255)>>2;
    uint32_t b=((color>>16)&255)>>3;
    if(a!=255) {
        uint16_t previous; memcpy(&previous,dst,2);
        r=(r*a+(previous>>11)*(255-a))>>8;
        g=(g*a+((previous>>5)&63)*(255-a))>>8;
        b=(b*a+(previous&31)*(255-a))>>8;
    }
    uint16_t out=pack(r,g,b); memcpy(dst,&out,2);
    return 1;
}

static uint32_t map_rgb(uint32_t rgb,const void *context)
{
    assert(!(rgb&0xff000000));
    return (rgb ^ *(const uint32_t *)context) | 0xa5000000;
}
static void check_canaries(const uint8_t *bytes)
{ assert(bytes[0]==0xd3 && bytes[3]==0xb7); }

int main(void)
{
    const uint32_t xor_mask=0x004c8a27;
    for(unsigned i=0;i<65536;i++) { table[i]=(uint16_t)(i^0x39e7); alternate[i]=(uint16_t)(i^0x71c3); }
    struct xz_native_text_map map={table,map_rgb,&xor_mask};
    struct xz_native_text_map second={alternate,map_rgb,&xor_mask};
    struct xz_native_text_map empty={0};
    uint32_t saved_palette[192]; memcpy(saved_palette,palette,sizeof(palette));

    /* Every reachable palette entry, all accepted coverages, and rejects. */
    for(unsigned index=0;index<192;index++) for(unsigned coverage=0;coverage<=4;coverage++) {
        if(coverage>=1 && coverage<=3 && index<coverage) continue;
        uint32_t color=(index>=coverage?index-coverage:0)|0xdead0000;
        uint8_t stock[4]={0xd3,0x4a,0x93,0xb7}, actual[4];
        memcpy(actual,stock,4);
        uint32_t expected_result=stock_argb(coverage,color,stock+1,0x76543210);
        uint32_t before=calls;
        uint32_t result=xz_native_text_argb16(stock_argb,&map,coverage,color,actual+1,0x76543210);
        assert(calls==before+1 && result==expected_result);
        assert(observed_color==color && observed_opaque==0x76543210);
        if(result) {
            uint16_t raw,got; memcpy(&raw,stock+1,2); memcpy(&got,actual+1,2);
            assert(got==table[raw]);
            uint8_t repeated[4]; memcpy(repeated,actual,4);
            assert(xz_native_text_argb16(stock_argb,&map,coverage,color,actual+1,0)==1);
            assert(!memcmp(repeated,actual,4));
            assert(xz_native_text_argb16(stock_argb,&second,coverage,color,actual+1,0)==1);
            assert(xz_native_text_argb16(stock_argb,&map,coverage,color,actual+1,0)==1);
            assert(!memcmp(repeated,actual,4));
        } else { assert(actual[1]==0x4a && actual[2]==0x93); }
        check_canaries(actual); check_canaries(stock);
        memcpy(actual,(uint8_t[]){0xd3,0x4a,0x93,0xb7},4);
        assert(xz_native_text_argb16(stock_argb,NULL,coverage,color,actual+1,0)==expected_result);
        assert(observed_destination==actual+1 && !memcmp(actual,stock,4));
        assert(xz_native_text_argb16(stock_argb,&empty,coverage,color,actual+1,0)==expected_result);
        assert(observed_destination==actual+1);
    }
    const unsigned alphas[]={0,1,127,254,255};
    const uint16_t backgrounds[]={0,0xffff,0xa135,0x07e0};
    for(unsigned a=0;a<5;a++) for(unsigned b=0;b<4;b++) for(unsigned coverage=0;coverage<=4;coverage++) {
        uint32_t color=(alphas[a]<<24)|0x37ba81;
        uint32_t mapped=(color&0xff000000)|((color^xor_mask)&0xffffff);
        uint8_t stock[4]={0xd3,0,0,0xb7},actual[4];
        memcpy(stock+1,&backgrounds[b],2); memcpy(actual,stock,4);
        uint32_t expected=stock_clut(coverage,mapped,stock+1,0);
        assert(xz_native_text_clut8(stock_clut,&map,coverage,color,actual+1,99)==expected);
        assert(observed_color==mapped && observed_destination==actual+1 && observed_opaque==99);
        assert(!memcmp(stock,actual,4)); check_canaries(actual);
        memcpy(stock+1,&backgrounds[b],2); memcpy(actual,stock,4);
        expected=stock_clut(coverage,color,stock+1,0);
        assert(xz_native_text_clut8(stock_clut,NULL,coverage,color,actual+1,0)==expected);
        assert(observed_color==color && !memcmp(stock,actual,4));
    }
    {uint8_t pixel[4]={0xd3,0x12,0x34,0xb7};uint16_t value;
     assert(xz_native_text_argb16_keyed(stock_argb,&map,1,0,3,4,pixel+1,0)==1);
     memcpy(&value,pixel+1,2);assert(value==0);check_canaries(pixel);
     assert(xz_native_text_argb16_keyed(stock_argb,&map,0,0,3,4,pixel+1,0)==1);
     memcpy(&value,pixel+1,2);assert(value==table[0]);check_canaries(pixel);}
    {uint8_t pixel[4]={0xd3,0,0,0xb7};uint16_t value,saved=table[0xffff];table[0xffff]=0;
     assert(xz_native_text_argb16_keyed(stock_argb,&map,1,0,3,0,pixel+1,0)==1);
     memcpy(&value,pixel+1,2);assert(value==1);check_canaries(pixel);table[0xffff]=saved;}
    assert(!memcmp(saved_palette,palette,sizeof(palette)));
    for(unsigned i=0;i<65536;i++) assert(table[i]==(uint16_t)(i^0x39e7));
    puts("native text draw-local color adapters PASS");
    return 0;
}
