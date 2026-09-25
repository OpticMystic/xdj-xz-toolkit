#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static uintptr_t test_caller;
#define __builtin_return_address(depth) ((void*)test_caller)
#include "native_wave_runtime.c"
#undef __builtin_return_address
static uint16_t buffer[320*34+8];
static int result=0,fixture_pitch=640,unlocks,maps,theme=1;
int xz_native_raw_skin_start(int(*a)(void*,void**,int*),int(*b)(void*)){(void)a;(void)b;return 0;}
void xz_native_raw_skin_locked(void *w,void **p,int *s,uintptr_t c,int r){(void)w;(void)p;(void)s;(void)c;(void)r;}
void xz_native_raw_skin_before_unlock(void *w){(void)w;}
static int lock(void *w,void **pixels,int *pitch){assert(w==(void*)0x1000);*pixels=buffer;*pitch=fixture_pitch;return result;}
static int unlock(void *w){assert(w==(void*)0x2000);unlocks++;return 17;}
int xz_read_memory(uint32_t address,void *out,size_t size){
 if(address!=0x1000||size!=64)return -1;
 uint32_t values[16]={0};values[0]=300|(34u<<16);values[2]=9;values[7]=0x2000;memcpy(out,values,64);return 0;
}
int xz_mods_native_style_v1(void){return theme;}
void xz_mods_native_pixels_v1(uint16_t *pixels,uint32_t count){assert(count==300*34);maps++;for(uint32_t i=0;i<count;i++)pixels[i]^=0xffff;}
void xz_log(const char *s){(void)s;}
int xz_hook_arm(uint32_t a,const unsigned char guard[8],void *r,void **o){(void)a;(void)guard;(void)r;(void)o;return 0;}
int main(void){
 stock_lock=lock;stock_unlock=unlock;
 for(int scenario=0;scenario<6;scenario++){
  for(unsigned i=0;i<sizeof(buffer)/sizeof(*buffer);i++)buffer[i]=0x1234;
  void *pixels=NULL;int pitch=0;test_caller=scenario==1?0x22f7bc:0x22ee4c;
  fixture_pitch=scenario==2?599:640;result=scenario==3?1:0;theme=scenario==4?0:1;
  if(scenario==5)test_caller=0x111000;
  int before=maps;assert(lock_hook((void*)0x1000,&pixels,&pitch)==result);
  assert(unlock_hook((void*)0x2000)==17);
  int expected=scenario<2;
  assert(maps-before==expected);
  for(int y=0;y<34;y++)for(int x=0;x<320;x++)assert(buffer[y*320+x]==(expected&&x<300?0xedcb:0x1234));
  for(unsigned i=320*34;i<sizeof(buffer)/sizeof(*buffer);i++)assert(buffer[i]==0x1234);
 }
 assert(unlocks==6);puts("PASS footer waveform provenance, full-row styling, pitch padding and failed-lock isolation");
}
