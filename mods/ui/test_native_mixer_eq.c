#define XZ_MIXER_EQ_TEST
#include "native_mixer_eq.c"
#include <assert.h>
#include <stdio.h>
static pthread_mutex_t memory_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned char mixer_memory[0x280];
static unsigned fixture_usb_decks=3;
static uint32_t fixture_pc_mode[2];
static uint32_t fixture_source[2]={2,3};
static int stock_calls;
static void put(unsigned offset,uint32_t value){memcpy(mixer_memory+offset,&value,4);}
int xz_read_memory(uint32_t address,void *out,size_t size){
    pthread_mutex_lock(&memory_lock);int result=0;uint32_t value=0;
    if(address==0x1c8981c&&size==4)value=0x1000;
    else if(address==0x1050&&size==4)value=0x2000;
    else if(address>=0x2000&&address+size<=0x2280){memcpy(out,mixer_memory+address-0x2000,size);goto done;}
    else if(address==0x224e6cc&&size==4)value=fixture_pc_mode[0];
    else if(address==0x224e6d4&&size==4)value=fixture_pc_mode[1];
    else if(address==0x22492e0&&size==4)value=fixture_source[0];
    else if(address==0x2249340&&size==4)value=fixture_source[1];
    else if(address==0x2bde78&&size==8){const unsigned char g[]={0x80,0x00,0x13,0xe3,0xb0,0x10,0x81,0xe3};memcpy(out,g,8);goto done;}
    else{result=-1;goto done;}
    memcpy(out,&value,4);
done:pthread_mutex_unlock(&memory_lock);return result;
}
static void stock(void *self,int status,int control,int value){(void)self;(void)status;(void)control;(void)value;stock_calls++;}
int xz_hook_arm(uint32_t address,const unsigned char guard[8],void *replacement,void **original){
    assert(address==0x2bde78&&guard[0]==0x80&&replacement);*original=(void *)stock;return 0;
}
int xz_oc_worker_schedule(void){return 0;}
void xz_log(const char *message){(void)message;}
int xz_audio_get_status(int deck,struct xz_audio_status *out){
    pthread_mutex_lock(&memory_lock);memset(out,0,sizeof(*out));
    strcpy(out->path,fixture_usb_decks&(1u<<deck)?"/media/usb2/sdb1/Contents/track.mp3":"/mnt/link/track.mp3");
    pthread_mutex_unlock(&memory_lock);return 0;
}
static void notified(const struct xz_eq_snapshot *snapshot){(void)snapshot;}
static void wait_status(unsigned status){
    for(unsigned i=0;i<200;i++){if(__atomic_load_n(&xz_mixer_eq_proof_v1.status,__ATOMIC_RELAXED)==status)return;delay();}
    fprintf(stderr,"waiting for status %u, observed %u\n",status,__atomic_load_n(&xz_mixer_eq_proof_v1.status,__ATOMIC_RELAXED));
    assert(!"Timed out waiting for native EQ state");
}
int main(void){
    put(0,0x474688);put(0x148,0x05000000);
    assert(!xz_native_eq_start(notified));xz_native_eq_enable(1);wait_status(XZ_EQ_WAITING);
    xz_eq_test_midi_cc(0xb4,14,0);wait_status(XZ_EQ_ACTIVE);assert(stock_calls==1&&revision[0][2]==1&&samples[0][2]==0);
    xz_eq_test_midi_cc(0xb4,15,64);assert(revision[0][1]==1&&samples[0][1]==64);
    xz_eq_test_midi_cc(0xb4,82,63);assert(revision[1][0]==1&&samples[1][0]==63);
    xz_eq_test_midi_cc(0xb4,14,128);assert(revision[0][2]==1);
    pthread_mutex_lock(&memory_lock);fixture_source[0]=fixture_source[1]=4;pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_SOURCE);assert(!capture_mask);
    xz_eq_test_midi_cc(0xb4,14,32);assert(revision[0][2]==1&&stock_calls==5);
    pthread_mutex_lock(&memory_lock);fixture_source[0]=2;fixture_source[1]=3;put(0x148,0x04000000);pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_ACTIVE);assert(capture_mask==2);
    xz_eq_test_midi_cc(0xb4,14,20);assert(revision[0][2]==1);
    xz_eq_test_midi_cc(0xb4,81,20);assert(revision[1][2]==1&&samples[1][2]==20);
    pthread_mutex_lock(&memory_lock);fixture_pc_mode[1]=1;pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_EXTERNAL);assert(!capture_mask);
    xz_native_eq_enable(0);wait_status(XZ_EQ_OFF);assert(!capture_mask);xz_native_eq_stop();
    puts("PASS passive mixer MIDI telemetry, source suspension, channel mapping and stock passthrough");
}
