#include "native.h"
#include <string.h>
#if !defined(__arm__) || defined(__aarch64__)
#error Native adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ pointer ABI");
_Static_assert(sizeof(long)==4,"XZ long ABI");
static int32_t word(const void *p,unsigned off) { int32_t v; memcpy(&v,(const char*)p+off,4); return v; }
static void put(void *p,unsigned off,int32_t v) { memcpy((char*)p+off,&v,4); }
static int cue_ms(void *ctx,int deck,int kind,int32_t *at) {
 struct xz_cue_native *n=ctx; _Alignas(8) uint32_t out[0x338/4];
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]) return 0;
 ((void (*)(void*,void*,int,int))(uintptr_t)0x42fbc)(out,n->engine_if[deck],deck,kind);
 *at=word(out,0x318); return *at>=0;
}
static int pause_deck(void *ctx,int deck) {
 struct xz_cue_native *n=ctx;
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]) return 0;
 return ((int (*)(void*,int,int,int,int))(uintptr_t)0x3ff20)(n->engine_if[deck],deck,1,1,0);
}
static int seek(void *ctx,int deck,int32_t at) {
 struct xz_cue_native *n=ctx;
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]||at<0) return 0;
 return ((int (*)(void*,int,int32_t))(uintptr_t)0x40210)(n->engine_if[deck],deck,at);
}
static int follow(void *ctx,int deck,int32_t at) {
 struct xz_cue_native *n=ctx; _Alignas(8) uint32_t mem[0x338/4];
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]||at<0) return 0;
 ((void (*)(void*,void*,int,int))(uintptr_t)0x42fbc)(mem,n->engine_if[deck],deck,0);
 put(mem,0x318,at);
 /* Main cue follows a point; retaining a previous loop end can create a reversed loop. */
 put(mem,0x324,-1); ((unsigned char*)mem)[0x334]=0;
 return ((int (*)(void*,int,int,const void*))(uintptr_t)0x43094)(n->engine_if[deck],deck,0,mem);
}
struct xz_cue_api xz_cue_native_api(struct xz_cue_native *n) {
 struct xz_cue_api a={n,cue_ms,pause_deck,seek,follow}; return a;
}
int xz_cue_native_decode(struct xz_cue_native *n,void *innards,const void *input,struct xz_cue_event *e) {
 uint16_t key; int mode; const unsigned char *p=input; const unsigned char *self=innards;
 if(!e) return 0;
 memset(e,0,sizeof(*e)); e->deck=-1; e->pad=-1; e->pad_page=-1; e->mode_button=-1;
 if(!n||!n->verified||!innards||!input) return 0;
 /* UiObjectManager constructs internal Player channels1 and2. */
 if(self[0x26]!=1 && self[0x26]!=2) return 0;
 e->deck=(int)self[0x26]-1;
 memcpy(&n->engine_if[e->deck],self+0x30,4);
 memcpy(&key,p+8,2); e->operation=p[11]&15;
 mode=word(self,0x80);
 e->hotcue_mode=mode==0;
 e->pad_page=mode==0?0:(mode>=2&&mode<=4?mode-1:-1);
 e->shift=self[0x34]!=0;
 e->mode_button=key>=0x4114&&key<=0x4117?(int)key-0x4114:-1;
 e->sync=key==0x4313;
 e->pad=key>=0x4119&&key<=0x4120?(int)key-0x4119:-1;
 e->play=key==0x4101;
 return 1;
}
