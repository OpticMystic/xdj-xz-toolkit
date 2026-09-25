#include "native_led.h"
#include <string.h>

static uint32_t word(const unsigned char *p) {
 uint32_t v;memcpy(&v,p,sizeof(v));return v;
}
static void put(unsigned char *p,uint32_t v) {memcpy(p,&v,sizeof(v));}

size_t xz_native_led_apply(void *records,size_t bytes,unsigned count,
                           unsigned channel,xz_led_color_fn color) {
 unsigned char *p=records;size_t changed=0;
 if(!p||!color||channel<1||channel>2||count>XZ_LED_MAX_RECORDS||
    bytes<(size_t)count*XZ_LED_RECORD_BYTES)return 0;
 for(unsigned i=0;i<count;i++,p+=XZ_LED_RECORD_BYTES) {
  uint32_t id=word(p),rgb=0;int enabled=0;
  /* Forced native status has priority (e.g. device caution/test state). */
  if(word(p+4)!=channel||id<0x12||id>0x19||p[0x18])continue;
  if(!color((int)channel-1,(int)id-0x12,&rgb,&enabled))continue;
  put(p+8,0);put(p+0xc,0);
  put(p+0x10,1);put(p+0x14,enabled?0:1);
  put(p+0x1c,0);put(p+0x20,0);put(p+0x24,0);
  p[0x28]=(unsigned char)((rgb>>16)&255u);
  p[0x29]=(unsigned char)((rgb>>8)&255u);
  p[0x2a]=(unsigned char)(rgb&255u);
  /* Leave force clear so the next stock update can restore ordinary cues. */
  changed++;
 }
 return changed;
}

#ifndef XZ_LED_PORTABLE_TEST
#include "../runtime.h"
#include "../ui_runtime.h"
#if !defined(__arm__) || defined(__aarch64__)
#error Native LED adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ LED pointer ABI");
typedef void (*xz_stock_led_fn)(void *,void *);
static xz_stock_led_fn original_led;
static int enabled;
/* Bounded diagnostic counters; no input-thread logging or packet retention. */
__attribute__((visibility("default"))) uint32_t xz_mods_led_trace_v1[12] = {1};
#define TRACE(i) __atomic_fetch_add(&xz_mods_led_trace_v1[i],1,__ATOMIC_RELAXED)

static void led_hook(void *player,void *packet) {
 unsigned char *p=packet;uint16_t count;uint32_t pointer,innards,mode;unsigned channel;
 original_led(player,packet);
 TRACE(1);
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!player||!packet)return;
 channel=((const unsigned char*)player)[0x26];
 if(channel<1||channel>2){TRACE(2);return;}
 memcpy(&innards,(const unsigned char*)player+0x138,4);
 if(!innards||(innards&3)||innards>UINT32_MAX-0x84){TRACE(3);return;}
 if(((const unsigned char*)(uintptr_t)innards)[0x26]!=channel){TRACE(4);return;}
 memcpy(&mode,(const unsigned char*)(uintptr_t)innards+0x80,4);
 xz_ui_runtime_pad_native_page((int)channel-1,mode==0?0:(mode>=2&&mode<=4?(int)mode-1:-1));
 memcpy(&count,p+8,2);memcpy(&pointer,p+0xc,4);
 __atomic_store_n(&xz_mods_led_trace_v1[5],count,__ATOMIC_RELAXED);
 __atomic_store_n(&xz_mods_led_trace_v1[6],mode,__ATOMIC_RELAXED);
 if(!pointer||(pointer&3)||count>XZ_LED_MAX_RECORDS||
    pointer>UINT32_MAX-(uint32_t)count*XZ_LED_RECORD_BYTES)return;
 for(unsigned i=0;i<count;i++) {
  const unsigned char *entry=(const unsigned char*)(uintptr_t)pointer+i*XZ_LED_RECORD_BYTES;
  if(word(entry)>=0x12&&word(entry)<=0x19) {
   TRACE(7);
   if(word(entry+4)==channel) {TRACE(8);if(entry[0x18])TRACE(9);}
  }
 }
 size_t changed=xz_native_led_apply((void*)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,
                      count,channel,
                      xz_ui_runtime_pad_color);
 __atomic_fetch_add(&xz_mods_led_trace_v1[10],(uint32_t)changed,__ATOMIC_RELAXED);
}

int xz_native_led_start(void) {
 static const unsigned char guard[8]={0xec,0x3e,0x9f,0xe5,0xf0,0x4f,0x2d,0xe9};
 unsigned char actual[8];
 if(!original_led) {
  if(xz_read_memory(0x27f074,actual,sizeof(actual))||memcmp(actual,guard,sizeof(guard)))return -1;
  if(xz_hook_slot(0x478d3c,0x27f074,(void*)led_hook,(void**)&original_led))return -1;
 }
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);return 0;
}
void xz_native_led_stop(void) {__atomic_store_n(&enabled,0,__ATOMIC_RELEASE);}
#endif
