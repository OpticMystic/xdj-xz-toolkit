#ifndef XZ_NATIVE_ASSET_VIEW_H
#define XZ_NATIVE_ASSET_VIEW_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint16_t xz_asset_u16(const unsigned char *p){return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static inline uint32_t xz_asset_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void xz_asset_put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}

/* Pure descriptor adapter for the verified type-5 RGB16 native asset branch.
   No live hook is installed here. Caller must keep both allocations alive for
   the entire synchronous original draw and validate its sorted record offsets. */
static inline int xz_asset_themed_view(const unsigned char source[68],unsigned char out[68],
        const unsigned char *pack,size_t size,uint32_t original_base,uint32_t themed_base){
 if(!source||!out||!pack||size<44||size>UINT32_MAX||original_base>UINT32_MAX-size||themed_base>UINT32_MAX-size)return 0;
 if(xz_asset_u32(source)!=5)return 0;
 uint32_t first=xz_asset_u32(pack+32),pointer=xz_asset_u32(source+12);
 if(first!=1582u*44u||first>size||pointer<original_base||pointer-original_base>=size)return 0;
 unsigned width=xz_asset_u16(source+4),height=xz_asset_u16(source+6),stride=xz_asset_u16(source+16);
 if(!width||!height||width>32767||stride!=width*2)return 0;
 if(xz_asset_u32(source+24)&~1u)return 0;
 if(xz_asset_u32(source+24)&1u){
  uint16_t key=(uint16_t)((source[28]>>3)<<11|(source[29]>>2)<<5|(source[30]>>3));
  if(key!=0xf81f)return 0; /* Other native keys are not qualified by this pack builder. */
 }
 uint32_t offset=pointer-original_base;
 unsigned low=0,high=1582;
 while(low<high){
  unsigned i=low+(high-low)/2;
  const unsigned char *entry=pack+i*44;
  uint32_t candidate=xz_asset_u32(entry+32);
  if(candidate<offset){low=i+1;continue;}
  if(candidate>offset){high=i;continue;}
  if(xz_asset_u32(entry+24)!=2||xz_asset_u32(entry+28)!=i*44||xz_asset_u32(entry+36)!=size||
     xz_asset_u16(entry+4)!=width||xz_asset_u16(entry+6)!=height||
     offset<first||(uint64_t)offset+(uint64_t)stride*height>size)return 0;
  memcpy(out,source,68);xz_asset_put32(out+12,themed_base+offset);return 1;
 }
 return 0;
}
#endif
