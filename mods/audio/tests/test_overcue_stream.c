/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../overcue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#ifdef NDEBUG
#error Overcue acceptance requires active assertions
#endif
static void pause_worker(void){struct timespec t={0,20000000};nanosleep(&t,NULL);}
static void original(struct xz_oc_assets *a,int64_t at,float pcm[4096]){
    double first=(at+29.4)*96000/44100;int64_t base=(int64_t)floor(first);int16_t raw[10000];
    assert(!xz_oc_read(&a->reference,base,5000,raw));
    for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++){
        double t=first+i*96000.0/44100-base;size_t k=(size_t)t;double f=t-k;
        pcm[i*2+c]=(float)((raw[k*2+c]*(1-f)+raw[(k+1)*2+c]*f)/32768*1.5);
    }
}
int main(int argc,char **argv){
    const float choices[]={0,0.25f,0.7f,1};
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<4;b++)for(unsigned c=0;c<4;c++){
        float levels[3]={choices[a],choices[b],choices[c]},parts[3]={0.2f,0.3f,0.4f};struct xz_oc_plan plan;
        assert(!xz_oc_plan_levels(levels,&plan));float actual=0.9f*plan.source,expected=0;
        for(unsigned i=0;i<3;i++){expected+=levels[i]*parts[i];for(unsigned j=0;j<2;j++)if(plan.mask[j]&(1u<<i))actual+=plan.gain[j]*parts[i];}
        assert(fabsf(actual-expected)<0.000001f);
    }
    if(argc!=2)return 2;struct xz_oc_assets assets;char error[256];assert(!xz_oc_open(argv[1],&assets,error,sizeof(error)));
    struct xz_oc_stream *s=xz_oc_stream_open(argv[1],44100,error,sizeof(error));assert(s);
    float pcm[4096],before[4096];const float unity[3]={1,1,1},mute[3]={0,0,0};struct xz_oc_status status;
    struct timeval begin,end;gettimeofday(&begin,NULL);
    for(unsigned attempt=0;attempt<400;attempt++){
        int64_t position=10000+(attempt%8)*3000;original(&assets,position,pcm);memcpy(before,pcm,sizeof(pcm));
        for(unsigned chunk=0;chunk<2048;chunk+=64)assert(!xz_oc_stream_mix(s,pcm+chunk*2,64,position+chunk,unity));
        assert(!memcmp(pcm,before,sizeof(pcm)));pause_worker();
        xz_oc_stream_status(s,&status);if(status.state==XZ_OC_READY)break;
    }
    gettimeofday(&end,NULL);
    fprintf(stderr,"alignment state=%d lag=%d correlation=%f elapsed_ms=%ld\n",status.state,status.lag,status.correlation,
        (long)((end.tv_sec-begin.tv_sec)*1000+(end.tv_usec-begin.tv_usec)/1000));
    assert(status.state==XZ_OC_READY);assert(abs(status.lag-29)<=1);assert(status.correlation>0.985);
    original(&assets,15000,pcm);xz_oc_stream_mix(s,pcm,2048,15000,mute);pause_worker();
    original(&assets,15000,pcm);assert(xz_oc_stream_mix(s,pcm,2048,15000,mute)==2048);
    for(unsigned i=0;i<4096;i++)assert(pcm[i]==0);
    original(&assets,15000,pcm);xz_oc_stream_mix(s,pcm,2048,15000,unity);
    original(&assets,15000,pcm);memcpy(before,pcm,sizeof(pcm));assert(!xz_oc_stream_mix(s,pcm,2048,15000,unity));assert(!memcmp(pcm,before,sizeof(pcm)));
    for(unsigned i=0;i<4096;i++)pcm[i]=0.5f;
    const float no_vocals[3]={1,1,0};memcpy(before,pcm,sizeof(pcm));
    assert(!xz_oc_stream_mix(s,pcm,2048,1000000,no_vocals));assert(!memcmp(pcm,before,sizeof(pcm)));
    for(unsigned attempt=0;attempt<200;attempt++){
        original(&assets,15000,pcm);xz_oc_stream_mix(s,pcm,2048,15000,no_vocals);pause_worker();xz_oc_stream_status(s,&status);
        if(status.state==XZ_OC_READY)break;
    }
    assert(status.state==XZ_OC_READY);original(&assets,15000,pcm);assert(xz_oc_stream_mix(s,pcm,2048,15000,no_vocals)==2048);
    double energy=0;for(unsigned i=256;i<4096;i++)energy+=pcm[i]*pcm[i];assert(energy>0.01);
    double start=(15000+29.4)*96000/44100;int64_t base=(int64_t)floor(start);int16_t expected_pcm[10000];
    assert(!xz_oc_read(&assets.combination[0],base,5000,expected_pcm));
    double error_sum=0;
    for(unsigned i=128;i<2048;i++)for(unsigned c=0;c<2;c++){
        double t=start+i*96000.0/44100-base;size_t k=(size_t)t;double f=t-k;
        double expected=(expected_pcm[k*2+c]*(1-f)+expected_pcm[(k+1)*2+c]*f)/32768*1.5;
        double delta=pcm[i*2+c]-expected;error_sum+=delta*delta;
    }
    assert(sqrt(error_sum/(1920*2))<0.003);
    xz_oc_stream_loop_start(s,15000);
    for(unsigned warm=0;warm<15;warm++)pause_worker();
    for(unsigned cycle=0;cycle<3;cycle++){
        for(int64_t position=60000;position<=260000;position+=40000){
            for(unsigned warm=0;warm<100;warm++){
                original(&assets,position,pcm);xz_oc_stream_mix(s,pcm,2048,position,no_vocals);
                pause_worker();xz_oc_stream_status(s,&status);if(status.state==XZ_OC_READY)break;
            }
            assert(status.state==XZ_OC_READY);
        }
        xz_oc_stream_status(s,&status);uint32_t misses=status.misses;
        original(&assets,15000,pcm);assert(xz_oc_stream_mix(s,pcm,2048,15000,no_vocals)==2048);
        xz_oc_stream_status(s,&status);assert(status.misses==misses);
        double loop_error=0,loop_energy=0;
        for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++){
            double t=start+i*96000.0/44100-base;size_t k=(size_t)t;double f=t-k;
            double expected=(expected_pcm[k*2+c]*(1-f)+expected_pcm[(k+1)*2+c]*f)/32768*1.5;
            double delta=pcm[i*2+c]-expected;loop_error+=delta*delta;loop_energy+=pcm[i*2+c]*pcm[i*2+c];
        }
        assert(sqrt(loop_error/4096)<0.003&&loop_energy>0.01);
    }
    const float layered[3]={0.2f,0.6f,1};
    for(unsigned step=0;step<80;step++){
        original(&assets,15000,pcm);xz_oc_stream_mix(s,pcm,2048,15000,step<40?layered:no_vocals);
        double energy=0;for(unsigned i=0;i<4096;i++)energy+=pcm[i]*pcm[i];assert(energy>0.01);
        pause_worker();
    }
    puts("PASS loop mix changes retain audible prior selection while replacement cache fills");
    puts("PASS loop return: first sample onward matches selected mix, no mute leak, silence or cache miss");
    xz_oc_stream_close(s);xz_oc_close(&assets);puts("PASS mix plans, measured alignment, bit-exact unity, selected mix and no silent loading gap");return 0;
}
