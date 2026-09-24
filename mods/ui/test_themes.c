#ifdef NDEBUG
#error Theme acceptance requires active assertions
#endif
#include "themes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
int main(int argc,char **argv){
 uint16_t guarded[2+48*36];
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  for(size_t i=0;i<sizeof(guarded)/sizeof(guarded[0]);i++)guarded[i]=0x1234;
  struct xz_theme_surface s={guarded+1,48,40,36,4,5,30,20};
  xz_theme_background(s,theme,(struct xz_theme_rect){-4,-8,54,50});
  for(int role=0;role<3;role++)xz_theme_frame(s,theme,(struct xz_theme_rect){-4,-8,54,50},xz_theme_palette(theme)->bg,role&1,(enum xz_theme_role)role);
  xz_theme_fill(s,(struct xz_theme_rect){INT_MIN,INT_MIN,INT_MAX,INT_MAX},0);
  assert(guarded[0]==0x1234&&guarded[1+48*36]==0x1234);
  for(int y=0;y<36;y++)for(int x=0;x<48;x++)if(x<4||x>=34||y<5||y>=25)assert(guarded[1+y*48+x]==0x1234);
  assert(strlen(xz_theme_name(theme))>0);
 }
 assert(xz_theme_palette(-1)==xz_theme_palette(0));assert(xz_theme_palette(12)==xz_theme_palette(0));
 assert(xz_theme_palette(6)->bg==0xfbf0d9);assert(xz_theme_palette(0)->stem[0]==0xff3b30);
 if(argc>1){
  static uint16_t pixels[800*600];struct xz_theme_surface s={pixels,800,800,600,0,0,800,600};
  for(int theme=7;theme<12;theme++){
   int y=(theme-7)*120;const struct xz_theme_palette *p=xz_theme_palette(theme);
   xz_theme_background(s,theme,(struct xz_theme_rect){0,y,800,120});
   xz_theme_frame(s,theme,(struct xz_theme_rect){12,y+8,776,28},p->bg,0,XZ_THEME_HEADER);
   for(int i=0;i<4;i++)xz_theme_frame(s,theme,(struct xz_theme_rect){12+i*196,y+48,188,56},p->bg,i==1,XZ_THEME_BUTTON);
  }
  FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 600\n255\n");
  for(size_t i=0;i<800*600;i++){unsigned v=pixels[i];unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}fclose(f);
 }
 puts("PASS theme IDs, clipped frames and stride guards");return 0;
}

