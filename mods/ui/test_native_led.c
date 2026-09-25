#ifdef NDEBUG
#error LED acceptance requires assertions
#endif
#include "native_led.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;static int bank;
static int color(int deck,int pad,uint32_t *rgb,int *enabled) {
 assert(deck==0);assert(pad>=0&&pad<8);calls++;
 if(pad<bank*4||pad>=bank*4+4)return 0;
 *rgb=0x123456;*enabled=pad!=bank*4+1;return 1;
}
static void put(unsigned char *p,uint32_t v) {memcpy(p,&v,4);}
static uint32_t word(const unsigned char *p) {uint32_t v;memcpy(&v,p,4);return v;}
static int stem_color(int deck,int pad,uint32_t *rgb,int *enabled) {
 assert(deck==0);
 if(pad>=4)return 0;
 *rgb=xz_stem_hardware_color(pad<3?2-pad:3);*enabled=1;return 1;
}
int main(void) {
 unsigned char packet[8*XZ_LED_RECORD_BYTES],saved[sizeof(packet)];
 memset(packet,0,sizeof(packet));
 for(unsigned i=0;i<8;i++) {
  unsigned char *p=packet+i*XZ_LED_RECORD_BYTES;
  put(p,0x12+i);put(p+4,1);put(p+0x10,2);put(p+0x14,1);
  put(p+0x1c,500);p[0x28]=0xee;
 }
 /* C is stock-forced; inactive E-H and channel2 must stay native. */
 packet[2*XZ_LED_RECORD_BYTES+0x18]=1;
 memcpy(saved,packet,sizeof(packet));
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,color)==3&&calls==7);
 assert(word(packet+0x10)==1&&word(packet+0x14)==0&&word(packet+0x1c)==0);
 assert(packet[0x28]==0x12&&packet[0x29]==0x34&&packet[0x2a]==0x56&&!packet[0x18]);
 assert(word(packet+XZ_LED_RECORD_BYTES+0x10)==1);
 assert(word(packet+XZ_LED_RECORD_BYTES+0x14)==1);
 assert(packet[XZ_LED_RECORD_BYTES+0x28]==0x12&&packet[XZ_LED_RECORD_BYTES+0x29]==0x34&&packet[XZ_LED_RECORD_BYTES+0x2a]==0x56);
 assert(!memcmp(packet+2*XZ_LED_RECORD_BYTES,saved+2*XZ_LED_RECORD_BYTES,XZ_LED_RECORD_BYTES));
 assert(!memcmp(packet+4*XZ_LED_RECORD_BYTES,saved+4*XZ_LED_RECORD_BYTES,4*XZ_LED_RECORD_BYTES));
 memcpy(packet,saved,sizeof(packet));bank=1;
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,color)==4);
 assert(!memcmp(packet,saved,4*XZ_LED_RECORD_BYTES));
 for(unsigned i=4;i<8;i++)assert(word(packet+i*XZ_LED_RECORD_BYTES+0x14)==(i==5?1u:0u));
 memcpy(packet,saved,sizeof(packet));calls=0;
 assert(!xz_native_led_apply(packet,sizeof(packet),8,2,color)&&!calls);
 assert(!memcmp(packet,saved,sizeof(packet)));
 assert(!xz_native_led_apply(packet,sizeof(packet)-1,8,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),257,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),8,0,color));
 assert(!xz_native_led_apply(NULL,sizeof(packet),8,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),8,1,NULL));
 assert(!memcmp(packet,saved,sizeof(packet)));
 /* Firmware PadColorCode palette entries 21, 1, 42, 64 at 0x47b740.
    Secondary channels must stay zero on the three colored physical pads. */
 const unsigned char native_rgb[4][3]={{0,255,0},{0,0,255},{255,0,0},{255,255,255}};
 packet[2*XZ_LED_RECORD_BYTES+0x18]=0;
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,stem_color)==4);
 for(unsigned i=0;i<4;i++) {
  assert(!memcmp(packet+i*XZ_LED_RECORD_BYTES+0x28,native_rgb[i],3));
  assert(word(packet+i*XZ_LED_RECORD_BYTES+0x14)==0);
 }
 puts("PASS native LED packet bounds, colors, mute, stock ownership and channel isolation");
}
