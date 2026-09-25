#ifdef NDEBUG
#error Native asset theme tests require assertions
#endif
#include "native_asset_theme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define STRIDE 132
#define ROWS 36
static uint16_t source[STRIDE*ROWS],saved[STRIDE*ROWS],dest[STRIDE*ROWS+2],lut[65536];
static void reset_dest(void){for(size_t i=0;i<sizeof(dest)/sizeof(dest[0]);i++)dest[i]=0x1357;}
static void check_guards(void){
 assert(dest[0]==0x1357&&dest[STRIDE*ROWS+1]==0x1357);
 for(size_t y=0;y<ROWS;y++)for(size_t x=128;x<STRIDE;x++)assert(dest[1+y*STRIDE+x]==0x1357);
 assert(memcmp(source,saved,sizeof(source))==0);
}
int main(void){
 for(unsigned i=0;i<65536;i++)lut[i]=(uint16_t)(i^0x04a5);
 const uint16_t keys[3]={0xf81f,0x0000,0xffff};
 for(unsigned k=0;k<3;k++){
  uint16_t key=keys[k];
  for(size_t i=0;i<STRIDE*ROWS;i++)source[i]=(uint16_t)(i*13u+1u);
  for(size_t y=0;y<ROWS;y++)source[y*STRIDE+127]=key;
  source[5*STRIDE+12]=key;source[0]=key;
  memcpy(saved,source,sizeof(source));
  for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(unsigned id=76;id<=87;id++){
   reset_dest();
   int result=xz_native_asset_theme_render(id,theme,source,128,36,STRIDE,dest+1,STRIDE,STRIDE*ROWS,lut,1,key,theme&1);
   int styled=theme!=0&&id>=77&&id<=86;assert(result==(styled?2:1));
   unsigned changed=0;
   for(size_t y=0;y<ROWS;y++)for(size_t x=0;x<128;x++){
    uint16_t old=source[y*STRIDE+x],pixel=dest[1+y*STRIDE+x];
    if(old==key)assert(pixel==key);
    else if(!styled)assert(pixel==(theme?lut[old]:old));
    else if(pixel!=lut[old])changed++;
   }
   if(styled)assert(changed>100);
   if(styled&&theme!=9){
    assert(dest[1+18*STRIDE+64]==(source[18*STRIDE+64]==key?key:lut[source[18*STRIDE+64]]));
    assert(dest[1+STRIDE+64]==(source[STRIDE+64]==key?key:lut[source[STRIDE+64]]));
   }
   check_guards();
  }
 }
 assert(xz_native_asset_role(1459,784,30).role==XZ_ASSET_FIELD);
 assert(xz_native_asset_role(650,400,30).role==XZ_ASSET_TITLE_STRIP);
 assert(xz_native_asset_role(650,399,30).role==XZ_ASSET_UNCLASSIFIED);
 assert(xz_native_asset_role(1344,240,38).role==XZ_ASSET_UNCLASSIFIED);
 /* Tiny/mismatched IDs never run frame drawing and never touch row padding. */
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  reset_dest();assert(xz_native_asset_theme_render(77,theme,source,1,1,STRIDE,dest+1,STRIDE,1,lut,0,source[0],0)==1);
  assert(dest[1]==(theme?lut[source[0]]:source[0]));assert(dest[2]==0x1357);
 }
 reset_dest();assert(!xz_native_asset_theme_render(77,7,source,128,36,STRIDE,dest+1,STRIDE,20,lut,1,0xf81f,0));
 assert(dest[1]==0x1357);
 assert(!xz_native_asset_theme_render(77,7,source,128,36,STRIDE,source,STRIDE,STRIDE*ROWS,lut,1,0xf81f,0));
 assert(!xz_native_asset_theme_render(77,7,source,128,36,127,dest+1,STRIDE,STRIDE*ROWS,lut,0,0,0));
 assert(!xz_native_asset_theme_render(77,7,source,128,36,SIZE_MAX,dest+1,STRIDE,STRIDE*ROWS,lut,0,0,0));
 assert(!xz_native_asset_theme_render(77,7,source,128,36,STRIDE,dest+1,STRIDE,STRIDE*ROWS,NULL,0,0,0));
 assert(xz_native_asset_theme_render(77,0,source,128,36,STRIDE,dest+1,STRIDE,STRIDE*ROWS,NULL,0,0,0)==1);
 check_guards();puts("PASS native asset theme copy, all themes/IDs, arbitrary keys, padding, source immutability and bounds");return 0;
}
