#ifdef NDEBUG
#error Font acceptance requires active assertions
#endif
#include "ui.h"
#include "font_atlas.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[804*480+16];
int main(void){
 for(int face=0;face<2;face++)for(int i=0;i<224;i++){
  const struct xz_font_glyph *g=&xz_font_glyphs[face][i];
  assert(g->offset+(size_t)g->width*g->height<=sizeof(xz_font_coverage));
  assert(g->advance>0);
 }
 struct xz_ui ui;struct xz_ui_model m={0};xz_ui_init(&ui);ui.page=XZ_UI_STEMS;
 m.deck[0].track="Caf\xc3\xa9 / D\xc3\xa9j\xc3\xa0 vu - WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
 m.deck[0].status="Long status with accents, lowercase and clipped edges";
 for(unsigned i=0;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=0xabcd;
 assert(xz_ui_render(&ui,&m,pixels,804*480,804));
 for(int y=0;y<480;y++)for(int x=800;x<804;x++)assert(pixels[y*804+x]==0xabcd);
 for(unsigned i=804*480;i<sizeof(pixels)/sizeof(*pixels);i++)assert(pixels[i]==0xabcd);
 for(int i=0;i<800*480;i++)pixels[i]=0x1234;
 xz_ui_render_badge(pixels,800);
 for(int y=0;y<480;y++)for(int x=0;x<800;x++)if(x<744||y>=24)assert(pixels[y*800+x]==0x1234);
 puts("PASS atlas ranges, accented title clipping, padded strides and badge bounds");
}
