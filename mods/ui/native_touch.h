#ifndef XZ_NATIVE_TOUCH_H
#define XZ_NATIVE_TOUCH_H
#include "ui.h"
struct xz_touch_status { uint8_t down,padding[3]; uint32_t x,y; };
struct xz_touch_region { int x,y,w,h; };
typedef void (*xz_stock_touch)(void *,const struct xz_touch_status *,const void *);
typedef void (*xz_touch_actions)(void *,const struct xz_ui_action *,size_t);
struct xz_native_touch {
 struct xz_ui *ui;
 const struct xz_ui_model *model;
 xz_touch_actions apply;
 void *context;
 int visible,capture,opening_contact;
 int badge_x,badge_y,badge_w,badge_h;
 int region_x,region_y,region_w,region_h,regional;
};
void xz_native_touch_init(struct xz_native_touch *,struct xz_ui *,const struct xz_ui_model *,xz_touch_actions,void *);
/* Called on the touch thread; releases active panel controls when closed. */
void xz_native_touch_visible(struct xz_native_touch *,int);
/* Configure while closed. NULL restores full-screen capture. Coordinates stay
   in native screen space, matching the inline renderer's final bounds. */
int xz_native_touch_region(struct xz_native_touch *,const struct xz_touch_region *);
/* Runtime supplies hash-gated original/trampoline. This function installs no hooks. */
int xz_native_touch_dispatch(struct xz_native_touch *,void *,const struct xz_touch_status *,const void *,xz_stock_touch);
#endif
