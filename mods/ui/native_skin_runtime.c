#define _GNU_SOURCE
#include "native_skin_runtime.h"
#include "native_palette.h"
#include "native_asset_theme.h"
#include "native_asset_view.h"
#include "native_text_theme.h"
#include "native_pixel_glyph.h"
#include "native_raw_skin.h"
#include "native_window_keys.h"
#include "../runtime.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PACK_COUNT 1582u
#define PACK_CAP (32u*1024u*1024u)
struct skin_snapshot { unsigned char *pack; int theme; unsigned generation; struct skin_snapshot *next; };
struct skin_proof {
 uint32_t version,requested,active,epochs,eventcalls,imagecalls,themed,fallbacks;
 uint32_t gamma,text,glyph,glyph_themed,colors,pixels,errors,pack_bytes;
};
__attribute__((visibility("default"))) struct skin_proof xz_mods_native_skin_v1={.version=1};
#define INC(field) __atomic_fetch_add(&xz_mods_native_skin_v1.field,1,__ATOMIC_RELAXED)
static pthread_mutex_t cache_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread,owner_thread;
static atomic_int running,owner_ready,active_theme,status_code;
static unsigned request_generation;
static int requested_theme,started,worker_started;
static unsigned char *original_pack;
static size_t pack_size;
static uint32_t original_base,backend_table[4];
static uint16_t *maps;
static struct skin_snapshot *active_snapshot,*pending_snapshot,*retired;
static _Thread_local unsigned event_depth,wstring_depth,packing_exponent;
static _Thread_local int native_owner;
static _Thread_local int text_key_state;
static _Thread_local uint16_t text_key;
static _Alignas(64) uint16_t keyed_image[800*480];
static int (*stock_event)(void *);
static uint32_t (*stock_image)(void *,const void *,const void *);
static xz_native_text_pixel_fn (*stock_gamma)(uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t (*wstring_fn)(void*,void*,const uint16_t*,uint32_t,uint32_t,uint32_t,const void*,int32_t,int32_t,uint32_t,uint32_t);
static wstring_fn stock_wstring;
static uint32_t (*stock_glyph)(void*,uint32_t,uint32_t,void*,void*);

static int owner(void){
 if(native_owner)return 1;
 if(atomic_load_explicit(&owner_ready,memory_order_acquire)&&pthread_equal(pthread_self(),owner_thread)){native_owner=1;return 1;}
 return 0;
}
static const uint16_t *map_for(int theme){return maps+(size_t)theme*65536u;}
static int current_theme(void){return owner()?atomic_load_explicit(&active_theme,memory_order_relaxed):0;}
int xz_native_skin_on_owner_thread(void){return owner();}
__attribute__((visibility("default"))) int xz_mods_native_style_v1(void){return current_theme();}
static uint32_t rgba_map(uint32_t rgba,const void *context){
 const uint16_t *lut=context;
 uint16_t p=(uint16_t)(((rgba&255)>>3)<<11|(((rgba>>8)&255)>>2)<<5|(((rgba>>16)&255)>>3));
 p=lut[p];
 return (rgba&0xff000000u)|((p>>11)*255u/31u)|((((p>>5)&63)*255u/63u)<<8)|(((p&31)*255u/31u)<<16);
}
__attribute__((visibility("default"))) uint32_t xz_mods_native_color_v1(uint32_t rgba){
 int theme=current_theme();if(!theme||!maps)return rgba;
 INC(colors);return rgba_map(rgba,map_for(theme));
}
static uint16_t rgba565(uint32_t c){return (uint16_t)(((c&255)>>3)<<11|(((c>>8)&255)>>2)<<5|(((c>>16)&255)>>3));}
static uint32_t rgba_map_keyed(uint32_t rgba,const uint16_t *lut,int state,uint16_t key){
 if(state<0||(state>0&&rgba565(rgba)==key))return rgba;
 uint32_t mapped=rgba_map(rgba,lut);
 if(state>0&&rgba565(mapped)==key){
  uint16_t p=key^1;
  mapped=(rgba&0xff000000u)|((p>>11)*255u/31u)|((((p>>5)&63)*255u/63u)<<8)|(((p&31)*255u/31u)<<16);
 }
 return mapped;
}
__attribute__((visibility("default"))) uint32_t xz_mods_native_surface_color_v1(uint32_t surface,uint32_t rgba){
 if(!current_theme())return rgba;
 uint16_t key=0;int state=xz_native_window_key(surface,&key);
 INC(colors);return rgba_map_keyed(rgba,map_for(current_theme()),state,key);
}
__attribute__((visibility("default"))) void xz_mods_native_pixels_v1(uint16_t *pixels,uint32_t count){
 int theme=current_theme();if(!theme||!maps||!pixels||count>536u*268u)return;
 const uint16_t *lut=map_for(theme);for(uint32_t i=0;i<count;i++)pixels[i]=lut[pixels[i]];
 INC(pixels);
}
static uint32_t argb_pixel(uint32_t coverage,uint32_t color,uint8_t *dest,uint32_t opaque){
 int theme=current_theme();
 struct xz_native_text_map m={theme?map_for(theme):NULL,NULL,NULL};
 return xz_native_text_argb16_keyed((xz_native_text_pixel_fn)(uintptr_t)0x157c60,theme?&m:NULL,text_key_state>0,text_key,coverage,color,dest,opaque);
}
static uint32_t clut_pixel(uint32_t coverage,uint32_t color,uint8_t *dest,uint32_t opaque){
 int theme=current_theme();
 uint32_t mapped=theme?rgba_map_keyed(color,map_for(theme),text_key_state,text_key):color;
 return ((xz_native_text_pixel_fn)(uintptr_t)0x157bb8)(coverage,mapped,dest,opaque);
}
static xz_native_text_pixel_fn gamma_hook(uint32_t a,uint32_t b,uint32_t c,uint32_t d){
 xz_native_text_pixel_fn result=stock_gamma(a,b,c,d);
 if(!current_theme()||!wstring_depth||text_key_state<0)return result;
 INC(gamma);
 if((uintptr_t)result==0x157c60)return argb_pixel;
 if((uintptr_t)result==0x157bb8)return clut_pixel;
 return result;
}
static uint32_t wstring_hook(void *window,void *font,const uint16_t *text,uint32_t meta,
 uint32_t a,uint32_t b,const void *clip,int32_t x,int32_t y,uint32_t flags,uint32_t language){
 unsigned saved=packing_exponent;int saved_key_state=text_key_state;uint16_t saved_key=text_key;
 packing_exponent=(meta>>16)&255;wstring_depth++;text_key_state=0;
 if(current_theme()&&window){
  uint32_t core;memcpy(&core,(unsigned char*)window+0x98,4);
  if(core){uint32_t surface;memcpy(&surface,(void*)(uintptr_t)(core+0x1c),4);text_key_state=xz_native_window_key(surface,&text_key);}
 }
 if(current_theme())INC(text);
 uint32_t result=stock_wstring(window,font,text,meta,a,b,clip,x,y,flags,language);
 wstring_depth--;packing_exponent=saved;text_key_state=saved_key_state;text_key=saved_key;return result;
}
static uint32_t glyph_hook(void *font,uint32_t character,uint32_t encoding,void *descriptor,void *metrics){
 int theme=current_theme();
 struct xz_native_text_glyph_scope scope={0};
 unsigned char before[28];
 if((theme==7||theme==10)&&wstring_depth&&text_key_state>=0&&font&&descriptor){
  uint32_t index;memcpy(&index,(unsigned char*)font+28,4);
  if(index<4&&backend_table[index]==0x206fac){
   memcpy(before,descriptor,sizeof(before));scope.enabled=1;scope.inside_wstring=1;
   scope.backend=backend_table[index];scope.packing_exponent=packing_exponent;
   scope.supplied_data=(uint8_t*)(uintptr_t)xz_asset_u32(before);scope.supplied_capacity=xz_asset_u32(before+4);
  }
 }
 uint32_t result=stock_glyph(font,character,encoding,descriptor,metrics);
 if(scope.enabled){
  const unsigned char *p=descriptor;struct xz_native_text_bitmap view;
  view.data=(uint8_t*)(uintptr_t)xz_asset_u32(p);view.capacity=xz_asset_u32(p+4);
  view.bearing_x=(int32_t)xz_asset_u32(p+8);view.bearing_y=(int32_t)xz_asset_u32(p+12);
  view.width=xz_asset_u32(p+16);view.height=xz_asset_u32(p+20);view.stride=xz_asset_u32(p+24);
  INC(glyph);if(xz_native_text_pixel_glyph(&scope,result,character,encoding,&view))INC(glyph_themed);
 }
 return result;
}
static uint32_t image_hook(void *core,const void *descriptor,const void *point){
 if(!current_theme()||!descriptor)return stock_image(core,descriptor,point);
 INC(imagecalls);unsigned char local[68];uint32_t result;
 pthread_mutex_lock(&cache_mutex);
 struct skin_snapshot *s=active_snapshot;
 int mapped=s&&s->theme&&xz_asset_themed_view(descriptor,local,original_pack,pack_size,original_base,(uint32_t)(uintptr_t)s->pack);
 if(mapped&&core){
  uint32_t surface;memcpy(&surface,(unsigned char*)core+0x1c,4);uint16_t key;
  int state=xz_native_window_key(surface,&key);
  if(state<0)mapped=0;
  else if(state>0&&key!=0xf81f){
   unsigned width=xz_asset_u16(local+4),height=xz_asset_u16(local+6);size_t count=(size_t)width*height;
   if(count>800u*480u)mapped=0;
   else{
    uint32_t offset=xz_asset_u32((const unsigned char*)descriptor+12)-original_base;
    const uint16_t *source=(const uint16_t*)(original_pack+offset),*themed=(const uint16_t*)(s->pack+offset);
    for(size_t i=0;i<count;i++)keyed_image[i]=source[i]==key?key:themed[i]==key?(uint16_t)(key^1):themed[i];
    xz_asset_put32(local+12,(uint32_t)(uintptr_t)keyed_image);
   }
  }
 }
 if(mapped){INC(themed);result=stock_image(core,local,point);}
 else{INC(fallbacks);result=stock_image(core,descriptor,point);}
 pthread_mutex_unlock(&cache_mutex);return result;
}
static int consume_pending(void){
 int repaint=0;
 pthread_mutex_lock(&cache_mutex);
 if(pending_snapshot&&pending_snapshot->generation!=request_generation){
  struct skin_snapshot *stale=pending_snapshot;pending_snapshot=NULL;
  stale->next=retired;retired=stale;pthread_cond_signal(&changed);
 }
 if(pending_snapshot){
  int initial_stock=!active_snapshot&&pending_snapshot->theme==0;
  struct skin_snapshot *old=active_snapshot;active_snapshot=pending_snapshot;pending_snapshot=NULL;
  atomic_store_explicit(&active_theme,active_snapshot->theme,memory_order_release);
  __atomic_store_n(&xz_mods_native_skin_v1.active,(uint32_t)active_snapshot->theme,__ATOMIC_RELAXED);
  if(old){old->next=retired;retired=old;}
  INC(epochs);repaint=!initial_stock;pthread_cond_signal(&changed);
 }
 pthread_mutex_unlock(&cache_mutex);
 return repaint;
}
static void native_repaint(void){
 xz_native_raw_skin_repaint();
 ((int(*)(void*))(uintptr_t)0x1852c4)(NULL);
 ((void(*)(void))(uintptr_t)0x1432e8)();
}
static void owner_epoch(void (*repaint)(void)){if(consume_pending())repaint();}
static int event_hook(void *self){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
 if(caller!=0x18ab20||(uintptr_t)self!=0x1b2cc84||*(const uint32_t*)self!=0x3ea424||event_depth)
  return stock_event(self);
 if(!atomic_load_explicit(&owner_ready,memory_order_relaxed)){
  owner_thread=pthread_self();atomic_store_explicit(&owner_ready,1,memory_order_release);
 }
 if(!owner())return stock_event(self);
 event_depth++;INC(eventcalls);owner_epoch(native_repaint);
 int result=stock_event(self);event_depth--;return result;
}
static void free_snapshot(struct skin_snapshot *s){if(s){free(s->pack);free(s);}}
static int publish_snapshot(struct skin_snapshot *s){
 struct skin_snapshot *old=NULL;int published=0;
 pthread_mutex_lock(&cache_mutex);
 if(s->generation==request_generation&&atomic_load(&running)){
  old=pending_snapshot;pending_snapshot=s;published=1;
 }
 pthread_mutex_unlock(&cache_mutex);
 free_snapshot(old);return published;
}
static struct skin_snapshot *take_retired(void){
 pthread_mutex_lock(&cache_mutex);struct skin_snapshot *garbage=retired;retired=NULL;
 pthread_mutex_unlock(&cache_mutex);return garbage;
}
static void release_retired(struct skin_snapshot *garbage){
 while(garbage){struct skin_snapshot *next=garbage->next;free_snapshot(garbage);garbage=next;}
}
static int load_pack(void){
 FILE *f=fopen("/root/gui/pset/imagedata/imagedata.dat","rb");if(!f)return -1;
 if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long length=ftell(f);
 if(length<(long)(PACK_COUNT*44)||length>(long)PACK_CAP||fseek(f,0,SEEK_SET)){fclose(f);return -1;}
 unsigned char *p=malloc((size_t)length);if(!p){fclose(f);return -1;}
 size_t got=fread(p,1,(size_t)length,f);fclose(f);if(got!=(size_t)length){free(p);return -1;}
 uint32_t base;if(xz_read_memory(0x3eabea0,&base,4)||!base||base>UINT32_MAX-(uint32_t)length){free(p);return -1;}
 unsigned char *table=malloc(PACK_COUNT*44);if(!table){free(p);return -1;}
 int valid=!xz_read_memory(base,table,PACK_COUNT*44)&&xz_asset_u32(p+32)==PACK_COUNT*44;
 for(unsigned i=0;valid&&i<PACK_COUNT;i++){
  const unsigned char *entry=p+i*44,*live=table+i*44;
  uint32_t off=xz_asset_u32(entry+32),next=i+1<PACK_COUNT?xz_asset_u32(entry+76):(uint32_t)length;
  uint32_t w=xz_asset_u16(entry+4),h=xz_asset_u16(entry+6);
  valid=w&&h&&w<32768&&xz_asset_u32(entry+24)==2&&xz_asset_u32(entry+28)==i*44&&
   xz_asset_u32(entry+36)==(uint32_t)length&&!(off&1u)&&off>=PACK_COUNT*44&&off<=next&&next<=(uint32_t)length&&
   (uint64_t)off+(uint64_t)w*h*2<=next&&memcmp(entry+4,live+4,4)==0&&memcmp(entry+24,live+24,20)==0;
 }
 free(table);if(!valid){free(p);return -1;}
 original_pack=p;pack_size=(size_t)length;original_base=base;
 for(unsigned i=0;i<4;i++)if(xz_read_memory(0x1b81898+i*40+0xd0,&backend_table[i],4))backend_table[i]=0;
 xz_mods_native_skin_v1.pack_bytes=(uint32_t)pack_size;return 0;
}
static struct skin_snapshot *prepare(int theme,unsigned generation){
 struct skin_snapshot *s=calloc(1,sizeof(*s));if(!s)return NULL;s->theme=theme;s->generation=generation;
 if(!theme)return s;
 s->pack=malloc(pack_size);if(!s->pack){free(s);return NULL;}memcpy(s->pack,original_pack,pack_size);
 for(unsigned i=0;i<PACK_COUNT;i++){
  const unsigned char *e=original_pack+i*44;uint32_t off=xz_asset_u32(e+32);int w=xz_asset_u16(e+4),h=xz_asset_u16(e+6);
  if(!xz_native_asset_theme_render(i,theme,(const uint16_t*)(original_pack+off),w,h,(size_t)w,
   (uint16_t*)(s->pack+off),(size_t)w,(size_t)w*h,map_for(theme),1,0xf81f,0)){free_snapshot(s);return NULL;}
 }
 return s;
}
static int build_maps(void){
 maps=malloc((size_t)XZ_THEME_COUNT*65536u*sizeof(*maps));
 if(!maps)return -1;
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  uint16_t *lut=maps+(size_t)theme*65536u;
  for(unsigned p=0;p<65536;p++)lut[p]=xz_native_palette_pixel(theme,(uint16_t)p);
  /* Every caller maps original source colors. Making output colors fixed points
     would break light themes: Windows ink is black, but source black is paper. */
  lut[0xf81f]=0xf81f;
 }
 return 0;
}
static void *worker(void *unused){
 (void)unused;
 (void)nice(10);
 int base_ready=0;
 for(unsigned attempt=0;attempt<200&&atomic_load(&running);attempt++){
  uint32_t base=0;unsigned char first[44],last[44];
  if(atomic_load_explicit(&owner_ready,memory_order_acquire)&&
     !xz_read_memory(0x3eabea0,&base,4)&&base&&base<=UINT32_MAX-PACK_COUNT*44&&
     !xz_read_memory(base,first,44)&&!xz_read_memory(base+(PACK_COUNT-1)*44,last,44)&&
     xz_asset_u32(first+32)==PACK_COUNT*44&&xz_asset_u32(first+36)>PACK_COUNT*44&&
     xz_asset_u32(last+36)==xz_asset_u32(first+36)){base_ready=1;break;}
  usleep(100000);
 }
 if(!atomic_load(&running))return NULL;
 if(!base_ready||load_pack()||build_maps()){INC(errors);atomic_store(&status_code,3);return NULL;}
 unsigned built=0;
 while(atomic_load(&running)){
  pthread_mutex_lock(&cache_mutex);
  unsigned gen=request_generation;int theme=requested_theme;
  if(gen==built&&!retired){pthread_cond_wait(&changed,&cache_mutex);pthread_mutex_unlock(&cache_mutex);continue;}
  pthread_mutex_unlock(&cache_mutex);
  release_retired(take_retired());
  if(gen==built)continue;
  struct skin_snapshot *s=prepare(theme,gen);built=gen;
  if(!s){INC(errors);atomic_store(&status_code,3);continue;}
  if(publish_snapshot(s)){s=NULL;atomic_store(&status_code,2);}
  free_snapshot(s);
 }
 return NULL;
}
void xz_native_skin_request(int theme){
 theme=xz_theme_id(theme);pthread_mutex_lock(&cache_mutex);
 requested_theme=theme;request_generation++;__atomic_store_n(&xz_mods_native_skin_v1.requested,(uint32_t)theme,__ATOMIC_RELAXED);
 pthread_cond_signal(&changed);pthread_mutex_unlock(&cache_mutex);
}
const char *xz_native_skin_status(void){
 int state=atomic_load(&status_code);
 if(state==3)return "Native theme unavailable";
 if(!state)return "Native theme stopped";
 pthread_mutex_lock(&cache_mutex);
 int ready=active_snapshot&&active_snapshot->generation==request_generation;
 pthread_mutex_unlock(&cache_mutex);
 return ready?"Native theme ready":"Preparing native theme";
}
int xz_native_skin_start(int theme){
 if(started){if(!worker_started||!atomic_load(&running))return -1;xz_native_skin_request(theme);return 0;}
 unsigned char actual[16];
 const unsigned char dirty[16]={0xf8,0x40,0x2d,0xe9,0xcc,0x40,0x9f,0xe5,0x18,0x30,0x94,0xe5,0,0,0x53,0xe3};
 const unsigned char refresh[16]={0x10,0x40,0x2d,0xe9,0xf0,0x05,0x01,0xeb,0x1c,0x40,0x90,0xe5,0x0e,0x06,0x01,0xeb};
 const unsigned char image_guard[8]={0xf0,0x4f,0x2d,0xe9,0x5c,0xd0,0x4d,0xe2};
 const unsigned char text_guard[8]={0x08,0xd0,0x4d,0xe2,0xba,0xc0,0xd0,0xe1};
 const unsigned char glyph_guard[8]={0x30,0x40,0x2d,0xe9,0x14,0xd0,0x4d,0xe2};
 const unsigned char gamma_guard[8]={0x02,0x20,0x42,0xe2,0x07,0x00,0x52,0xe3};
 if(xz_read_memory(0x1852c4,actual,16)||memcmp(actual,dirty,16)||
    xz_read_memory(0x1432e8,actual,16)||memcmp(actual,refresh,16))goto fail;
 if(xz_native_window_keys_start())goto fail;
 if(xz_hook_arm(0x159ad4,image_guard,(void*)image_hook,(void**)&stock_image)||
    xz_hook_arm(0x15df68,text_guard,(void*)wstring_hook,(void**)&stock_wstring)||
    xz_hook_arm(0x207a70,glyph_guard,(void*)glyph_hook,(void**)&stock_glyph)||
    xz_hook_arm(0x157cc0,gamma_guard,(void*)gamma_hook,(void**)&stock_gamma)||
    xz_hook_slot(0x3ea45c,0x18194c,(void*)event_hook,(void**)&stock_event))goto fail;
 started=1;atomic_store(&running,1);atomic_store(&status_code,1);xz_native_skin_request(theme);
 if(pthread_create(&worker_thread,NULL,worker,NULL)){atomic_store(&running,0);goto fail;}worker_started=1;
 return 0;
fail:INC(errors);atomic_store(&status_code,3);return -1;
}
void xz_native_skin_stop(void){
 if(!started)return;
 atomic_store(&running,0);pthread_mutex_lock(&cache_mutex);pthread_cond_broadcast(&changed);pthread_mutex_unlock(&cache_mutex);
 if(worker_started){pthread_join(worker_thread,NULL);worker_started=0;}atomic_store(&status_code,0);
 struct skin_snapshot *stock=calloc(1,sizeof(*stock));
 if(stock){
  pthread_mutex_lock(&cache_mutex);struct skin_snapshot *old=pending_snapshot;stock->generation=++request_generation;requested_theme=0;pending_snapshot=stock;
  pthread_mutex_unlock(&cache_mutex);free_snapshot(old);
 }else{INC(errors);atomic_store(&status_code,3);}
 /* Hooks and stable maps remain valid until the containing runtime is unloaded.
    The next native event pass restores stock through the normal repaint path.
    Stopping the worker must not free memory potentially used by native drawing. */
}
