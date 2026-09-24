/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../overcue.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#define RATE 44100u
#define SECONDS 18u
#define BLOCK 256u
static double seconds(void){struct timeval t;gettimeofday(&t,NULL);return t.tv_sec+t.tv_usec/1000000.0;}
static void wait_until(double target){double delay=target-seconds();if(delay>0){struct timespec t={(time_t)delay,(long)((delay-floor(delay))*1e9)};nanosleep(&t,NULL);}}
int main(int argc,char **argv){
    if(argc<2||argc>3)return 2;int loop=argc==3&&!strcmp(argv[2],"--loop");char error[256];struct xz_oc_assets reference;
    if(xz_oc_open(argv[1],&reference,error,sizeof(error))){puts(error);return 3;}
    size_t count=(SECONDS+1u)*XZ_OC_RATE;int16_t *source=malloc(count*4);if(!source)return 4;
    for(unsigned i=0;i<SECONDS+1u;i++)if(xz_oc_read(&reference.reference,(30+i)*XZ_OC_RATE,XZ_OC_RATE,source+i*XZ_OC_RATE*2))return 5;
    struct xz_oc_stream *stream=xz_oc_stream_open(argv[1],RATE,error,sizeof(error));if(!stream){puts(error);return 6;}
    double start=seconds();float pcm[BLOCK*2],before[BLOCK*2];unsigned changed[3]={0},pending[3]={0},blocks[3]={0},zero=0;
    uint32_t previous_misses=0,previous_position=0,loop_misses=0,wraps=0;
    const unsigned loop_start=3*RATE/BLOCK*BLOCK,loop_length=3*RATE/BLOCK*BLOCK;
    for(unsigned position=0;position<SECONDS*RATE;position+=BLOCK){
        wait_until(start+(double)position/RATE);
        unsigned played=loop&&position>=loop_start?loop_start+(position-loop_start)%loop_length:position;
        xz_oc_stream_loop_start(stream,loop?(int32_t)(30*RATE+loop_start):-1);
        for(unsigned i=0;i<BLOCK;i++)for(unsigned c=0;c<2;c++){
            double x=(double)(played+i)*XZ_OC_RATE/RATE;size_t at=(size_t)x;double f=x-at;
            pcm[i*2+c]=(float)((source[at*2+c]*(1-f)+source[(at+1)*2+c]*f)/32768.0);
        }
        memcpy(before,pcm,sizeof(pcm));
        float levels[3]={1,1,1};int phase=-1;
        if(position>=2*RATE&&position<9*RATE){levels[2]=0;phase=0;}
        else if(position>=9*RATE&&position<16*RATE){levels[0]=0.2f;levels[1]=0.6f;phase=1;}
        else if(position>=16*RATE)phase=2;
        size_t mixed=xz_oc_stream_mix(stream,pcm,BLOCK,30*RATE+played,levels);
        struct xz_oc_status status;xz_oc_stream_status(stream,&status);
        if(phase>=0){blocks[phase]++;changed[phase]+=mixed!=0;
            unsigned since=phase==0?position-2*RATE:phase==1?position-9*RATE:position-16*RATE;
            if(since>2*RATE&&status.misses!=previous_misses)pending[phase]++;
        }
        if(loop&&played<previous_position&&((position>5*RATE&&position<9*RATE)||(position>11*RATE&&position<16*RATE))){wraps++;loop_misses+=status.misses!=previous_misses;}
        previous_position=played;previous_misses=status.misses;
        double input=0,output=0;for(unsigned i=0;i<BLOCK*2;i++){input+=before[i]*before[i];output+=pcm[i]*pcm[i];}
        if(input>0.001&&output<1e-10){if(zero<3)printf("silent_at=%.4f source=%.4f phase=%d state=%d misses=%u\n",(double)position/RATE,30+(double)played/RATE,phase,status.state,status.misses);zero++;}
    }
    struct xz_oc_status status;xz_oc_stream_status(stream,&status);
    printf("lag=%d correlation=%.6f elapsed=%.2f unexpected_silent_blocks=%u\n",status.lag,status.correlation,seconds()-start,zero);
    for(unsigned i=0;i<3;i++)printf("phase=%u blocks=%u processed=%u steady_pending=%u\n",i,blocks[i],changed[i],pending[i]);
    if(loop)printf("loop_wraps=%u loop_cache_misses=%u\n",wraps,loop_misses);
    int ok=(!loop||(wraps>=3&&!loop_misses))&&status.correlation>=0.995&&!zero&&changed[0]>blocks[0]*0.5&&changed[1]>blocks[1]*0.5&&!pending[0]&&!pending[1];
    xz_oc_stream_close(stream);xz_oc_close(&reference);free(source);return ok?0:1;
}
