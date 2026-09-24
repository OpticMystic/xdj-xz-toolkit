#include "mixer_eq.h"
#include <math.h>
int xz_eq_decode(uint32_t report,int *deck,int *stem,float *gain){
    unsigned id=report>>16,value=report&0xffff;
    if(value>1023)return 0;
    switch(id){
    case 0x106:*deck=0;*stem=2;break;case 0x10a:*deck=0;*stem=1;break;case 0x10e:*deck=0;*stem=0;break;
    case 0x107:*deck=1;*stem=2;break;case 0x10b:*deck=1;*stem=1;break;case 0x10f:*deck=1;*stem=0;break;
    default:return 0;
    }
    *gain=value>=512?1.0f:(float)value/512.0f;return 1;
}
unsigned xz_eq_allowed(uint32_t switches,unsigned local_usb_decks){
    return ((((switches>>24)&3)==1?1u:0u)|(((switches>>26)&3)==1?2u:0u))&local_usb_decks&3u;
}
int xz_eq_pickup(struct xz_eq_pickup *p,float input,float current){
    if(!isfinite(input)||!isfinite(current)||input<0||input>1||current<0||current>1)return 0;
    if(!p->acquired&&(fabsf(input-current)<=.02f||(p->seen&&
            ((p->previous<=current&&input>=current)||(p->previous>=current&&input<=current)))))p->acquired=1;
    p->previous=input;p->seen=1;return p->acquired;
}
