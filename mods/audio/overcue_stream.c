/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "overcue.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__) && defined(__arm__)
#include <sched.h>
#include <unistd.h>
#include <sys/prctl.h>
#endif

#define WINDOW XZ_OC_WINDOW_FRAMES
#define CAPTURE 2048u
#define SEARCH 8192
int xz_oc_worker_schedule(void){
#if defined(__linux__) && defined(__arm__)
    if(sysconf(_SC_NPROCESSORS_ONLN)<4)return -1;
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(1,&mask);CPU_SET(2,&mask);
    struct sched_param priority={0};
    if(pthread_setschedparam(pthread_self(),SCHED_OTHER,&priority))return -1;
    if(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask))return -1;
    prctl(PR_SET_NAME,"XZStemWorker",0,0,0);
#endif
    return 0;
}
struct window { int64_t first; int16_t *pcm[2]; unsigned masks; int access, valid; };
static int window_acquire(struct window *w){
    int readers=__atomic_load_n(&w->access,__ATOMIC_ACQUIRE);
    for(int attempt=0;attempt<3&&readers>=0;attempt++)
        if(__atomic_compare_exchange_n(&w->access,&readers,readers+1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 1;
    return 0;
}
static void window_release(struct window *w){__atomic_fetch_sub(&w->access,1,__ATOMIC_RELEASE);}

struct xz_oc_stream {
    struct xz_oc_assets assets;
    uint32_t rate;
    pthread_t worker;
    int running, published, state, capture_state;
    int64_t desired, capture_position, previous_position;
    size_t capture_count;
    float capture[CAPTURE*2];
    struct window window[4];
    int loop_published;
    int32_t loop_start;
    int starved;
    double offset, scale;
    int confirmations, attempts;
    float score;
    uint32_t blocks, misses;
    uint32_t previous_levels[3], request_masks;
};
static float sample(const int16_t *pcm,size_t frames,double at,int channel){
    if(at<0||at>=(double)(frames-1))return 0;
    size_t i=(size_t)at;double fraction=at-(double)i;
    return (float)(((1-fraction)*pcm[i*2+(unsigned)channel]+fraction*pcm[(i+1)*2+(unsigned)channel])/32768.0);
}
int xz_oc_plan_levels(const float levels[3],struct xz_oc_plan *out){
    if(!levels||!out)return -1;
    unsigned order[3]={0,1,2};
    for(unsigned i=0;i<3;i++)if(!isfinite(levels[i])||levels[i]<0||levels[i]>2)return -1;
    for(unsigned i=0;i<3;i++)for(unsigned j=i+1;j<3;j++)if(levels[order[j]]<levels[order[i]]){unsigned t=order[i];order[i]=order[j];order[j]=t;}
    *out=(struct xz_oc_plan){0};out->source=levels[order[0]];
    out->gain[0]=levels[order[1]]-levels[order[0]];out->gain[1]=levels[order[2]]-levels[order[1]];
    if(out->gain[0])out->mask[0]=(1u<<order[1])|(1u<<order[2]);
    if(out->gain[1])out->mask[1]=1u<<order[2];
    return 0;
}
static unsigned plan_masks(const struct xz_oc_plan *p){return p->mask[0]|p->mask[1]<<4;}
struct refill { struct xz_oc_file *file; int64_t first; int16_t *output; int result; };
static void *refill_role(void *context){struct refill *j=context;j->result=xz_oc_read(j->file,j->first,WINDOW,j->output);return NULL;}
static int refill_window(struct xz_oc_stream *s,struct window *w){
    struct refill jobs[2];pthread_t threads[2];int spawned[2]={0,0};
    unsigned masks[2]={w->masks&15,w->masks>>4};
    for(unsigned r=0;r<2;r++){
        jobs[r]=(struct refill){xz_oc_mix_file(&s->assets,masks[r]),w->first,w->pcm[r],0};
        if(!masks[r])continue;
        if(!jobs[r].file)return 0;
        if(masks[0]&&masks[1])spawned[r]=pthread_create(threads+r,NULL,refill_role,jobs+r)==0;
        if(!spawned[r])refill_role(jobs+r);
    }
    int ok=1;for(unsigned r=0;r<2;r++){if(spawned[r])pthread_join(threads[r],NULL);if(jobs[r].result)ok=0;}return ok;
}
static int fill_slot(struct xz_oc_stream *s,int slot,int64_t first,unsigned masks){
    struct window *w=&s->window[slot];int idle=0;
    if(!__atomic_compare_exchange_n(&w->access,&idle,-1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    w->valid=0;w->first=first;w->masks=masks;
    int ok=refill_window(s,w);w->valid=ok;
    __atomic_store_n(&w->access,0,__ATOMIC_RELEASE);
    return ok?1:-1;
}
static int window_has(const struct window *w,const struct xz_oc_plan *p){
    if(!plan_masks(p))return 1;if(!w||!w->valid)return 0;
    for(unsigned r=0;r<2;r++)if(p->mask[r]&&p->mask[r]!=(w->masks&15)&&p->mask[r]!=(w->masks>>4))return 0;
    return 1;
}
static float planned_sample(const struct xz_oc_stream *s,const struct window *w,const struct xz_oc_plan *p,double at,int channel,float native){
    float result=native*p->source;
    for(unsigned r=0;r<2;r++)if(p->mask[r]){
        unsigned slot=p->mask[r]==(w->masks&15)?0:1;
        result+=sample(w->pcm[slot],WINDOW,at,channel)*(float)(s->scale*p->gain[r]/xz_oc_mix_gain(&s->assets,p->mask[r]));
    }
    return result;
}
static double correlation(const float *native,const float *reference,size_t n,int lag,unsigned step,double *scale){
    double cross=0,original=0,prepared=0;
    const float *r=reference+(size_t)(SEARCH+lag)*2;
    for(size_t i=0;i<n;i+=step)for(unsigned c=0;c<2;c++){
        double a=native[i*2+c],b=r[i*2+c];cross+=a*b;original+=a*a;prepared+=b*b;
    }
    if(original<0.00001||prepared<0.00001)return 0;
    if(scale)*scale=cross/prepared;
    return cross/sqrt(original*prepared);
}
static double fractional_correlation(const float *native,const float *reference,size_t n,double lag,double *scale){
    int base=(int)floor(lag);double fraction=lag-base,cross=0,original=0,prepared=0;
    const float *r=reference+(size_t)(SEARCH+base)*2;
    for(size_t i=0;i<n;i++)for(unsigned c=0;c<2;c++){
        double a=native[i*2+c],b=r[i*2+c]*(1-fraction)+r[(i+1)*2+c]*fraction;
        cross+=a*b;original+=a*a;prepared+=b*b;
    }
    if(original<0.00001||prepared<0.00001)return 0;
    if(scale)*scale=cross/prepared;return cross/sqrt(original*prepared);
}
static int align(struct xz_oc_stream *s){
    size_t native_count=s->capture_count+SEARCH*2+2;
    double first=(double)(s->capture_position-SEARCH)*XZ_OC_RATE/s->rate;
    int64_t raw_first=(int64_t)floor(first);
    size_t raw_count=(size_t)ceil((double)native_count*XZ_OC_RATE/s->rate)+3;
    int16_t *raw=malloc(raw_count*4);float *reference=malloc(native_count*2*sizeof(float));
    if(!raw||!reference){free(raw);free(reference);return -1;}
    if(xz_oc_read(&s->assets.reference,raw_first,raw_count,raw)){free(raw);free(reference);return -1;}
    for(size_t i=0;i<native_count;i++)for(unsigned c=0;c<2;c++)
        reference[i*2+c]=sample(raw,raw_count,first+(double)i*XZ_OC_RATE/s->rate-(double)raw_first,(int)c);
    double peaks[16];int locations[16];
    for(unsigned i=0;i<16;i++){peaks[i]=-1;locations[i]=-SEARCH*2;}
    for(int lag=-SEARCH+1;lag<SEARCH;lag++){
        double score=correlation(s->capture,reference,s->capture_count,lag,4,NULL);
        int slot=0;
        for(int i=0;i<16;i++){
            if(abs(locations[i]-lag)<=3){slot=i;break;}
            if(peaks[i]<peaks[slot])slot=i;
        }
        if(score>peaks[slot]){peaks[slot]=score;locations[slot]=lag;}
    }
    double best_score=-1,scale=1,offset=0,refined[16],positions[16];
    for(unsigned peak=0;peak<16;peak++){
        refined[peak]=-1;positions[peak]=locations[peak];
        for(int part=-15;part<=15;part++){
            double candidate=locations[peak]+part*0.05;
            if(candidate<=-SEARCH||candidate>=SEARCH)continue;
            double candidate_scale=1,score=fractional_correlation(s->capture,reference,s->capture_count,candidate,&candidate_scale);
            if(score>refined[peak]){refined[peak]=score;positions[peak]=candidate;}
            if(score>best_score){best_score=score;offset=candidate;scale=candidate_scale;}
        }
    }
    free(raw);free(reference);s->attempts++;
    if(best_score<0.995||scale<0.1||scale>8)return 0;
    for(unsigned peak=0;peak<16;peak++)if(fabs(positions[peak]-offset)>4&&refined[peak]>best_score-0.003)return 0;
    if(!s->confirmations||fabs(s->offset-offset)>1.25||fabs(s->scale-scale)>s->scale*0.08){
        s->confirmations=1;s->offset=offset;s->scale=scale;s->previous_position=s->capture_position;s->score=(float)best_score;return 0;
    }
    if(llabs(s->capture_position-s->previous_position)<512)return 0;
    s->offset=(s->offset+offset)*0.5;s->scale=(s->scale+scale)*0.5;s->score=(float)best_score;s->confirmations++;
    return 1;
}
static void *stream_worker(void *context){
    struct xz_oc_stream *s=context;int aligned=0;
    if(xz_oc_worker_schedule()){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);return NULL;}
    while(__atomic_load_n(&s->running,__ATOMIC_ACQUIRE)){
        if(!aligned&&__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==2){
            int result=align(s);
            if(result<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
            if(result>0){aligned=1;__atomic_store_n(&s->capture_state,3,__ATOMIC_RELEASE);}
            else { s->capture_count=0; __atomic_store_n(&s->capture_state,0,__ATOMIC_RELEASE); }
        }
        if(aligned){
            unsigned masks=__atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE);
            if(!masks){__atomic_store_n(&s->state,XZ_OC_READY,__ATOMIC_RELEASE);struct timespec idle={0,5000000};nanosleep(&idle,NULL);continue;}
            int64_t desired=__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE);
            int64_t at=(int64_t)floor(((double)desired+s->offset)*XZ_OC_RATE/s->rate);
            int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
            int32_t loop=__atomic_load_n(&s->loop_start,__ATOMIC_ACQUIRE);
            int64_t loop_at=(int64_t)floor(((double)loop+s->offset)*XZ_OC_RATE/s->rate)-9600;
            int loop_current=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
            struct window *pinned=&s->window[loop_current<0?2:loop_current];
            if(loop>=0&&(!pinned->valid||pinned->first!=loop_at||pinned->masks!=masks)){
                int candidate=loop_current==2?3:2;
                int ok=fill_slot(s,candidate,loop_at,masks);
                if(ok>0){__atomic_store_n(&s->loop_published,candidate,__ATOMIC_SEQ_CST);pinned=&s->window[candidate];}
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
            }
            int loop_covered=pinned->valid&&pinned->masks==masks&&at>=pinned->first&&at+72000<pinned->first+WINDOW;
            int refill=!loop_covered&&(current<0||s->window[current].masks!=masks||
                (s->window[current].first>0&&at<s->window[current].first+4800)||at>s->window[current].first+WINDOW/3);
            if(refill){
                int candidate=current==0?1:0;
                int ok=fill_slot(s,candidate,at-9600,masks);
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
                if(ok)__atomic_store_n(&s->published,candidate,__ATOMIC_SEQ_CST);
            }
            int64_t latest=(int64_t)floor(((double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)+s->offset)*XZ_OC_RATE/s->rate);
            int ready=0;
            for(unsigned i=0;i<4;i++){
                struct window *w=&s->window[i];
                ready|=w->valid&&w->masks==masks&&latest>=w->first&&latest+72000<w->first+WINDOW;
            }
            __atomic_store_n(&s->state,ready?XZ_OC_READY:XZ_OC_BUFFERING,__ATOMIC_RELEASE);
        }
        struct timespec delay={0,5000000};nanosleep(&delay,NULL);
    }return NULL;
}
struct xz_oc_stream *xz_oc_stream_open(const char *path,uint32_t rate,char *error,size_t capacity){
    if(rate<32000||rate>96000){snprintf(error,capacity,"Unsupported XZ reader rate");return NULL;}
    struct xz_oc_stream *s=calloc(1,sizeof(*s));if(!s)return NULL;s->rate=rate;s->published=-1;s->loop_published=-1;s->loop_start=-1;s->state=XZ_OC_ALIGNING;
    if(xz_oc_open(path,&s->assets,error,capacity)){free(s);return NULL;}
    for(unsigned r=0;r<3;r++){float one=1;memcpy(&s->previous_levels[r],&one,4);}
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<2;r++){
        s->window[i].pcm[r]=malloc(WINDOW*4);
        if(!s->window[i].pcm[r]){xz_oc_stream_close(s);return NULL;}
    }
    s->running=1;
    if(pthread_create(&s->worker,NULL,stream_worker,s)){s->running=0;xz_oc_stream_close(s);return NULL;}
    return s;
}
void xz_oc_stream_close(struct xz_oc_stream *s){
    if(!s)return;if(s->running){__atomic_store_n(&s->running,0,__ATOMIC_RELEASE);pthread_join(s->worker,NULL);}
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<2;r++)free(s->window[i].pcm[r]);
    xz_oc_close(&s->assets);free(s);
}
void xz_oc_stream_loop_start(struct xz_oc_stream *s,int32_t position){
    if(s)__atomic_store_n(&s->loop_start,position,__ATOMIC_RELEASE);
}
static struct window *find_window(struct xz_oc_stream *s,const struct xz_oc_plan *plan,double at,double span,int headroom){
    int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
    int loop=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
    int order[4]={current<0?0:current,loop<0?2:loop,loop==2?3:2,current==0?1:0};
    for(unsigned i=0;i<4;i++){
        struct window *w=&s->window[order[i]];
        if(!window_acquire(w))continue;
        double first=at-w->first,last=first+span;
        if(w->valid&&window_has(w,plan)&&first>=0&&last<WINDOW-1&&(!headroom||WINDOW-last>=72000))return w;
        window_release(w);
    }
    return NULL;
}
size_t xz_oc_stream_mix(struct xz_oc_stream *s,float *pcm,size_t frames,int64_t position,const float levels[3]){
    if(!s||!pcm||!frames||frames>65536||position<0)return 0;
    struct xz_oc_plan requested;
    if(xz_oc_plan_levels(levels,&requested))return 0;
    __atomic_store_n(&s->request_masks,plan_masks(&requested),__ATOMIC_RELEASE);
    __atomic_store_n(&s->desired,position,__ATOMIC_RELEASE);
    int expected=0;
    if(__atomic_compare_exchange_n(&s->capture_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        int64_t end=s->capture_position+(int64_t)s->capture_count;
        if(!s->capture_count||position>end||position+(int64_t)frames<=s->capture_position){s->capture_position=position;s->capture_count=0;end=position;}
        if(position<=end&&position+(int64_t)frames>end){
            size_t skip=(size_t)(end-position),n=frames-skip;if(n>CAPTURE-s->capture_count)n=CAPTURE-s->capture_count;
            memcpy(s->capture+s->capture_count*2,pcm+skip*2,n*2*sizeof(float));s->capture_count+=n;
        }
        __atomic_store_n(&s->capture_state,s->capture_count>=1024?2:0,__ATOMIC_RELEASE);
    }
    int aligned=__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==3;
    if(!aligned&&plan_masks(&requested))return 0;
    float previous[3],applied[3];
    for(unsigned r=0;r<3;r++){uint32_t bits=__atomic_load_n(&s->previous_levels[r],__ATOMIC_RELAXED);memcpy(previous+r,&bits,4);applied[r]=levels[r];}
    struct xz_oc_plan old; xz_oc_plan_levels(previous,&old);
    double at=aligned?((double)position+s->offset)*XZ_OC_RATE/s->rate:0;
    double span=(double)(frames-1)*XZ_OC_RATE/s->rate;
    int switching=plan_masks(&requested)!=plan_masks(&old);
    struct window *w=find_window(s,&requested,at,span,switching);
    int error=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE)==XZ_OC_ERROR;
    if(error&&w){window_release(w);w=NULL;}
    int starved=0;
    if(plan_masks(&requested)&&!w){
        __atomic_fetch_add(&s->misses,1,__ATOMIC_RELAXED);
        if(!error)__atomic_store_n(&s->state,XZ_OC_BUFFERING,__ATOMIC_RELEASE);
        if(!error)w=find_window(s,&old,at,span,0);
        requested=old;memcpy(applied,previous,sizeof(applied));
        if(plan_masks(&old)&&!w){
            requested=(struct xz_oc_plan){.source=old.source};
            old=requested;starved=1;
        }
    }
    int was_starved=__atomic_load_n(&s->starved,__ATOMIC_RELAXED);
    if(!window_has(w,&old)||was_starved)old=(struct xz_oc_plan){.source=old.source};
    int changed=was_starved!=starved;__atomic_store_n(&s->starved,starved,__ATOMIC_RELAXED);
    for(unsigned r=0;r<3;r++){changed|=previous[r]!=applied[r];uint32_t bits;memcpy(&bits,applied+r,4);__atomic_store_n(&s->previous_levels[r],bits,__ATOMIC_RELAXED);}
    if(requested.source==1&&!plan_masks(&requested)&&!changed){if(w)window_release(w);return 0;}
    double first=w?at-w->first:0;
    size_t ramp=frames<128?frames:128;
    for(size_t i=0;i<frames;i++)for(unsigned c=0;c<2;c++){
        double at=first+(double)i*XZ_OC_RATE/s->rate;float native=pcm[i*2+c];
        float value=planned_sample(s,w,&requested,at,(int)c,native);
        if(changed&&i<ramp){float f=(float)(i+1)/(float)ramp;value=planned_sample(s,w,&old,at,(int)c,native)*(1-f)+value*f;}
        pcm[i*2+c]=value>1?1:value< -1?-1:value;
    }
    __atomic_fetch_add(&s->blocks,1,__ATOMIC_RELAXED);
    if(w)window_release(w);return frames;
}

void xz_oc_stream_status(struct xz_oc_stream *s,struct xz_oc_status *out){
    memset(out,0,sizeof(*out));if(!s){out->state=XZ_OC_ERROR;return;}
    out->state=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==3){out->lag=(int)lround(s->offset);out->correlation=s->score;}
    out->blocks=__atomic_load_n(&s->blocks,__ATOMIC_RELAXED);out->misses=__atomic_load_n(&s->misses,__ATOMIC_RELAXED);
}
void xz_oc_stream_wave(struct xz_oc_stream *s,unsigned role,unsigned char *bins,size_t count,float *progress){
    if(!s||role>=3||!bins||!count)return;uint32_t n=s->assets.wave_count[role];
    memset(bins,0,count);if(!n)return;
    for(size_t i=0;i<count;i++){
        size_t first=(uint64_t)i*n/count,last=(uint64_t)(i+1)*n/count;if(last<=first)last=first+1;
        for(size_t j=first;j<last&&j<n;j++)if(s->assets.wave[role][j]>bins[i])bins[i]=s->assets.wave[role][j];
    }
    if(progress){double at=(double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)*XZ_OC_RATE/s->rate/s->assets.frames;*progress=(float)(at<0?0:at>1?1:at);}
}
