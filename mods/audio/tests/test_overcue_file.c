/* SPDX-License-Identifier: MIT */
#include "../overcue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Overcue acceptance requires active assertions
#endif
int main(int argc,char **argv){
    if(argc!=3)return 2;struct xz_oc_assets assets;char error[256];
    int result=xz_oc_open(argv[1],&assets,error,sizeof(error));
    if(!strcmp(argv[2],"reject")){assert(result);puts("PASS rejected invalid bundle");return 0;}
    if(result){fprintf(stderr,"%s\n",error);return 1;}
    if(!strcmp(argv[2],"real")){
        int16_t samples[4096];
        for(unsigned r=0;r<3;r++){assert(!xz_oc_read(assets.role+r,(int64_t)(assets.frames/2),2048,samples));assert(assets.wave_count[r]);}
        printf("PASS real Overcue source identity, pages and waveforms: frames=%llu wave=%u/%u/%u\n",(unsigned long long)assets.frames,assets.wave_count[0],assets.wave_count[1],assets.wave_count[2]);
        xz_oc_close(&assets);return 0;
    }
    int16_t data[16];
    if(!strcmp(argv[2],"corrupt")){assert(xz_oc_read(&assets.role[0],0,8,data));xz_oc_close(&assets);puts("PASS rejected corrupt page");return 0;}
    assert(assets.frames==32771);assert(!xz_oc_read(&assets.role[0],32766,8,data));
    for(int i=0;i<5;i++){assert(data[i*2]==(int16_t)((32766+i)%1000));assert(data[i*2+1]==-data[i*2]);}
    for(int i=5;i<8;i++)assert(!data[i*2]&&!data[i*2+1]);
    assert(!xz_oc_read(&assets.role[0],-2,4,data));assert(data[0]==0&&data[2]==0&&data[4]==0&&data[6]==1);
    assert(assets.wave_count[0]==5&&assets.wave[0][4]==31);
    xz_oc_close(&assets);puts("PASS verified paged PCM, cross-page reads, bounds and waveform");return 0;
}
