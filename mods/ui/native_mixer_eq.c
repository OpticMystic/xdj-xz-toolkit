#define _POSIX_C_SOURCE 200809L
#include "native_mixer_eq.h"
#include "mixer_eq.h"
#include "../runtime.h"
#include "../audio/overcue.h"
#include "../audio/runtime.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static pthread_t worker;
static int running,requested,available;
static unsigned capture_mask;
static uint32_t samples[2][3],revision[2][3];
static xz_eq_callback notify;
static void (*stock_receive)(void *);
__attribute__((visibility("default"))) struct {
    uint32_t version,status,allowed,reports,last_id,last_value,requests,exits;
} xz_mixer_eq_proof_v1={1,XZ_EQ_UNAVAILABLE,0,0,0,0,0,0};

static int read32(uint32_t address,uint32_t *value){return !xz_read_memory(address,value,4);}
static int device(uint32_t *mixer,uint32_t *switches,unsigned *usb_decks){
    uint32_t manager,vtable;
    if(!read32(0x1c8981c,&manager)||!manager||!read32(manager+0x50,mixer)||!*mixer||
       !read32(*mixer,&vtable)||vtable!=0x474688||!read32(*mixer+0x148,switches))return 0;
    *usb_decks=0;
    for(int deck=0;deck<2;deck++){
        uint32_t pc_mode=1,source=0;struct xz_audio_status status;
        if(!read32(0x224e6cc+(uint32_t)deck*8,&pc_mode)||!read32(0x22492e0+(uint32_t)deck*96,&source))return 0;
        if(!pc_mode&&(source==2||source==3)&&!xz_audio_get_status(deck,&status)&&
           (!strncmp(status.path,"/media/usb1/",12)||!strncmp(status.path,"/media/usb2/",12)))*usb_decks|=1u<<deck;
    }
    return 1;
}
static void request_pattern(uint32_t mixer,int enabled,int pattern){
    uint32_t vtable;if(!read32(mixer,&vtable)||vtable!=0x474688)return;
#ifdef XZ_MIXER_EQ_TEST
    extern void xz_eq_test_pattern(uint32_t,int,int);
    xz_eq_test_pattern(mixer,enabled,pattern);
#else
    ((void (*)(void *,int,int))(uintptr_t)0x25c868)((void *)(uintptr_t)mixer,enabled,pattern);
#endif
}
static void receive(void *self){
    static uint32_t previous;
    uint32_t report,switches;memcpy(&report,(char *)self+0x144,4);memcpy(&switches,(char *)self+0x148,4);
    unsigned mask=__atomic_load_n(&capture_mask,__ATOMIC_ACQUIRE)&xz_eq_allowed(switches,3);
    int deck,stem;float gain;
    if(report!=previous&&mask&&xz_eq_decode(report,&deck,&stem,&gain)&&(mask&(1u<<deck))){
        __atomic_store_n(&samples[deck][stem],report&0xffff,__ATOMIC_RELAXED);
        __atomic_add_fetch(&revision[deck][stem],1,__ATOMIC_RELEASE);
        __atomic_store_n(&xz_mixer_eq_proof_v1.last_id,report>>16,__ATOMIC_RELAXED);
        __atomic_store_n(&xz_mixer_eq_proof_v1.last_value,report&0xffff,__ATOMIC_RELAXED);
        __atomic_add_fetch(&xz_mixer_eq_proof_v1.reports,1,__ATOMIC_RELAXED);
    }
    previous=report;stock_receive(self);
}
static void delay(void){struct timespec t={0,10000000};nanosleep(&t,0);}
static void *poll_mixer(void *unused){
    (void)unused;xz_oc_worker_schedule();uint32_t owned=0;unsigned elapsed=0;int failed=0;
    while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
        uint32_t mixer=0,switches=0,pattern=0;unsigned usb_decks=0;
        int valid=device(&mixer,&switches,&usb_decks),enabled=__atomic_load_n(&requested,__ATOMIC_ACQUIRE);
        unsigned mask=valid&&enabled&&!failed?xz_eq_allowed(switches,usb_decks):0;
        unsigned status=!enabled?XZ_EQ_OFF:!valid||failed?XZ_EQ_UNAVAILABLE:!usb_decks?XZ_EQ_SOURCE:!mask?XZ_EQ_EXTERNAL:XZ_EQ_WAITING;
        if(!enabled)failed=0;
        if(owned&&(!mask||mixer!=owned)){
            __atomic_store_n(&capture_mask,0,__ATOMIC_RELEASE);
            request_pattern(owned,1,0);
            for(unsigned i=0;i<10;i++)delay();
            for(unsigned i=0;i<100;i++){if(read32(owned+0x22c,&pattern)&&pattern==0)break;delay();}
            request_pattern(owned,0,0);owned=0;elapsed=0;
            __atomic_add_fetch(&xz_mixer_eq_proof_v1.exits,1,__ATOMIC_RELAXED);
        }
        if(mask&&!owned){
            unsigned char pending=1;
            if(xz_read_memory(mixer+0x22a,&pending,1)||pending||!read32(mixer+0x230,&pattern)||pattern){status=XZ_EQ_UNAVAILABLE;mask=0;}
            else{request_pattern(mixer,1,5);owned=mixer;elapsed=0;__atomic_add_fetch(&xz_mixer_eq_proof_v1.requests,1,__ATOMIC_RELAXED);}
        }
        if(mask&&owned){
            if(read32(owned+0x22c,&pattern)&&pattern==5){status=XZ_EQ_ACTIVE;__atomic_store_n(&capture_mask,mask,__ATOMIC_RELEASE);}
            else{__atomic_store_n(&capture_mask,0,__ATOMIC_RELEASE);if(++elapsed>100)failed=1;}
        }
        struct xz_eq_snapshot snapshot={0};snapshot.status=status;snapshot.allowed=status==XZ_EQ_ACTIVE?mask:0;
        for(int d=0;d<2;d++)for(int s=0;s<3;s++){
            snapshot.revision[d][s]=__atomic_load_n(&revision[d][s],__ATOMIC_ACQUIRE);
            unsigned raw=__atomic_load_n(&samples[d][s],__ATOMIC_RELAXED);snapshot.gain[d][s]=raw>=512?1.0f:(float)raw/512.0f;
        }
        __atomic_store_n(&xz_mixer_eq_proof_v1.status,status,__ATOMIC_RELAXED);
        __atomic_store_n(&xz_mixer_eq_proof_v1.allowed,snapshot.allowed,__ATOMIC_RELAXED);
        notify(&snapshot);delay();
    }
    __atomic_store_n(&capture_mask,0,__ATOMIC_RELEASE);
    if(owned){uint32_t pattern=5;request_pattern(owned,1,0);for(unsigned i=0;i<10;i++)delay();for(unsigned i=0;i<100;i++){if(read32(owned+0x22c,&pattern)&&pattern==0)break;delay();}request_pattern(owned,0,0);}
    return NULL;
}
int xz_native_eq_start(xz_eq_callback callback){
    static const unsigned char receiver_guard[8]={0x40,0x32,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    static const unsigned char command_guard[8]={0x2a,0x12,0xc0,0xe5,0x30,0x22,0x80,0xe5};
    unsigned char actual[8];
    if(!callback||xz_read_memory(0x25c868,actual,8)||memcmp(actual,command_guard,8))return -1;
    static const unsigned char pc_guard[8]={0x20,0x30,0x9f,0xe5,0xd8,0x20,0x93,0xe5};
    if(xz_read_memory(0xdac3c,actual,8)||memcmp(actual,pc_guard,8))return -1;
    static const unsigned char source_guard[8]={0x80,0x00,0x80,0xe0,0xac,0x3c,0x07,0xe3};
    if(xz_read_memory(0xa9374,actual,8)||memcmp(actual,source_guard,8))return -1;
    if(xz_hook_arm(0x25d7f4,receiver_guard,(void *)receive,(void **)&stock_receive)){xz_log("Native EQ unavailable: mixer receive hook refused");return -1;}
    notify=callback;__atomic_store_n(&running,1,__ATOMIC_RELEASE);
    if(pthread_create(&worker,NULL,poll_mixer,NULL)){__atomic_store_n(&running,0,__ATOMIC_RELEASE);return -1;}
    available=1;return 0;
}
void xz_native_eq_enable(int enabled){__atomic_store_n(&requested,available&&enabled,__ATOMIC_RELEASE);}
void xz_native_eq_stop(void){if(available){__atomic_store_n(&running,0,__ATOMIC_RELEASE);pthread_join(worker,NULL);available=0;}}
