#include "cue.h"
#include <string.h>
void xz_cue_init(struct xz_cue *c, struct xz_cue_api api) { memset(c,0,sizeof(*c)); c->api=api; }
void xz_cue_settings(struct xz_cue *c,int gate,int smart) {
 c->gate=!!gate; c->smart=!!smart;
 memset(c->deck,0,sizeof(c->deck));
}
static int valid(const struct xz_cue_event *e) { return e->deck>=0 && e->deck<2; }
int xz_cue_before(struct xz_cue *c,const struct xz_cue_event *e) {
 struct xz_cue_deck *d;
 if(!valid(e)) return 0;
 d=&c->deck[e->deck];
 if(!e->hotcue_mode) { memset(d,0,sizeof(*d)); return 0; }
 if(e->play && e->operation==0 && c->gate && d->held && !d->latched) { d->latched=1; return 1; }
 if(e->pad<0 || e->pad>=8 || e->operation!=0 || (!c->gate && !c->smart)) return 0;
 if(!d->held) { d->latched=0; d->latest_valid=0; }
 d->held|=1u<<e->pad;
 d->pads[e->pad].had=c->api.cue_ms && c->api.cue_ms(c->api.context,e->deck,e->pad+1,&d->pads[e->pad].at);
 return 0;
}
void xz_cue_after(struct xz_cue *c,const struct xz_cue_event *e,int stock_result) {
 struct xz_cue_deck *d; struct xz_cue_pad *p; int32_t now;
 if(!valid(e)||e->pad<0||e->pad>=8) return;
 d=&c->deck[e->deck]; p=&d->pads[e->pad];
 if(e->operation==0) {
  if(stock_result==1 && p->had && e->hotcue_mode && c->api.cue_ms && c->api.cue_ms(c->api.context,e->deck,e->pad+1,&now) && now==p->at) {
   d->latest=e->pad; d->latest_at=p->at; d->latest_valid=1;
   d->last_error=XZ_CUE_OK;
   if(c->smart && (!c->api.follow_memory || !c->api.follow_memory(c->api.context,e->deck,p->at))) d->last_error=XZ_CUE_MEMORY_FAILED;
  }
  return;
 }
 if(e->operation!=2&&e->operation!=3) return;
 if(!(d->held&(1u<<e->pad))) return;
 d->held&=~(1u<<e->pad);
 if(!e->hotcue_mode || !p->had || !d->latest_valid || !c->api.cue_ms || !c->api.cue_ms(c->api.context,e->deck,e->pad+1,&now) || now!=p->at) { p->had=0; return; }
 if(!c->api.cue_ms(c->api.context,e->deck,d->latest+1,&now) || now!=d->latest_at) { d->latest_valid=0; p->had=0; return; }
 if(c->gate && !d->held && !d->latched) {
  if(!c->api.pause || !c->api.pause(c->api.context,e->deck)) d->last_error=XZ_CUE_PAUSE_FAILED;
  else if(!c->api.seek_ms || !c->api.seek_ms(c->api.context,e->deck,d->latest_at)) d->last_error=XZ_CUE_SEEK_FAILED;
 }
 p->had=0;
}
