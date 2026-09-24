/* Passive mixer telemetry. The factory volume-test command is deliberately
 * absent from performance builds because it changes the XZ audio routing. */
#define _POSIX_C_SOURCE 200809L
#include "native_mixer_eq.h"
#include "mixer_eq.h"
#include "../runtime.h"
#include "../audio/runtime.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static pthread_t worker;
static int running, requested, available;
static unsigned capture_mask, reported_decks;
static uint32_t samples[2][3], revision[2][3];
static xz_eq_callback notify;
static void (*stock_midi_cc)(void *, int, int, int);

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
           (!strncmp(status.path,"/media/usb1/",12)||!strncmp(status.path,"/media/usb2/",12)))
            *usb_decks|=1u<<deck;
    }
    return 1;
}
static int decode_cc(int status,int control,int value,int *deck,int *stem,float *gain){
    (void)status;if(value<0||value>127)return 0;
    switch(control){
    case 14:*deck=0;*stem=2;break;case 15:*deck=0;*stem=1;break;case 21:*deck=0;*stem=0;break;
    case 81:*deck=1;*stem=2;break;case 92:*deck=1;*stem=1;break;case 82:*deck=1;*stem=0;break;
    default:return 0;
    }
    *gain=value>=64?1.0f:(float)value/64.0f;return 1;
}
static void midi_cc(void *self,int status,int control,int value){
    unsigned mask=__atomic_load_n(&capture_mask,__ATOMIC_ACQUIRE);int deck,stem;float gain;
    if(mask&&(status&0xf0)==0xb0&&decode_cc(status,control,value,&deck,&stem,&gain)&&(mask&(1u<<deck))){
        __atomic_fetch_or(&reported_decks,1u<<deck,__ATOMIC_RELAXED);
        __atomic_store_n(&samples[deck][stem],(uint32_t)value,__ATOMIC_RELAXED);
        __atomic_add_fetch(&revision[deck][stem],1,__ATOMIC_RELEASE);
        __atomic_store_n(&xz_mixer_eq_proof_v1.last_id,(uint32_t)control,__ATOMIC_RELAXED);
        __atomic_store_n(&xz_mixer_eq_proof_v1.last_value,(uint32_t)value,__ATOMIC_RELAXED);
        __atomic_add_fetch(&xz_mixer_eq_proof_v1.reports,1,__ATOMIC_RELAXED);
    }
    stock_midi_cc(self,status,control,value);
}
static void delay(void){struct timespec t={0,20000000};nanosleep(&t,0);}
static void *poll_mixer(void *unused){
    (void)unused;
    while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
        uint32_t mixer=0,switches=0;unsigned usb_decks=0;int valid=device(&mixer,&switches,&usb_decks);
        int enabled=__atomic_load_n(&requested,__ATOMIC_ACQUIRE);unsigned allowed=valid?xz_eq_allowed(switches,usb_decks):0;
        unsigned seen=__atomic_load_n(&reported_decks,__ATOMIC_RELAXED)&allowed;
        unsigned status=!enabled?XZ_EQ_OFF:!valid?XZ_EQ_UNAVAILABLE:!usb_decks?XZ_EQ_SOURCE:
            !allowed?XZ_EQ_EXTERNAL:!seen?XZ_EQ_WAITING:XZ_EQ_ACTIVE;
        __atomic_store_n(&capture_mask,enabled?allowed:0,__ATOMIC_RELEASE);
        struct xz_eq_snapshot snapshot={0};snapshot.status=status;snapshot.allowed=status==XZ_EQ_ACTIVE?allowed:0;
        for(int d=0;d<2;d++)for(int s=0;s<3;s++){
            snapshot.revision[d][s]=__atomic_load_n(&revision[d][s],__ATOMIC_ACQUIRE);
            unsigned raw=__atomic_load_n(&samples[d][s],__ATOMIC_RELAXED);snapshot.gain[d][s]=raw>=64?1.0f:(float)raw/64.0f;
        }
        __atomic_store_n(&xz_mixer_eq_proof_v1.status,status,__ATOMIC_RELAXED);__atomic_store_n(&xz_mixer_eq_proof_v1.allowed,snapshot.allowed,__ATOMIC_RELAXED);
        notify(&snapshot);delay();
    }
    __atomic_store_n(&capture_mask,0,__ATOMIC_RELEASE);return NULL;
}
int xz_native_eq_start(xz_eq_callback callback){
    static const unsigned char guard[8]={0x80,0x00,0x13,0xe3,0xb0,0x10,0x81,0xe3};unsigned char actual[8];
    if(!callback||xz_read_memory(0x2bde78,actual,8)||memcmp(actual,guard,8))return -1;
    if(xz_hook_arm(0x2bde78,guard,(void *)midi_cc,(void **)&stock_midi_cc)){xz_log("Native EQ unavailable: mixer MIDI telemetry hook refused");return -1;}
    notify=callback;__atomic_store_n(&running,1,__ATOMIC_RELEASE);
    if(pthread_create(&worker,NULL,poll_mixer,NULL)){__atomic_store_n(&running,0,__ATOMIC_RELEASE);return -1;}
    available=1;return 0;
}
void xz_native_eq_enable(int enabled){
    int next=available&&enabled;
    int previous=__atomic_exchange_n(&requested,next,__ATOMIC_ACQ_REL);
    if(next&&!previous)__atomic_store_n(&reported_decks,0,__ATOMIC_RELAXED);
}
void xz_native_eq_stop(void){if(available){__atomic_store_n(&running,0,__ATOMIC_RELEASE);pthread_join(worker,NULL);available=0;}}
#ifdef XZ_MIXER_EQ_TEST
void xz_eq_test_midi_cc(int status,int control,int value){midi_cc(NULL,status,control,value);}
#endif
