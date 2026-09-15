#ifdef NDEBUG
#error Native decode acceptance requires active assertions
#endif
#include "native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    struct xz_cue_native n={{0},1};
    unsigned char self[0x88]={0},input[12]={0};
    struct xz_cue_event e;
    uint32_t engine=0x12345678;
    memcpy(self+0x30,&engine,4);
    for(int channel=1;channel<=2;channel++)for(int pad=0;pad<8;pad++) {
        uint16_t key=(uint16_t)(0x4119+pad);memcpy(input+8,&key,2);
        self[0x26]=(unsigned char)channel;
        for(int i=0;i<3;i++) {
            input[11]=(unsigned char)(i==0?0:i+1);
            assert(xz_cue_native_decode(&n,self,input,&e));
            assert(e.deck==channel-1&&e.pad==pad&&e.operation==input[11]&&e.hotcue_mode&&!e.play);
            assert(e.pad_page==0&&!e.shift&&e.mode_button==-1&&!e.sync);
            assert((uintptr_t)n.engine_if[e.deck]==engine);
        }
    }
    self[0x80]=1;input[11]=0x12;
    assert(xz_cue_native_decode(&n,self,input,&e)&&!e.hotcue_mode&&e.operation==2);
    assert(e.pad_page==-1);
    for(int page=0;page<4;page++) {
        uint16_t key=(uint16_t)(0x4114+page);
        uint32_t mode=(uint32_t)(page==0?0:page+1);
        memcpy(input+8,&key,2);memcpy(self+0x80,&mode,4);
        for(int shifted=0;shifted<=1;shifted++) {
            self[0x34]=(unsigned char)shifted;
            memset(&e,0x55,sizeof(e));
            assert(xz_cue_native_decode(&n,self,input,&e));
            assert(e.pad_page==page&&e.mode_button==page&&e.shift==shifted);
            assert(e.pad==-1&&!e.sync&&!e.play&&e.hotcue_mode==(page==0));
        }
    }
    uint16_t sync=0x4313;memcpy(input+8,&sync,2);
    for(int op=0;op<=3;op++) {
        input[11]=(unsigned char)op;
        assert(xz_cue_native_decode(&n,self,input,&e));
        assert(e.sync&&e.shift&&e.mode_button==-1&&e.pad==-1&&e.operation==op);
    }
    uint32_t unknown=0x100;memcpy(self+0x80,&unknown,4);
    assert(xz_cue_native_decode(&n,self,input,&e)&&e.pad_page==-1&&!e.hotcue_mode);
    uint16_t play=0x4101;memcpy(input+8,&play,2);
    assert(xz_cue_native_decode(&n,self,input,&e)&&e.play&&e.pad==-1);
    self[0x26]=0;assert(!xz_cue_native_decode(&n,self,input,&e));
    self[0x26]=3;assert(!xz_cue_native_decode(&n,self,input,&e));
    self[0x26]=1;n.verified=0;assert(!xz_cue_native_decode(&n,self,input,&e));
    assert(e.deck==-1&&e.pad==-1&&e.pad_page==-1&&e.mode_button==-1&&!e.shift&&!e.sync);
    assert(!xz_cue_native_decode(NULL,self,input,&e));
    assert(!xz_cue_native_decode(&n,self,input,NULL));
    puts("PASS actual ARM decoder: channels1/2, pads, four pages, shift, sync, operations and rejection");
}
