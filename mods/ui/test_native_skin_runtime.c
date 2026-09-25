/* Portable integration fixture: no firmware calls, hooks, files or hardware. */
#include "native_skin_runtime.c"
#include <assert.h>
#include <errno.h>
#include <sched.h>

int xz_hook_arm(uint32_t a,const unsigned char b[8],void*c,void**d){(void)a;(void)b;(void)c;(void)d;abort();}
int xz_hook_slot(uint32_t a,uint32_t b,void*c,void**d){(void)a;(void)b;(void)c;(void)d;abort();}
int xz_read_memory(uint32_t a,void*b,size_t c){(void)a;(void)b;(void)c;abort();}
void xz_log(const char *s){(void)s;abort();}
void xz_native_raw_skin_repaint(void){abort();}
int xz_native_window_keys_start(void){abort();}
static int fixture_key_state;static uint16_t fixture_key;
int xz_native_window_key(uintptr_t surface,uint16_t *key){(void)surface;if(key)*key=fixture_key;return fixture_key_state;}

static unsigned repaints;
static void repaint_spy(void){repaints++;}
static pthread_t reclaimer;
static atomic_int reclaim_entered,reclaim_finished;
static unsigned char source_descriptor[68],source_before[68];
static unsigned char *inflight_pack;
static unsigned char fixture_core[32];
static void *reclaim_spy(void *unused){
 (void)unused;atomic_store(&reclaim_entered,1);
 release_retired(take_retired());atomic_store(&reclaim_finished,1);return NULL;
}
static uint32_t image_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2);
 assert(descriptor!=source_descriptor);
 assert(xz_asset_u32((const unsigned char*)descriptor+12)==(uint32_t)(uintptr_t)inflight_pack+PACK_COUNT*44);
 assert(memcmp(source_descriptor,source_before,68)==0);
 assert(pthread_mutex_trylock(&cache_mutex)==EBUSY);
 assert(pthread_create(&reclaimer,NULL,reclaim_spy,NULL)==0);
 while(!atomic_load(&reclaim_entered))sched_yield();
 assert(!atomic_load(&reclaim_finished));
 assert(active_snapshot->pack==inflight_pack);
 return 0x7ac1;
}
static uint32_t fallback_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2);assert(descriptor==source_descriptor);return 0x3abc;
}
static uint32_t keyed_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2&&descriptor!=source_descriptor);
 assert(xz_asset_u32((const unsigned char*)descriptor+12)==(uint32_t)(uintptr_t)keyed_image);
 assert(keyed_image[0]==0);return 0x4abc;
}
static void init_fixture(void){
 atomic_store(&running,1);atomic_store(&status_code,2);
 owner_thread=pthread_self();atomic_store(&owner_ready,1);
 assert(build_maps()==0);
 for(int t=0;t<XZ_THEME_COUNT;t++)for(unsigned p=0;p<65536;p++){
  const uint16_t *lut=map_for(t);assert(lut[lut[p]]==lut[p]);if(!t)assert(lut[p]==p);
 }
 pack_size=PACK_COUNT*44+PACK_COUNT*2;original_pack=calloc(1,pack_size);assert(original_pack);
 original_base=0x10000000;
 for(unsigned i=0;i<PACK_COUNT;i++){
  unsigned char *e=original_pack+i*44;e[4]=1;e[6]=1;
  xz_asset_put32(e+24,2);xz_asset_put32(e+28,i*44);xz_asset_put32(e+32,PACK_COUNT*44+i*2);
  xz_asset_put32(e+36,(uint32_t)pack_size);original_pack[PACK_COUNT*44+i*2]=(unsigned char)i;
 }
}
static struct skin_snapshot *request_and_prepare(int theme){
 xz_native_skin_request(theme);return prepare(theme,request_generation);
}
int main(void){
 init_fixture();unsigned char *source_copy=malloc(pack_size);assert(source_copy);memcpy(source_copy,original_pack,pack_size);
 struct skin_snapshot *a=request_and_prepare(7);assert(a&&publish_snapshot(a));
 assert(strcmp(xz_native_skin_status(),"Preparing native theme")==0);
 /* An idle native epoch, with no input gesture or message, consumes the request. */
 owner_epoch(repaint_spy);assert(repaints==1&&atomic_load(&active_theme)==7);
 assert(strcmp(xz_native_skin_status(),"Native theme ready")==0);
 owner_epoch(repaint_spy);assert(repaints==1);
 unsigned char *first_a=malloc(pack_size);assert(first_a);memcpy(first_a,active_snapshot->pack,pack_size);
 struct skin_snapshot *b=request_and_prepare(10);assert(b&&publish_snapshot(b));
 assert(atomic_load(&active_theme)==7);owner_epoch(repaint_spy);assert(repaints==2&&atomic_load(&active_theme)==10);
 a=request_and_prepare(7);assert(a&&publish_snapshot(a));owner_epoch(repaint_spy);
 assert(repaints==3&&memcmp(first_a,active_snapshot->pack,pack_size)==0);
 assert(memcmp(original_pack,source_copy,pack_size)==0);
 /* Both supersession races: obsolete computation and obsolete published work. */
 b=request_and_prepare(10);assert(b);xz_native_skin_request(8);assert(!publish_snapshot(b));free_snapshot(b);
 b=prepare(8,request_generation);assert(b&&publish_snapshot(b));xz_native_skin_request(7);
 owner_epoch(repaint_spy);assert(repaints==3&&atomic_load(&active_theme)==7&&!pending_snapshot);
 assert(strcmp(xz_native_skin_status(),"Preparing native theme")==0);
 a=prepare(7,request_generation);assert(a&&publish_snapshot(a));owner_epoch(repaint_spy);assert(repaints==4);
 /* Original image draw sees only a cloned descriptor. Cache retirement waits. */
 memset(source_descriptor,0,68);xz_asset_put32(source_descriptor,5);source_descriptor[4]=1;source_descriptor[6]=1;
 source_descriptor[16]=2;xz_asset_put32(source_descriptor+12,original_base+PACK_COUNT*44);
 xz_asset_put32(source_descriptor+44,0x1234);xz_asset_put32(source_descriptor+48,0x5678);
 memcpy(source_before,source_descriptor,68);inflight_pack=active_snapshot->pack;stock_image=image_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x7ac1);
 assert(pthread_join(reclaimer,NULL)==0&&atomic_load(&reclaim_finished));
 assert(active_snapshot->pack==inflight_pack&&memcmp(source_before,source_descriptor,68)==0);
 assert(memcmp(source_copy,original_pack,pack_size)==0);
 /* Unknown type/key inputs are passed untouched, preserving native callbacks. */
 fixture_key_state=1;fixture_key=0;stock_image=keyed_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x4abc);
 for(unsigned alpha=0;alpha<256;alpha++)assert(xz_mods_native_surface_color_v1(0x1234,alpha<<24)==alpha<<24);
 assert(rgba565(rgba_map_keyed(0xffffffff,map_for(9),1,0))!=0);
 assert((rgba_map_keyed(0x7fffffff,map_for(9),1,0)>>24)==0x7f);
 fixture_key_state=-1;stock_image=fallback_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 assert(xz_mods_native_surface_color_v1(0x1234,0xff123456)==0xff123456);fixture_key_state=0;
 source_descriptor[0]=3;stock_image=fallback_spy;assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 source_descriptor[0]=5;source_descriptor[24]=1;source_descriptor[28]=0;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 struct skin_snapshot *stock=request_and_prepare(0);assert(stock&&publish_snapshot(stock));owner_epoch(repaint_spy);
 assert(repaints==5&&xz_mods_native_color_v1(0xab123456)==0xab123456&&xz_mods_native_style_v1()==0);
 assert(memcmp(source_copy,original_pack,pack_size)==0);
 free(first_a);free(source_copy);release_retired(take_retired());free_snapshot(active_snapshot);
 free(original_pack);free(maps);puts("native skin integration: PASS (offline ownership, epochs, supersession, source integrity)");return 0;
}
