#include "cue.h"
#include <assert.h>
#ifdef NDEBUG
#error Cue acceptance requires active assertions; compile with -UNDEBUG
#endif
#include <stdio.h>
#include <string.h>
struct fake {
 int32_t pos[2][9],memory[2],last_seek;
 char order[64]; int len,follows,delete_on_stock,stock_failure,memory_failure,pause_failure,seek_failure;
};
static int get(void *p,int d,int k,int32_t *at) { struct fake*f=p;*at=f->pos[d][k];return *at>=0; }
static int pause_(void*p,int d) { struct fake*f=p;(void)d;f->order[f->len++]='P';return !f->pause_failure; }
static int seek_(void*p,int d,int32_t at) { struct fake*f=p;(void)d;f->last_seek=at;f->order[f->len++]='J';return !f->seek_failure; }
static int follow_(void*p,int d,int32_t at) {
 struct fake*f=p;f->follows++;f->order[f->len++]='M';
 if(f->memory_failure)return 0;f->memory[d]=at;return 1;
}
static void send(struct xz_cue*c,struct fake*f,struct xz_cue_event e) {
 if(xz_cue_before(c,&e))f->order[f->len++]='L';
 else {
  f->order[f->len++]='S';
  if(f->delete_on_stock && e.operation==0 && e.pad>=0)f->pos[e.deck][e.pad+1]=-1;
  xz_cue_after(c,&e,!f->stock_failure);
 }
}
static void setup(struct xz_cue*c,struct fake*f,int gate,int smart) {
 struct xz_cue_api a={f,get,pause_,seek_,follow_};
 memset(f,0,sizeof(*f));f->pos[0][1]=100;f->pos[0][2]=200;f->pos[1][1]=100;
 xz_cue_init(c,a);xz_cue_settings(c,gate,smart);
}
int main(void) {
 struct fake f;struct xz_cue c;
 struct xz_cue_event a={0,0,0,1,0,0,0,-1,0},au={0,0,2,1,0,0,0,-1,0},b={0,1,0,1,0,0,0,-1,0},bu={0,1,2,1,0,0,0,-1,0},play={0,-1,0,1,1,0,0,-1,0};
 setup(&c,&f,0,0);send(&c,&f,a);send(&c,&f,au);assert(f.len==2&&f.follows==0);
 setup(&c,&f,1,1);send(&c,&f,a);
 assert(f.len==2&&f.order[0]=='S'&&f.order[1]=='M'&&f.memory[0]==100&&c.deck[0].held==1);
 send(&c,&f,au);assert(f.len==5&&!memcmp(f.order,"SMSPJ",5)&&f.follows==1);
 setup(&c,&f,1,1);send(&c,&f,a);send(&c,&f,b);
 assert(f.memory[0]==200&&f.follows==2&&c.deck[0].held==3);
 send(&c,&f,bu);send(&c,&f,au);assert(f.memory[0]==200&&f.follows==2&&f.last_seek==200);
 setup(&c,&f,1,0);send(&c,&f,a);send(&c,&f,play);send(&c,&f,au);assert(f.len==3&&f.order[1]=='L');
 setup(&c,&f,1,1);f.delete_on_stock=1;send(&c,&f,a);send(&c,&f,au);assert(f.len==2&&f.follows==0);
 setup(&c,&f,1,1);f.pos[0][1]=-1;send(&c,&f,a);f.pos[0][1]=100;send(&c,&f,au);assert(f.len==2&&f.follows==0);
 setup(&c,&f,1,0);send(&c,&f,a);xz_cue_settings(&c,0,0);send(&c,&f,au);assert(f.len==2);
 setup(&c,&f,1,0);send(&c,&f,a);play.deck=1;send(&c,&f,play);send(&c,&f,au);assert(f.len==5&&f.order[1]=='S');
 setup(&c,&f,1,1);f.stock_failure=1;send(&c,&f,a);send(&c,&f,au);assert(f.follows==0&&f.len==2);
 setup(&c,&f,1,1);f.memory_failure=1;send(&c,&f,a);assert(c.deck[0].last_error==XZ_CUE_MEMORY_FAILED&&f.memory[0]==0);
 send(&c,&f,au);assert(f.follows==1&&f.last_seek==100&&c.deck[0].last_error==XZ_CUE_MEMORY_FAILED);
 setup(&c,&f,1,0);f.pause_failure=1;send(&c,&f,a);send(&c,&f,au);assert(c.deck[0].last_error==XZ_CUE_PAUSE_FAILED&&f.last_seek==0);
 setup(&c,&f,1,0);f.seek_failure=1;send(&c,&f,a);send(&c,&f,au);assert(c.deck[0].last_error==XZ_CUE_SEEK_FAILED);
 puts("PASS immediate smart follow while held, overlap recall order, stock release, latch, delete/record, deck isolation and native failures");
 return 0;
}
