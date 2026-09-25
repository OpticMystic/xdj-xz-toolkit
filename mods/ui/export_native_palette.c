#include "native_palette.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;
 char *end;long theme=strtol(argv[1],&end,10);
 if(*end||theme<0||theme>=XZ_THEME_COUNT)return 2;
 FILE *file=fopen(argv[2],"wb");if(!file)return 3;
 for(unsigned pixel=0;pixel<65536;pixel++){
  uint16_t out=xz_native_palette_pixel((int)theme,(uint16_t)pixel);
  unsigned char bytes[2]={(unsigned char)out,(unsigned char)(out>>8)};
  if(fwrite(bytes,1,2,file)!=2){fclose(file);return 4;}
 }
 return fclose(file)?5:0;
}
