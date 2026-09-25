"""Compile actual native DirectFB style adapters against typed host fixtures."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'vendor/tools/xz_runtime/xz_directfb_hook.c').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

declarations = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pixel_font.h"
#ifdef NDEBUG
#error assertions required
#endif
typedef int DFBResult;
typedef int DFBSurfaceTextFlags;
typedef int DFBSurfacePixelFormat;
typedef struct {int x,y,w,h;} DFBRectangle;
#define DFB_OK 0
#define DSPF_UNKNOWN 0
#define DSPF_RGB16 9
typedef struct IDirectFBSurface IDirectFBSurface;
typedef struct IDirectFBFont IDirectFBFont;
typedef DFBResult (*SurfaceColorFn)(IDirectFBSurface *,uint8_t,uint8_t,uint8_t,uint8_t);
typedef DFBResult (*SurfaceStringFn)(IDirectFBSurface *,const char *,int,int,int,DFBSurfaceTextFlags);
struct IDirectFBFont {
 DFBResult (*GetAscender)(IDirectFBFont *,int *);
 DFBResult (*GetStringWidth)(IDirectFBFont *,const char *,int,int *);
 DFBResult (*Release)(IDirectFBFont *);
};
struct IDirectFBSurface {
 DFBResult (*GetPixelFormat)(IDirectFBSurface *,DFBSurfacePixelFormat *);
 SurfaceColorFn SetColor,Clear;
 SurfaceStringFn DrawString;
 DFBResult (*GetFont)(IDirectFBSurface *,IDirectFBFont **);
 DFBResult (*FillRectangles)(IDirectFBSurface *,const DFBRectangle *,unsigned int);
};
static SurfaceColorFn original_surface_color,original_surface_clear;
static SurfaceStringFn original_surface_string;
static uint32_t (*mods_native_color)(uint32_t);
static uint32_t (*mods_native_surface_color)(uint32_t,uint32_t);
static int (*mods_native_style)(void);
static unsigned xz_native_surface_style_proof_v1[8];
static int theme,format=DSPF_RGB16,format_error,font_error,null_font,ascender=14;
static int ascender_error,width_error,width_negative,width_huge,width_zero,fill_error;
static int refs,font_gets,releases,paints,stocks,color_calls,clear_calls,width_calls,prefix_bytes;
static int batch_calls;
static uint32_t captured_color;
static uint32_t expected_surface;
static const char *expected_text;
static int expected_bytes,expected_x,expected_y,expected_flags;
struct rect {int x,y,w,h;};
static struct rect rectangles[5376];
static int style(void){return theme;}
static uint32_t color_map(uint32_t c){return theme?(c^0x002d6a84):c;}
static uint32_t keyed_color_map(uint32_t surface,uint32_t c){assert(surface==expected_surface);return (c&0xffffff)?color_map(c):c;}
static int advance(unsigned ch){return ch==' '?4:5+(int)(ch%7);}
static int native_width(const char *s,int n){int w=0;for(int i=0;i<n;i++)w+=advance((unsigned char)s[i]);return w;}
static DFBResult get_ascender(IDirectFBFont *f,int *out){(void)f;*out=ascender;return ascender_error?-1:0;}
static DFBResult get_width(IDirectFBFont *f,const char *s,int n,int *out){
 (void)f;assert(refs==1&&paints==0);width_calls++;prefix_bytes+=n;
 if(width_error==n)return -1;
 *out=width_negative?-1:width_huge?4097:width_zero?0:native_width(s,n);return 0;
}
static DFBResult release_font(IDirectFBFont *f){(void)f;assert(refs==1);refs--;releases++;return 0;}
static IDirectFBFont font={get_ascender,get_width,release_font};
static DFBResult get_font(IDirectFBSurface *s,IDirectFBFont **out){
 (void)s;font_gets++;if(font_error)return -1;if(null_font){*out=NULL;return 0;}refs++;*out=&font;return 0;
}
static DFBResult get_format(IDirectFBSurface *s,DFBSurfacePixelFormat *out){(void)s;*out=format;return format_error?-1:0;}
static DFBResult set_color(IDirectFBSurface *s,uint8_t r,uint8_t g,uint8_t b,uint8_t a){
 (void)s;color_calls++;captured_color=r|((uint32_t)g<<8)|((uint32_t)b<<16)|((uint32_t)a<<24);return 17;
}
static DFBResult clear_surface(IDirectFBSurface *s,uint8_t r,uint8_t g,uint8_t b,uint8_t a){
 (void)s;clear_calls++;captured_color=r|((uint32_t)g<<8)|((uint32_t)b<<16)|((uint32_t)a<<24);return 19;
}
static DFBResult alternate_color(IDirectFBSurface *s,uint8_t r,uint8_t g,uint8_t b,uint8_t a){(void)s;(void)r;(void)g;(void)b;(void)a;return 23;}
static DFBResult stock_string(IDirectFBSurface *s,const char *t,int n,int x,int y,DFBSurfaceTextFlags flags){
 (void)s;assert(refs==0&&paints==0);assert(t==expected_text&&n==expected_bytes&&x==expected_x&&y==expected_y&&flags==expected_flags);stocks++;return 29;
}
static DFBResult alternate_string(IDirectFBSurface *s,const char *t,int n,int x,int y,DFBSurfaceTextFlags f){(void)s;(void)t;(void)n;(void)x;(void)y;(void)f;return 31;}
static DFBResult fill(IDirectFBSurface *s,const DFBRectangle *items,unsigned int count){
 (void)s;assert(refs==0);assert(count>0&&count<=128);batch_calls++;
 for(unsigned int i=0;i<count;i++){
  assert(items[i].w>0&&items[i].h>0&&paints<5376);
  rectangles[paints++]=(struct rect){items[i].x,items[i].y,items[i].w,items[i].h};
 }
 return fill_error==batch_calls?-37:0;
}
static IDirectFBSurface surface(void){return (IDirectFBSurface){get_format,set_color,clear_surface,stock_string,get_font,fill};}
static void reset(void){
 assert(refs==0);font_gets=releases=paints=stocks=width_calls=prefix_bytes=batch_calls=0;
 font_error=null_font=ascender_error=width_error=width_negative=width_huge=width_zero=fill_error=0;ascender=14;
}
static void expect(const char *s,int n,int x,int y,int flags){expected_text=s;expected_bytes=n;expected_x=x;expected_y=y;expected_flags=flags;}
'''
functions = '\n'.join(extract(s) for s in (
    'static uint32_t native_style_rgba(',
    'static DFBResult styled_surface_color(',
    'static DFBResult styled_surface_clear(',
    'static DFBResult styled_surface_string(',
    'static void hook_native_surface_style(',
))
checks = r'''
static void fallback(IDirectFBSurface *s,const char *text,int bytes,int x,int y,int flags){
 expect(text,bytes,x,y,flags);assert(styled_surface_string(s,text,bytes,x,y,flags)==29);
 assert(stocks==1&&paints==0&&refs==0&&font_gets==releases+(font_error||null_font?1:0));
}
int main(void){
 IDirectFBSurface s=surface();mods_native_style=style;
 hook_native_surface_style(&s);assert(s.SetColor==set_color);
 mods_native_color=color_map;format=2;hook_native_surface_style(&s);assert(s.SetColor==set_color);
 format=DSPF_RGB16;format_error=1;hook_native_surface_style(&s);assert(s.SetColor==set_color);
 format_error=0;hook_native_surface_style(&s);
 assert(s.SetColor==styled_surface_color&&s.Clear==styled_surface_clear&&s.DrawString==styled_surface_string);
 unsigned hooked=xz_native_surface_style_proof_v1[5];hook_native_surface_style(&s);assert(xz_native_surface_style_proof_v1[5]==hooked);
 IDirectFBSurface other=surface();other.SetColor=alternate_color;other.Clear=alternate_color;other.DrawString=alternate_string;
 hook_native_surface_style(&other);assert(other.SetColor==alternate_color&&other.Clear==alternate_color&&other.DrawString==alternate_string);
 IDirectFBSurface missing=surface();missing.SetColor=NULL;missing.Clear=NULL;missing.DrawString=NULL;
 hook_native_surface_style(&missing);assert(!missing.SetColor&&!missing.Clear&&!missing.DrawString);hook_native_surface_style(NULL);
 for(int t=0;t<2;t++)for(unsigned alpha=0;alpha<256;alpha++){
  theme=t?7:0;uint32_t raw=0x00345612|(alpha<<24);
  assert(s.SetColor(&s,0x12,0x56,0x34,(uint8_t)alpha)==17);assert(captured_color==(theme?(raw^0x002d6a84):raw));
  assert(s.Clear(&s,0x12,0x56,0x34,(uint8_t)alpha)==19);assert((captured_color>>24)==alpha);
 }
 mods_native_color=NULL;assert(native_style_rgba(&s,1,2,3,4)==0x04030201);mods_native_color=color_map;
 expected_surface=(uint32_t)(uintptr_t)&s;mods_native_surface_color=keyed_color_map;theme=7;
 assert(s.SetColor(&s,0,0,0,255)==17&&captured_color==0xff000000);
 assert(s.Clear(&s,0,0,0,128)==19&&captured_color==0x80000000);mods_native_surface_color=NULL;
 char ascii[96];for(int i=0;i<95;i++)ascii[i]=(char)(32+i);ascii[95]=0;
 unsigned total_rectangles=0;
 for(int selected=7;selected<=10;selected+=3){
  reset();theme=selected;char pristine[96];memcpy(pristine,ascii,96);
  assert(styled_surface_string(&s,ascii,95,101,80,0)==0);
  assert(refs==0&&font_gets==1&&releases==1&&stocks==0&&width_calls==95&&prefix_bytes==4560);
  assert(!memcmp(pristine,ascii,96));assert(paints<=95*21);assert(batch_calls==(paints+127)/128);
  /* Every painted pixel matches the original pixel-font runs positioned with
   * native prefix metrics, including spaces and variable-width characters. */
  int index=0;
  for(int i=0;i<95;i++)for(int row=0;row<7;row++){
   unsigned bits=xz_pixel_font_row((unsigned char)ascii[i],row);
   int start=native_width(ascii,i),adv=advance((unsigned char)ascii[i]),ink=adv>5?adv-1:adv;
   for(int col=0;col<5;){if(!(bits&(1u<<(4-col)))){col++;continue;}
    int end=col+1;while(end<5&&(bits&(1u<<(4-end))))end++;
    int l=101+start+col*ink/5,r=101+start+end*ink/5;
    if(r>l){assert(index<paints);struct rect got=rectangles[index++];assert(got.x==l&&got.y==66+row*2&&got.w==r-l&&got.h==2);}
    col=end;
   }
  }
  assert(index==paints);total_rectangles+=(unsigned)paints;
 }
 for(int test=0;test<20;test++){
  reset();theme=7;const char *text="TEST";int n=-1,x=0,y=20,flags=0;
  switch(test){
   case 0:theme=0;break;case 1:theme=8;break;case 2:text=NULL;break;
   case 3:flags=1;break;case 4:n=-2;break;case 5:n=257;break;
   case 6:x=-4097;break;case 7:y=4097;break;case 8:text="";break;
   case 9:text="A\xc3\xa9";break;case 10:text="A\nB";break;
   case 11:font_error=1;break;case 12:null_font=1;break;case 13:ascender_error=1;break;
   case 14:ascender=6;break;case 15:ascender=65;break;case 16:width_error=4;break;
   case 17:width_negative=1;break;case 18:width_huge=1;break;case 19:width_zero=1;break;
  }fallback(&s,text,n,x,y,flags);
 }
 reset();theme=7;fill_error=1;assert(styled_surface_string(&s,"A",1,0,20,0)==-37);assert(batch_calls==1&&paints>0&&stocks==0&&refs==0);
 reset();theme=10;assert(styled_surface_string(&s,"   ",3,0,20,0)==0);assert(paints==0&&refs==0&&releases==1);
 reset();char longtext[258];memset(longtext,'M',257);longtext[257]=0;theme=7;
 fallback(&s,longtext,-1,0,20,0);
 reset();assert(styled_surface_string(&s,longtext,256,0,20,0)==0);
 assert(width_calls==256&&prefix_bytes==32896&&paints<=5376&&refs==0);
 assert(batch_calls==(paints+127)/128&&batch_calls==32);
 printf("PASS actual native surface adapters; 95 ASCII glyph rectangles=%u per theme; 256-char rectangles=%d, batch calls=%d, prefix calls=%d, examined prefix bytes=%d\n",total_rectangles/2,paints,batch_calls,width_calls,prefix_bytes);
 reset();theme=7;fill_error=2;assert(styled_surface_string(&s,longtext,256,0,20,0)==-37);
 assert(batch_calls==2&&paints==256&&stocks==0&&refs==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xz-native-surface-') as temporary:
    build = Path(temporary)
    test = build / 'native_surface.c'
    test.write_text(declarations + functions + checks)
    binary = build / 'native_surface.exe'
    subprocess.run([str(Path(args.zig).resolve()), 'cc', '-O2', '-UNDEBUG',
                    '-Wall', '-Wextra', '-Werror', '-I',
                    str(root / 'mods/ui'), str(test),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
