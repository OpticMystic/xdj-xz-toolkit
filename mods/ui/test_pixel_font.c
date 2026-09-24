#ifdef NDEBUG
#error Pixel font acceptance requires assertions
#endif
#include "pixel_font.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 for(unsigned ch=0;ch<512;ch++)for(unsigned r=0;r<12;r++){
  unsigned bits=xz_pixel_font_row(ch,r);assert(bits<32);if(r>=7)assert(bits==0);
 }
 for(unsigned ch='a';ch<='z';ch++)for(unsigned r=0;r<7;r++)assert(xz_pixel_font_row(ch,r)==xz_pixel_font_row(ch-32,r));
 for(unsigned r=0;r<7;r++){assert(!xz_pixel_font_row(' ',r));assert(xz_pixel_font_row(0,r)==xz_pixel_font_row('?',r));}
 assert(xz_pixel_font_row('A',3)==31);assert(xz_pixel_font_row('_',6)==31);
 puts("PASS bounded pixel glyph rows, lowercase and fallback");return 0;
}
