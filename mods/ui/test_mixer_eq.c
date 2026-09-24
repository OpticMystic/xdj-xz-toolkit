#include "mixer_eq.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Mixer EQ acceptance requires assertions
#endif
int main(void){
    unsigned ids[2][3]={{0x10e,0x10a,0x106},{0x10f,0x10b,0x107}};
    for(int d=0;d<2;d++)for(int s=0;s<3;s++){
        int deck=-1,stem=-1;float gain=-1;
        assert(xz_eq_decode(ids[d][s]<<16,&deck,&stem,&gain)&&deck==d&&stem==s&&gain==0);
        assert(xz_eq_decode((ids[d][s]<<16)|256,&deck,&stem,&gain)&&gain==.5f);
        assert(xz_eq_decode((ids[d][s]<<16)|512,&deck,&stem,&gain)&&gain==1);
        assert(xz_eq_decode((ids[d][s]<<16)|1023,&deck,&stem,&gain)&&gain==1);
        assert(!xz_eq_decode((ids[d][s]<<16)|1024,&deck,&stem,&gain));
    }
    int d,s;float gain;assert(!xz_eq_decode(0x1040200,&d,&s,&gain));
    assert(xz_eq_allowed(0x05000000,3)==3);
    assert(xz_eq_allowed(0x04000000,3)==2);
    assert(xz_eq_allowed(0x09000000,3)==1);
    assert(!xz_eq_allowed(0x0a000000,3));
    assert(xz_eq_allowed(0x05000000,1)==1);
    assert(xz_eq_allowed(0x05000000,2)==2);
    assert(!xz_eq_allowed(0x05000000,0));
    struct xz_eq_pickup p={0};assert(!xz_eq_pickup(&p,0,1));assert(!xz_eq_pickup(&p,.5f,1));
    assert(xz_eq_pickup(&p,1,1));assert(xz_eq_pickup(&p,.25f,1));
    p=(struct xz_eq_pickup){0};assert(!xz_eq_pickup(&p,1,.5f));assert(xz_eq_pickup(&p,.25f,.5f));
    p=(struct xz_eq_pickup){0};assert(!xz_eq_pickup(&p,0,.5f));assert(xz_eq_pickup(&p,.75f,.5f));
    puts("PASS native ADC mapping, unity centre, both deck input locks and pickup without gain jumps");
}
