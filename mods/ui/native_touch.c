#include "native_touch.h"
#include "../settings.h"
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(struct xz_touch_status)==12,"XZ TouchStatus ABI");
_Static_assert(offsetof(struct xz_touch_status,x)==4,"XZ touch x offset");
_Static_assert(offsetof(struct xz_touch_status,y)==8,"XZ touch y offset");
void xz_native_touch_init(struct xz_native_touch*t,struct xz_ui*ui,const struct xz_ui_model*model,xz_touch_actions apply,void*ctx) {
 memset(t,0,sizeof(*t));t->ui=ui;t->model=model;t->apply=apply;t->context=ctx;
 t->badge_x=744;t->badge_y=0;t->badge_w=56;t->badge_h=24;
 t->vj_btn_x=0;t->vj_btn_y=0;t->vj_btn_w=80;t->vj_btn_h=24;
}
void xz_native_touch_visible(struct xz_native_touch*t,int visible) {
 if(t->visible&&!visible) {
  struct xz_ui_action out[XZ_UI_ACTIONS];size_t n=xz_ui_cancel(t->ui,out);
  t->visible=0;
  if(n&&t->apply)t->apply(t->context,out,n);
 } else t->visible=!!visible;
}
static int badge(const struct xz_native_touch*t,const struct xz_touch_status*s) {
 return s->x>=(uint32_t)t->badge_x&&s->y>=(uint32_t)t->badge_y&&
        s->x<(uint32_t)(t->badge_x+t->badge_w)&&s->y<(uint32_t)(t->badge_y+t->badge_h);
}
static int vj_btn(const struct xz_native_touch*t,const struct xz_touch_status*s) {
 return s->x>=(uint32_t)t->vj_btn_x&&s->y>=(uint32_t)t->vj_btn_y&&
        s->x<(uint32_t)(t->vj_btn_x+t->vj_btn_w)&&s->y<(uint32_t)(t->vj_btn_y+t->vj_btn_h);
}
int xz_native_touch_region(struct xz_native_touch*t,const struct xz_touch_region*r) {
 if(t->visible||t->capture)return 0;
 if(!r){t->regional=0;return 1;}
 if(r->x<0||r->y<0||r->w<=0||r->h<=0||r->w>XZ_UI_WIDTH||r->h>XZ_UI_HEIGHT||
    r->x>XZ_UI_WIDTH-r->w||r->y>XZ_UI_HEIGHT-r->h)return 0;
 t->region_x=r->x;t->region_y=r->y;t->region_w=r->w;t->region_h=r->h;t->regional=1;
 return 1;
}
static int region(const struct xz_native_touch*t,const struct xz_touch_status*s) {
 return s->x>=(uint32_t)t->region_x&&s->y>=(uint32_t)t->region_y&&
        s->x<(uint32_t)(t->region_x+t->region_w)&&s->y<(uint32_t)(t->region_y+t->region_h);
}
int xz_native_touch_dispatch(struct xz_native_touch*t,void*self,const struct xz_touch_status*s,const void*mode,xz_stock_touch stock) {
 struct xz_touch_status previous;struct xz_ui_action out[XZ_UI_ACTIONS];size_t n;
 memcpy(&previous,(const char*)self+4,sizeof(previous));
 if(!t->visible&&!t->capture&&s->down&&!previous.down&&badge(t,s)) {
  t->visible=1;t->capture=1;t->opening_contact=1;
 }
 if(!t->visible&&!t->capture&&s->down&&!previous.down&&t->model&&t->model->takeover_assign==XZ_TAKEOVER_ONSCREEN&&vj_btn(t,s)) {
  t->capture=1;t->opening_contact=1;
  struct xz_ui_action act={.kind=XZ_UI_TAKEOVER_TOGGLE};
  if(t->apply)t->apply(t->context,&act,1);
 }
 if(t->visible&&!t->capture&&(!t->regional||(s->down&&!previous.down&&region(t,s))))t->capture=1;
 if(!t->capture) {stock(self,s,mode);return 0;}
 /* End any stock gesture before the panel takes ownership. Native solver handles
    its own area teardown; the shim never modifies its private pointer fields. */
 if(previous.down) {previous.down=0;stock(self,&previous,mode);}
 if(t->visible&&!t->opening_contact) {
  int x=s->x>=XZ_UI_WIDTH?XZ_UI_WIDTH-1:(int)s->x;
  int y=s->y>=XZ_UI_HEIGHT?XZ_UI_HEIGHT-1:(int)s->y;
  n=xz_ui_touch(t->ui,t->model,x,y,!!s->down,out);
  if(n&&t->apply)t->apply(t->context,out,n);
 }
 if(!s->down) {t->capture=0;t->opening_contact=0;}
 return 1;
}
