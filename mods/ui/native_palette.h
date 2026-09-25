#ifndef XZ_NATIVE_PALETTE_H
#define XZ_NATIVE_PALETTE_H
#include "themes.h"

/* Map original native draw colors. Never feed a previously themed retained
   surface back through this mapping; callers own pristine input separately. */
static inline uint16_t xz_native_palette_pixel(int theme,uint16_t source){
 if(theme<=0||theme>=XZ_THEME_COUNT)return source;
 unsigned r=((source>>11)&31)*255/31,g=((source>>5)&63)*255/63,b=(source&31)*255/31;
 unsigned light=(77*r+150*g+29*b)>>8;
 if(theme==9){
  unsigned maximum=r>g?r:g;if(b>maximum)maximum=b;
  unsigned minimum=r<g?r:g;if(b<minimum)minimum=b;
  if(maximum>=160&&maximum-minimum>=64){
   unsigned red=r*4>=maximum*3,green=g*4>=maximum*3,blue=b*4>=maximum*3;
   return xz_theme_rgb565((red?0x800000u:0)|(green?0x008000u:0)|(blue?0x000080u:0));
  }
  return xz_theme_rgb565(light>=192?0x000000:light>=112?0x404040:light>=56?0x808080:0xc0c0c0);
 }
 if(theme==7){
  const uint32_t tones[4]={0xb5c98b,0x8a9f68,0x4d6041,0x1d2b20};
  return xz_theme_rgb565(tones[light>>6]);
 }
 const struct xz_theme_palette *p=xz_theme_palette(theme);
 uint32_t ink=p->ink;
 unsigned maximum=r>g?r:g;if(b>maximum)maximum=b;
 unsigned minimum=r<g?r:g;if(b<minimum)minimum=b;
 if(maximum-minimum>40){
  uint32_t accent=r>g&&r>b?p->stem[0]:g>r&&g>b?p->stem[2]:p->stem[1];
  ink=xz_theme_mix(ink,accent,144);
 }
 return xz_theme_rgb565(xz_theme_mix(p->bg,ink,light*256/255));
}

/* Native SetWindowColor role 1 uses reduced RGB bytes, not RGB888 or RGB565.
   Roles 2 (transparency key) and 3 (unverified semantics) are left unchanged. */
static inline uint32_t xz_native_palette_reduced(int theme,unsigned role,uint32_t source){
 if(role!=1||(source&255)>31||((source>>8)&255)>63||((source>>16)&255)>31)return source;
 uint16_t pixel=(uint16_t)((source&31)<<11|((source>>8)&63)<<5|((source>>16)&31));
 uint16_t mapped=xz_native_palette_pixel(theme,pixel);
 return (source&0xff000000u)|((mapped>>11)&31)|(((mapped>>5)&63)<<8)|((mapped&31)<<16);
}

/* The DirectFB text path instead takes full RGBA bytes. Alpha is never mapped. */
static inline uint32_t xz_native_palette_rgba(int theme,uint32_t source){
 if(theme<=0||theme>=XZ_THEME_COUNT)return source;
 unsigned r=source&255,g=(source>>8)&255,b=(source>>16)&255;
 uint16_t mapped=xz_native_palette_pixel(theme,(uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3)));
 return (source&0xff000000u)|(((mapped>>11)&31)*255/31)|((((mapped>>5)&63)*255/63)<<8)|(((mapped&31)*255/31)<<16);
}
#endif
