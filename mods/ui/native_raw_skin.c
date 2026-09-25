#include "native_raw_skin.h"
#include "native_skin_runtime.h"
#include "native_window_keys.h"
#include "../runtime.h"
#include <pthread.h>
#include <string.h>
#include <stddef.h>

extern int xz_native_skin_on_owner_thread(void);
#define CACHE_COUNT 32
#define KEY565 0xf81f
#define SENTINEL 0x1234
struct descriptor {uintptr_t gr,hw;unsigned width,height;};
struct marker {struct descriptor d;int pitch;unsigned valid;uint16_t raw[8*48];};
struct transaction {struct descriptor d;uint16_t *actual;int pitch,kind,slot;};
static struct marker cache[CACHE_COUNT];
static struct transaction pending;
static _Alignas(64) uint16_t alert_scratch[1024*480];
static pthread_mutex_t registry_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int repainting;
static int (*original_lock)(void *,void **,int *);
static int (*original_unlock)(void *);
static int (*original_destroy)(void *);
__attribute__((visibility("default"))) struct xz_native_raw_skin_proof xz_native_raw_skin_v1={1,0,0,0,0,0,0,0,0};
static void enter(void){pthread_mutex_lock(&registry_mutex);}
static void leave(void){pthread_mutex_unlock(&registry_mutex);}
static void count(uint32_t *value){__atomic_fetch_add(value,1,__ATOMIC_RELAXED);}
static int read_descriptor(uintptr_t gr,struct descriptor *d,int locked){
 uint32_t v[16];
 if(!gr||gr>UINT32_MAX||xz_read_memory((uint32_t)gr,v,sizeof(v)))return 0;
 unsigned w=v[0]&65535,h=v[0]>>16;
 if(!w||w>800||!h||h>480||v[2]!=9||!v[7]||!(v[6]&0x40000000u)||
    (locked&&!(v[6]&0x01000000u)))return 0;
 *d=(struct descriptor){gr,v[7],w,h};return 1;
}
static int same(struct descriptor a,struct descriptor b){return a.gr==b.gr&&a.hw==b.hw&&a.width==b.width&&a.height==b.height;}
static int pitch_valid(const struct descriptor *d,int pitch,void *pixels){return pixels&&!((uintptr_t)pixels&1)&&pitch>0&&pitch<=2048&&!(pitch&1)&&(unsigned)pitch>=d->width*2;}
static int classify(uintptr_t caller,unsigned *height,int *fresh){
 *height=0;*fresh=0;
 switch(caller){
 case 0x147bc0:case 0x147dec:case 0x148444:return 2;
 case 0x22f384:case 0x22f5f0:*height=48;*fresh=1;return 1;
 case 0x22fb18:case 0x22fc38:*height=48;return 1;
 case 0x23f64c:case 0x23fa1c:*height=18;*fresh=1;return 1;
 case 0x23ebe0:case 0x23ead0:*height=18;return 1;
 default:return 0;
 }
}
static void copy_raw(struct marker *m,uint16_t *actual,int pitch,int save){
 for(unsigned y=0;y<m->d.height;y++){
  uint16_t *row=(uint16_t *)((unsigned char *)actual+(size_t)y*(unsigned)pitch);
  if(save)memcpy(m->raw+y*8,row,16);else memcpy(row,m->raw+y*8,16);
 }
}
static void paint_marker(struct marker *m,uint16_t *actual,int pitch){
 uint16_t themed[8*48];unsigned n=m->d.height*8;
 memcpy(themed,m->raw,n*2);xz_mods_native_pixels_v1(themed,n);
 for(unsigned y=0;y<m->d.height;y++){
  uint16_t *row=(uint16_t *)((unsigned char *)actual+(size_t)y*(unsigned)pitch);
  for(unsigned x=0;x<8;x++){unsigned at=y*8+x;row[x]=m->raw[at]==KEY565?KEY565:themed[at];}
 }
}
static int destroy_hook(void *gr){
 xz_native_window_keys_forget(gr);
 enter();
 for(unsigned i=0;i<CACHE_COUNT;i++)if(cache[i].d.gr==(uintptr_t)gr){memset(&cache[i],0,sizeof(cache[i]));count(&xz_native_raw_skin_v1.evictions);}
 if(pending.d.gr==(uintptr_t)gr)memset(&pending,0,sizeof(pending));
 leave();return original_destroy(gr);
}
int xz_native_raw_skin_start(int (*lock)(void *,void **,int *),int (*unlock)(void *)){
 static const unsigned char guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
 if(!lock||!unlock)return -1;
 if(!original_destroy&&xz_hook_arm(0x1576a0,guard,(void *)destroy_hook,(void **)&original_destroy))return -1;
 original_lock=lock;original_unlock=unlock;return 0;
}
void xz_native_raw_skin_locked(void *gr,void **pixels,int *pitch,uintptr_t caller,int result){
 unsigned height;int fresh,kind=classify(caller,&height,&fresh);
 if(!kind||!xz_native_skin_on_owner_thread()||repainting)return;
 enter();struct descriptor d;
 if(result!=0||!pixels||!pitch||pending.kind||!read_descriptor((uintptr_t)gr,&d,1)||!pitch_valid(&d,*pitch,*pixels)||
    (kind==1&&(d.width!=8||d.height!=height))){count(&xz_native_raw_skin_v1.rejected);leave();return;}
 int slot=-1;
 if(kind==1){
  for(unsigned i=0;i<CACHE_COUNT;i++)if(cache[i].d.gr==d.gr){slot=(int)i;break;}
  if(slot>=0&&(!same(cache[slot].d,d)||cache[slot].pitch!=*pitch))memset(&cache[slot],0,sizeof(cache[slot]));
  int seed=!fresh&&(slot<0||!cache[slot].valid);
  if(seed&&xz_mods_native_style_v1()!=0){count(&xz_native_raw_skin_v1.rejected);leave();return;}
  if(slot<0)for(unsigned i=0;i<CACHE_COUNT;i++)if(!cache[i].d.gr){slot=(int)i;break;}
  if(slot<0){count(&xz_native_raw_skin_v1.cache_full);leave();return;}
  struct marker *m=&cache[slot];
  if(fresh||seed){memset(m,0,sizeof(*m));m->d=d;m->pitch=*pitch;if(seed){copy_raw(m,*pixels,*pitch,1);m->valid=1;}}
  else copy_raw(m,*pixels,*pitch,0);
 }
 pending=(struct transaction){d,*pixels,*pitch,kind,slot};
 if(kind==2){
  size_t n=(size_t)(*pitch/2)*d.height;
  for(size_t i=0;i<n;i++)alert_scratch[i]=SENTINEL;
  *pixels=alert_scratch;
 }
 count(&xz_native_raw_skin_v1.captures);leave();
}
void xz_native_raw_skin_before_unlock(void *hw){
 if(!xz_native_skin_on_owner_thread()||repainting)return;
 enter();
 if(!pending.kind||pending.d.hw!=(uintptr_t)hw){leave();return;}
 struct transaction p=pending;struct descriptor d;memset(&pending,0,sizeof(pending));
 if(!read_descriptor(p.d.gr,&d,1)||!same(p.d,d)||!pitch_valid(&d,p.pitch,p.actual)){
  if(p.kind==1)memset(&cache[p.slot],0,sizeof(cache[p.slot]));
  count(&xz_native_raw_skin_v1.rejected);leave();return;
 }
 if(p.kind==1){
  struct marker *m=&cache[p.slot];copy_raw(m,p.actual,p.pitch,1);m->valid=1;paint_marker(m,p.actual,p.pitch);count(&xz_native_raw_skin_v1.markers);
 }else{
  uint16_t colors[2]={0xffff,0xf800};xz_mods_native_pixels_v1(colors,2);
  uint16_t key=KEY565;int key_state=xz_native_window_key_for_hw(d.hw,&key);
  for(unsigned y=0;y<d.height;y++){
   uint16_t *row=(uint16_t *)((unsigned char *)p.actual+(size_t)y*(unsigned)p.pitch);
   const uint16_t *source=alert_scratch+(size_t)y*(unsigned)(p.pitch/2);
   for(unsigned x=0;x<d.width;x++){
    uint16_t v=source[x];if(v==SENTINEL)continue;
    if(key_state<0||(key_state>0&&v==key)){}
    else if(v==0xffff)v=colors[0];else if(v==0xf800)v=colors[1];else if(v!=KEY565)count(&xz_native_raw_skin_v1.unknown_colors);
    row[x]=v;
   }
  }
  count(&xz_native_raw_skin_v1.alerts);
 }
 leave();
}
void xz_native_raw_skin_repaint(void){
 if(!original_lock||!original_unlock||!xz_native_skin_on_owner_thread()||repainting)return;
 enter();if(pending.kind){count(&xz_native_raw_skin_v1.rejected);leave();return;}
 repainting=1;
 for(unsigned i=0;i<CACHE_COUNT;i++){
  struct marker *m=&cache[i];if(!m->valid)continue;
  struct descriptor d;
  if(!read_descriptor(m->d.gr,&d,0)||!same(d,m->d)){memset(m,0,sizeof(*m));count(&xz_native_raw_skin_v1.rejected);continue;}
  void *pixels=NULL;int pitch=0;
  if(original_lock((void *)m->d.gr,&pixels,&pitch)!=0){count(&xz_native_raw_skin_v1.rejected);continue;}
  if(read_descriptor(m->d.gr,&d,1)&&same(d,m->d)&&pitch==m->pitch&&pitch_valid(&d,pitch,pixels)){
   paint_marker(m,pixels,pitch);count(&xz_native_raw_skin_v1.repaints);
  }else{m->valid=0;count(&xz_native_raw_skin_v1.rejected);}
  original_unlock((void *)m->d.gr);
 }
 repainting=0;leave();
}

