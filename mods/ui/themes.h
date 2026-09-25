#ifndef XZ_UI_THEMES_H
#define XZ_UI_THEMES_H
#include <stddef.h>
#include <stdint.h>

#define XZ_THEME_COUNT 12
struct xz_theme_palette { uint32_t bg,ink,accent,alarm,stem[3]; };
struct xz_theme_surface { uint16_t *pixels; size_t stride; int width,height; int clip_x,clip_y,clip_w,clip_h; };
struct xz_theme_rect { int x,y,w,h; };
enum xz_theme_role { XZ_THEME_BUTTON, XZ_THEME_PANEL, XZ_THEME_HEADER };

static inline int xz_theme_id(int theme) { return theme>=0&&theme<XZ_THEME_COUNT?theme:0; }
static inline const struct xz_theme_palette *xz_theme_palette(int theme) {
 static const struct xz_theme_palette colors[XZ_THEME_COUNT]={
 {0x000000,0xf4f5f6,0xa8ceff,0xffa000,{0xff3b30,0x2997ff,0x30d158}},
 {0xf0f0f0,0x141414,0x176398,0xa35400,{0xc52727,0x125eae,0x16753d}},
 {0x00060e,0xdff3f7,0x54c1e6,0xfee801,{0xff2e88,0x54c1e6,0x2bf58a}},
 {0x0b0d17,0xc9d1d9,0x00e5ff,0xff9100,{0xff2daa,0x00e5ff,0x7c4dff}},
 {0x1e1e2e,0xcdd6f4,0xcba6f7,0xfab387,{0xf38ba8,0x89b4fa,0xa6e3a1}},
 {0x141414,0xf0fef9,0x00e575,0xd451ff,{0xff4fc3,0x006afb,0x00e575}},
 {0xfbf0d9,0x262a44,0x393f61,0xfdb03f,{0xe8705d,0x393f61,0x869a5f}},
 {0xb5c98b,0x1d2b20,0x4d6041,0x1d2b20,{0x1d2b20,0x4d6041,0x4d6041}},
 {0xd7d5e4,0x29233f,0x65549c,0x9b2638,{0xb33250,0x465b98,0x276951}},
 {0xc0c0c0,0x000000,0x000080,0x800000,{0x800000,0x000080,0x006000}},
 {0xe8edb0,0x25304f,0x483b89,0xa33149,{0xa33149,0x23538e,0x246344}},
 {0xe6edf4,0x172b41,0x1269be,0xa92d27,{0xa83046,0x155da9,0x247243}}
 };
 return &colors[xz_theme_id(theme)];
}
static inline const char *xz_theme_name(int theme) {
 static const char *const names[XZ_THEME_COUNT]={"ORIGINAL","WHITE","CYBERPUNK","NEON","MOCHA","AURORA","SANDSTONE","GAME BOY","SUPER NINTENDO","WINDOWS 95","GAME BOY COLOR","AQUA / ITUNES"};
 return names[xz_theme_id(theme)];
}
static inline uint16_t xz_theme_rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static inline uint32_t xz_theme_mix(uint32_t a,uint32_t b,unsigned amount) {
 unsigned n=amount>256?256:amount;
 return (((((a>>16)&255)*(256-n)+((b>>16)&255)*n)>>8)<<16)|
        (((((a>>8)&255)*(256-n)+((b>>8)&255)*n)>>8)<<8)|
        (((a&255)*(256-n)+(b&255)*n)>>8);
}
/* All rectangle arithmetic is widened before clipping. Stride is in pixels. */
static inline void xz_theme_fill(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t color) {
 int64_t x=r.x,y=r.y,right=x+r.w,bottom=y+r.h,cx=s.clip_x,cy=s.clip_y,cr=cx+s.clip_w,cb=cy+s.clip_h;
 if(!s.pixels||s.width<=0||s.height<=0||s.stride<(size_t)s.width||r.w<=0||r.h<=0||s.clip_w<=0||s.clip_h<=0)return;
 if(x<0)x=0;if(y<0)y=0;if(x<cx)x=cx;if(y<cy)y=cy;
 if(right>s.width)right=s.width;if(bottom>s.height)bottom=s.height;if(right>cr)right=cr;if(bottom>cb)bottom=cb;
 uint16_t pixel=xz_theme_rgb565(color);
 for(int64_t yy=y;yy<bottom;yy++)for(int64_t xx=x;xx<right;xx++)s.pixels[(size_t)yy*s.stride+(size_t)xx]=pixel;
}
static inline void xz_theme_edge(struct xz_theme_surface s,struct xz_theme_rect r,int inset,uint32_t color) {
 if(inset<0||r.w<=inset*2||r.h<=inset*2)return;
 int x=r.x+inset,y=r.y+inset,w=r.w-inset*2,h=r.h-inset*2;
 xz_theme_fill(s,(struct xz_theme_rect){x,y,w,1},color);xz_theme_fill(s,(struct xz_theme_rect){x,y+h-1,w,1},color);
 xz_theme_fill(s,(struct xz_theme_rect){x,y,1,h},color);xz_theme_fill(s,(struct xz_theme_rect){x+w-1,y,1,h},color);
}
static inline void xz_theme_background(struct xz_theme_surface s,int theme,struct xz_theme_rect r) {
 const struct xz_theme_palette *p=xz_theme_palette(theme);xz_theme_fill(s,r,p->bg);
 if(theme==7)for(int y=0;y<r.h;y+=8)for(int x=0;x<r.w;x+=8){
  xz_theme_fill(s,(struct xz_theme_rect){r.x+x,r.y+y,1,1},0x8a9f68);
  if(x+4<r.w&&y+4<r.h)xz_theme_fill(s,(struct xz_theme_rect){r.x+x+4,r.y+y+4,1,1},0x8a9f68);
 }
 if(theme==11)for(int y=2;y<r.h;y+=4)xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+y,r.w,1},0xf4f7fa);
 if(theme==10)for(int y=0;y<r.h;y+=8)for(int x=0;x<r.w;x+=8)xz_theme_fill(s,(struct xz_theme_rect){r.x+x,r.y+y,2,2},0xd5dda2);
}
/* Fill the entire frame before drawing caller-owned text, icons and meters.
   Panel/header content should keep an 8px inset; compact buttons need 6px. */
static inline void xz_theme_frame(struct xz_theme_surface s,int theme,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=xz_theme_palette(theme);
 if(r.w<=0||r.h<=0)return;
 if(theme==7)fill=selected?0x8a9f68:p->bg;
 xz_theme_fill(s,r,fill);
 if(theme<7||theme>=XZ_THEME_COUNT){xz_theme_edge(s,r,0,selected?p->accent:xz_theme_mix(p->bg,p->ink,112));return;}
 if(theme==7||theme==10){
  uint32_t dark=p->ink,light=theme==7?p->bg:0xf7f6da;
  xz_theme_edge(s,r,0,dark);xz_theme_edge(s,r,1,dark);xz_theme_edge(s,r,2,light);xz_theme_edge(s,r,3,dark);
  if(r.w>16&&r.h>16){
   xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,3,3},p->bg);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y,3,3},p->bg);
   xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-3,3,3},p->bg);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y+r.h-3,3,3},p->bg);
   if(theme==7&&role!=XZ_THEME_BUTTON){
    xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+4,2,2},p->accent);
    xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-6,r.y+4,2,2},p->accent);
    xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+r.h-6,2,2},p->accent);
    xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-6,r.y+r.h-6,2,2},p->accent);
   }
  }
  if(selected)xz_theme_fill(s,(struct xz_theme_rect){r.x+5,r.y+6,2,r.h-12},theme==7?dark:p->accent);
 }else if(theme==8){
  xz_theme_edge(s,r,0,0x393149);xz_theme_edge(s,r,1,0xf7f5ff);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+3,r.y+3,3,r.h-6},selected?0x65549c:0x9389b4);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y+2,2,r.h-2},0x827795);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+2,r.y+r.h-3,r.w-2,2},0x827795);
  if(role==XZ_THEME_HEADER)xz_theme_fill(s,(struct xz_theme_rect){r.x+7,r.y+r.h-6,r.w-14,2},0x65549c);
 }else if(theme==9){
  uint32_t hi=selected?0x404040:0xffffff,lo=selected?0xffffff:0x404040;
  xz_theme_edge(s,r,0,lo);
  xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,r.w-1,1},hi);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,1,r.h-1},hi);
  xz_theme_edge(s,r,1,selected?0xc0c0c0:0x808080);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+1,r.w-3,1},selected?0x808080:0xdfdfdf);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+1,1,r.h-3},selected?0x808080:0xdfdfdf);
  if(role==XZ_THEME_HEADER)xz_theme_fill(s,(struct xz_theme_rect){r.x+3,r.y+3,r.w-6,r.h-6},0x000080);
 }else if(theme==11){
  for(int y=1;y<r.h-1;y++){
   unsigned mix=(unsigned)(y*112/(r.h>1?r.h-1:1));
   uint32_t base=selected?0x9fcdf7:fill;
   uint32_t c=y<r.h/2?xz_theme_mix(base,0xffffff,192-mix):xz_theme_mix(base,0x88a9ca,mix/2);
   xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+y,r.w-2,1},c);
  }
  xz_theme_edge(s,r,0,selected?0x2775ba:0x7e96ae);xz_theme_edge(s,r,1,0xe9f5ff);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+2,r.w-8,1},0xffffff);
 }
}
#endif
