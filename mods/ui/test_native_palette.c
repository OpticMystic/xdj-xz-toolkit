#include "native_palette.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Native palette checks require assertions
#endif
int main(void){
 for(unsigned p=0;p<65536;p++){
  assert(xz_native_palette_pixel(0,(uint16_t)p)==p);
  uint16_t mapped=xz_native_palette_pixel(7,(uint16_t)p);
  assert(mapped==xz_theme_rgb565(0xb5c98b)||mapped==xz_theme_rgb565(0x8a9f68)||mapped==xz_theme_rgb565(0x4d6041)||mapped==xz_theme_rgb565(0x1d2b20));
 }
 assert(xz_native_palette_pixel(7,0)==xz_theme_rgb565(0xb5c98b));
 assert(xz_native_palette_pixel(7,0xffff)==xz_theme_rgb565(0x1d2b20));
 assert(xz_native_palette_pixel(9,0)==xz_theme_rgb565(0xc0c0c0));
 assert(xz_native_palette_pixel(9,0xffff)==0);
 assert(xz_native_palette_pixel(9,0x001f)==xz_theme_rgb565(0x000080));
 assert(xz_native_palette_pixel(9,0x07e0)==xz_theme_rgb565(0x008000));
 assert(xz_native_palette_pixel(9,0xf800)==xz_theme_rgb565(0x800000));
 for(int t=0;t<XZ_THEME_COUNT;t++)for(unsigned alpha=0;alpha<256;alpha++){
  uint32_t color=(alpha<<24)|0x001f001f;
  assert(xz_native_palette_reduced(t,2,color)==color);
  assert(xz_native_palette_reduced(t,3,color)==color);
  assert((xz_native_palette_reduced(t,1,color)>>24)==alpha);
  assert((xz_native_palette_rgba(t,(alpha<<24)|0x003f8acf)>>24)==alpha);
 }
 assert(xz_native_palette_reduced(7,1,0x000000ff)==0x000000ff);
 puts("PASS native color formats, protected key roles, four sage tones and alpha preservation");
}
