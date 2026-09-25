#include "native_asset_view.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Asset view tests require assertions
#endif
static unsigned char pack[1582*44+8+1581*2];
int main(void){
 unsigned char descriptor[68]={0},themed[68],saved[68];
 const uint32_t base=0x100000,copy=0x200000,offset=1582*44;
 for(unsigned i=1;i<1582;i++){
  unsigned char *entry=pack+i*44;entry[4]=entry[6]=1;
  xz_asset_put32(entry+24,2);xz_asset_put32(entry+28,i*44);
  xz_asset_put32(entry+32,offset+8+(i-1)*2);xz_asset_put32(entry+36,sizeof(pack));
 }
 pack[4]=2;pack[6]=2;xz_asset_put32(pack+24,2);xz_asset_put32(pack+32,offset);xz_asset_put32(pack+36,sizeof(pack));
 xz_asset_put32(descriptor,5);descriptor[4]=2;descriptor[6]=2;descriptor[16]=4;
 xz_asset_put32(descriptor+12,base+offset);xz_asset_put32(descriptor+24,1);
 descriptor[28]=255;descriptor[30]=255;memcpy(saved,descriptor,68);
 for(int repeat=0;repeat<1000;repeat++){
  assert(xz_asset_themed_view(descriptor,themed,pack,sizeof(pack),base,copy));
  assert(xz_asset_u32(themed+12)==copy+offset);
  assert(!memcmp(descriptor,saved,68));
  xz_asset_put32(themed+12,base+offset);assert(!memcmp(themed,descriptor,68));
 }
 for(int mode=0;mode<7;mode++){
  memcpy(descriptor,saved,68);
  if(mode==0)xz_asset_put32(descriptor,0);
  if(mode==1)xz_asset_put32(descriptor+12,base-1);
  if(mode==2)descriptor[4]=3;
  if(mode==3)descriptor[16]=5;
  if(mode==4)descriptor[28]=0;
  if(mode==5)xz_asset_put32(descriptor+24,2);
  if(mode==6)xz_asset_put32(descriptor+12,base+offset+1);
  memset(themed,0xab,68);assert(!xz_asset_themed_view(descriptor,themed,pack,sizeof(pack),base,copy));
  for(unsigned i=0;i<68;i++)assert(themed[i]==0xab);
 }
 assert(!xz_asset_themed_view(saved,themed,pack,sizeof(pack),base,UINT32_MAX-1));
 puts("PASS immutable native descriptors, private asset offsets, key/format guards and repeated draws");
}
