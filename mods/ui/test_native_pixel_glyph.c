/* SPDX-License-Identifier: MIT */
#include "native_pixel_glyph.h"
#include <assert.h>
#include <stdio.h>

#ifdef NDEBUG
#error "Native glyph acceptance requires active assertions"
#endif

static unsigned pixel(const uint8_t *raster,unsigned stride,unsigned x,unsigned y)
{ return (raster[y*stride+x/4]>>(6-(x%4)*2))&3; }

int main(void)
{
    uint8_t storage[191];
    struct xz_native_text_bitmap bitmap={storage+1,189,2,-3,21,27,7};
    struct xz_native_text_glyph_scope scope={1,1,0x206fac,1,storage+1,189};
    const unsigned encodings[]={1,2,14,16};
    for(unsigned encoding=0;encoding<4;encoding++) for(unsigned ch=32;ch<=126;ch++) {
        struct xz_native_text_bitmap b=bitmap;
        if(encodings[encoding]==2) { b.width=11; b.height=23; b.stride=3; }
        memset(storage,0x5a,sizeof(storage));
        struct xz_native_text_bitmap before=b;
        assert(xz_native_text_pixel_glyph(&scope,0,ch,encodings[encoding],&b)==1);
        assert(!memcmp(&b,&before,sizeof(b)));
        assert(storage[0]==0x5a);
        for(size_t i=1+b.stride*b.height;i<sizeof(storage);i++) assert(storage[i]==0x5a);
        unsigned scale=b.width/5;
        if(scale>b.height/7)scale=b.height/7;
        unsigned left=(b.width-scale*5)/2,top=(b.height-scale*7)/2;
        for(unsigned y=0;y<b.height;y++) for(unsigned x=0;x<b.stride*4;x++) {
            unsigned expected=0;
            if(x>=left && x<left+scale*5 && y>=top && y<top+scale*7)
                expected=(xz_pixel_font_row(ch,(y-top)/scale)>>(4-(x-left)/scale))&1 ?3:0;
            assert(pixel(b.data,b.stride,x,y)==expected);
        }
        uint8_t first[191]; memcpy(first,storage,sizeof(first));
        assert(xz_native_text_pixel_glyph(&scope,0,ch,encodings[encoding],&b)==1);
        assert(!memcmp(first,storage,sizeof(first)));
    }
    /* Every eligibility field independently fails closed with unchanged raster. */
    for(unsigned rejection=0;rejection<20;rejection++) {
        struct xz_native_text_bitmap b=bitmap;
        struct xz_native_text_glyph_scope s=scope;
        unsigned result=0,ch='A',encoding=1;
        switch(rejection) {
        case 0:s.enabled=0;break;
        case 1:s.inside_wstring=0;break;
        case 2:s.backend++;break;
        case 3:s.packing_exponent=0;break;
        case 4:s.packing_exponent=2;break;
        case 5:s.supplied_data++;break;
        case 6:s.supplied_capacity--;break;
        case 7:b.capacity=s.supplied_capacity=188;break;
        case 8:b.width=4;break;
        case 9:b.width=29;break;
        case 10:b.height=26;break;
        case 11:b.stride=6;break;
        case 12:ch=31;break;
        case 13:ch=127;break;
        case 14:ch=0x65e5;break;
        case 15:encoding=3;break;
        case 16:encoding=2;break;
        case 17:result=1;break;
        case 18:b.data=NULL;break;
        case 19:ch=0x152;break;
        }
        memset(storage,0x5a,sizeof(storage));
        uint8_t before[191];memcpy(before,storage,sizeof(before));
        assert(!xz_native_text_pixel_glyph(&s,result,ch,encoding,&b));
        assert(!memcmp(storage,before,sizeof(storage)));
    }
    assert(!xz_native_text_pixel_glyph(NULL,0,'A',1,&bitmap));
    assert(!xz_native_text_pixel_glyph(&scope,0,'A',1,NULL));
    puts("native pixel glyph guarded raster replacement PASS");
    return 0;
}
