#include "native_touch.h"
#include <assert.h>
#ifdef NDEBUG
#error Touch acceptance requires active assertions
#endif
#include <stdio.h>
#include <string.h>
struct fake_solver { uint32_t owner;struct xz_touch_status previous;int calls,releases; };
static void stock(void*p,const struct xz_touch_status*s,const void*m) {
 struct fake_solver*f=p;(void)m;f->calls++;if(f->previous.down&&!s->down)f->releases++;f->previous=*s;
}
int main(void) {
 struct xz_ui ui;struct xz_ui_model model={0};struct xz_native_touch touch;
 struct fake_solver solver={0};struct xz_touch_status s={1,{0,0,0},100,100};
 xz_ui_init(&ui);xz_native_touch_init(&touch,&ui,&model,NULL,NULL);
 xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(solver.calls==1&&solver.previous.down);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(solver.calls==2&&solver.releases==1);
 s.down=1;s.x=760;s.y=10;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 assert(touch.visible&&touch.capture&&touch.opening_contact&&solver.calls==2&&!ui.down);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(!touch.capture&&!touch.opening_contact&&solver.calls==2);
 s.down=1;s.x=400;s.y=200;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(ui.down&&touch.capture&&solver.calls==2);
 xz_native_touch_visible(&touch,0);assert(!ui.down&&!touch.visible&&touch.capture);
 xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(solver.calls==2);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(!touch.capture&&solver.calls==2);
 s.down=1;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(solver.calls==3&&solver.previous.down);
 xz_native_touch_visible(&touch,1);xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 assert(solver.calls==4&&solver.releases==2&&!solver.previous.down&&touch.capture);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(solver.calls==4);
 xz_native_touch_visible(&touch,0);
 s.down=1;s.x=100;s.y=100;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 s.x=760;s.y=10;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 assert(!touch.visible&&!touch.capture&&solver.previous.down);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 /* Inline region uses fixture coordinates, not a claimed native layout. */
 struct xz_touch_region r={120,220,500,60};
 assert(xz_native_touch_region(&touch,&r));
 xz_native_touch_visible(&touch,1);
 assert(!xz_native_touch_region(&touch,NULL));
 s=(struct xz_touch_status){1,{0,0,0},30,30};
 assert(!xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 int count=solver.calls;
 s.x=200;s.y=240;
 assert(!xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 assert(solver.calls==count+1&&solver.previous.down&&!touch.capture);
 s.down=0;
 assert(!xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 s.down=1;
 assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 count=solver.calls;
 s.x=10;s.y=10;
 assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 assert(solver.calls==count&&touch.capture);
 xz_native_touch_visible(&touch,0);
 s.down=0;
 assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));
 assert(solver.calls==count&&!touch.capture&&!ui.down);
 assert(xz_native_touch_region(&touch,NULL));
 r.x=790;assert(!xz_native_touch_region(&touch,&r));
 r=(struct xz_touch_region){0,0,0,10};assert(!xz_native_touch_region(&touch,&r));
 puts("PASS regional ownership, native drag crossing, captured drag exit/close and bounds");
 puts("PASS stock touch passthrough, opening-contact ownership, synthesized release and no close click-through");
 return 0;
}
