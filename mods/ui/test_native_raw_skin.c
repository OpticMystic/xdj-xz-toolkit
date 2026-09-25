#ifdef NDEBUG
#error Raw skin fixtures require assertions
#endif
#include "native_raw_skin.c"
#include <assert.h>
#include <stdio.h>
#define N 40
static struct {uint32_t d[16];uint16_t pixels[12*48];int pitch,alive;} windows[N];
static int owner=1,theme=0,locks,unlocks,destroys;
static void *gr_at(unsigned i){return (void *)(uintptr_t)(0x1000+i*0x100);}
static unsigned index_at(void *gr){return (unsigned)(((uintptr_t)gr-0x1000)/0x100);}
int xz_native_skin_on_owner_thread(void){return owner;}
int xz_mods_native_style_v1(void){return theme;}
void xz_native_window_keys_forget(void *gr){(void)gr;}
static int fixture_key_state;static uint16_t fixture_key;
int xz_native_window_key_for_hw(uintptr_t hw,uint16_t *key){(void)hw;if(key)*key=fixture_key;return fixture_key_state;}
void xz_mods_native_pixels_v1(uint16_t *p,uint32_t n){for(uint32_t i=0;i<n;i++)if(theme)p[i]=(uint16_t)(p[i]^(theme==1?0x035a:0x7100));}
int xz_read_memory(uint32_t address,void *out,size_t length){
 if(address<0x1000||((address-0x1000)%0x100))return -1;
 unsigned i=(address-0x1000)/0x100;if(i>=N||!windows[i].alive||length!=64)return -1;
 memcpy(out,windows[i].d,64);return 0;
}
static int fake_destroy(void *gr){windows[index_at(gr)].alive=0;destroys++;return 0;}
int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *hook,void **old){
 const unsigned char guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
 assert(address==0x1576a0&&!memcmp(expected,guard,8)&&hook==(void *)destroy_hook);*old=(void *)fake_destroy;return 0;
}
static int fake_lock(void *gr,void **pixels,int *pitch){
 unsigned i=index_at(gr);assert(i<N);locks++;
 if(!windows[i].alive)return -1;
 windows[i].d[6]|=0x01000000;*pixels=windows[i].pixels;*pitch=windows[i].pitch;return 0;
}
static int fake_unlock(void *gr){
 unsigned i=index_at(gr);unlocks++;xz_native_raw_skin_before_unlock((void *)(uintptr_t)windows[i].d[7]);windows[i].d[6]&=~0x01000000u;return 0;
}
static void init(unsigned i,unsigned width,unsigned height){
 memset(&windows[i],0,sizeof(windows[i]));windows[i].d[0]=width|(height<<16);windows[i].d[2]=9;
 windows[i].d[6]=0x40000000;windows[i].d[7]=0x100000+i*0x100;windows[i].pitch=24;windows[i].alive=1;
 for(unsigned j=0;j<12*48;j++)windows[i].pixels[j]=0xaaaa;
}
static uint16_t *begin(unsigned i,uintptr_t caller){
 void *p;int pitch;int result=fake_lock(gr_at(i),&p,&pitch);
 xz_native_raw_skin_locked(gr_at(i),&p,&pitch,caller,result);return p;
}
static void end(unsigned i){fake_unlock(gr_at(i));}
static void populate(uint16_t *p,unsigned h){for(unsigned y=0;y<h;y++)for(unsigned x=0;x<8;x++)p[y*12+x]=x==0?KEY565:x==1?0xffff:x==2?0xf800:0;}
static void check(unsigned i,unsigned h,int expected_theme){
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<12;x++){
  uint16_t value=x>=8?0xaaaa:x==0?KEY565:x==1?0xffff:x==2?0xf800:0;
  if(x<8&&x&&expected_theme)value^=expected_theme==1?0x035a:0x7100;
  assert(windows[i].pixels[y*12+x]==value);
 }
}
int main(void){
 assert(!xz_native_raw_skin_start(fake_lock,fake_unlock));
 init(0,8,48);populate(begin(0,0x22f384),48);end(0);check(0,48,0);
 theme=1;xz_native_raw_skin_repaint();check(0,48,1);
 theme=2;xz_native_raw_skin_repaint();check(0,48,2);
 theme=1;xz_native_raw_skin_repaint();check(0,48,1);
 uint16_t *p=begin(0,0x22fb18);assert(p[1]==0xffff&&p[2]==0xf800);p[3]=0xffff;end(0);
 assert(windows[0].pixels[3]==(uint16_t)(0xffff^0x035a));
 theme=0;xz_native_raw_skin_repaint();assert(windows[0].pixels[3]==0xffff&&windows[0].pixels[0]==KEY565);
 /* An uninitialized partial owner is never seeded from potentially themed pixels. */
 init(1,8,18);theme=1;unsigned rejected=xz_native_raw_skin_v1.rejected;p=begin(1,0x23ebe0);end(1);assert(xz_native_raw_skin_v1.rejected==rejected+1);
 theme=0;p=begin(1,0x23ebe0);end(1);assert(xz_native_raw_skin_v1.rejected==rejected+1);
 populate(begin(1,0x23f64c),18);end(1);check(1,18,0);
 /* Unknown/JPEG caller and nonowner do not capture or redirect. */
 init(2,8,18);unsigned captures=xz_native_raw_skin_v1.captures;p=begin(2,0x235bb4);assert(p==windows[2].pixels);end(2);
 owner=0;p=begin(2,0x23f64c);assert(p==windows[2].pixels);end(2);owner=1;assert(xz_native_raw_skin_v1.captures==captures);
 /* Caution write-only scratch merges only touched pixels and never padding. */
 init(3,8,18);theme=1;p=begin(3,0x147bc0);assert(p!=windows[3].pixels&&p[0]==SENTINEL);
 p[1]=0xffff;p[2]=0xf800;p[3]=KEY565;p[4]=0x7777;p[10]=0xffff;end(3);
 assert(windows[3].pixels[0]==0xaaaa&&windows[3].pixels[1]==(uint16_t)(0xffff^0x035a));
 assert(windows[3].pixels[2]==(uint16_t)(0xf800^0x035a)&&windows[3].pixels[3]==KEY565);
 assert(windows[3].pixels[4]==0x7777&&windows[3].pixels[10]==0xaaaa&&xz_native_raw_skin_v1.unknown_colors==1);
 fixture_key_state=1;fixture_key=0xffff;init(6,8,18);p=begin(6,0x147bc0);p[1]=0xffff;p[2]=0xf800;end(6);
 assert(windows[6].pixels[1]==0xffff&&windows[6].pixels[2]==(uint16_t)(0xf800^0x035a));fixture_key_state=0;
 /* Invalid sender metadata and failed lock are no-ops. */
 init(4,8,18);windows[4].d[2]=4;p=begin(4,0x23f64c);assert(p==windows[4].pixels);end(4);
 windows[4].d[2]=9;windows[4].pitch=15;p=begin(4,0x23f64c);assert(p==windows[4].pixels);end(4);
 windows[4].pitch=24;void *q=windows[4].pixels;int pitch=24;
 xz_native_raw_skin_locked(gr_at(4),&q,&pitch,0x23f64c,-1);assert(!pending.kind);
 /* Destruction clears stale state before same-address reuse. */
 assert(!destroy_hook(gr_at(0)));assert(destroys==1);init(0,8,48);
 p=begin(0,0x22fb18);end(0);assert(p==windows[0].pixels&&!pending.kind);
 /* HW identity drift between lock/unlock prevents publication. */
 init(5,8,18);p=begin(5,0x148444);p[0]=0xffff;uintptr_t oldhw=windows[5].d[7];windows[5].d[7]++;
 xz_native_raw_skin_before_unlock((void *)oldhw);assert(windows[5].pixels[0]==0xaaaa&&!pending.kind);end(5);
 /* Cache capacity is bounded and reports rejection rather than evicting live owners. */
 for(unsigned i=0;i<N;i++){init(i,8,18);populate(begin(i,0x23f64c),18);end(i);}
 assert(xz_native_raw_skin_v1.cache_full>0);
 unsigned active=0;for(unsigned i=0;i<CACHE_COUNT;i++)active+=cache[i].valid!=0;assert(active==CACHE_COUNT);
 assert(locks==unlocks);puts("PASS raw skins identity, A-B-A, pristine partial updates, sentinels, padding, destroy and bounded cache");return 0;
}
