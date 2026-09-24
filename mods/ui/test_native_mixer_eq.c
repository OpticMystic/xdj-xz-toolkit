#define XZ_MIXER_EQ_TEST
#include "native_mixer_eq.c"
#include <assert.h>
#include <stdio.h>
static pthread_mutex_t memory_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned char mixer_memory[0x280];
static unsigned fixture_usb_decks=3;
static uint32_t fixture_pc_mode[2];
static uint32_t fixture_source[2]={2,3};
static int commands[32][2],command_count,stock_calls;
static void put(unsigned offset,uint32_t value){memcpy(mixer_memory+offset,&value,4);}
static void stock(void *self){(void)self;stock_calls++;}
int xz_read_memory(uint32_t address,void *out,size_t size){
    pthread_mutex_lock(&memory_lock);int result=0;uint32_t value=0;
    if(address==0x1c8981c&&size==4)value=0x1000;
    else if(address==0x1050&&size==4)value=0x2000;
    else if(address==0x104c&&size==4)value=0x3000;
    else if(address>=0x2000&&address+size<=0x2280){memcpy(out,mixer_memory+address-0x2000,size);goto done;}
    else if(address==0x224e6cc&&size==4)value=fixture_pc_mode[0];
    else if(address==0x224e6d4&&size==4)value=fixture_pc_mode[1];
    else if(address==0x22492e0&&size==4)value=fixture_source[0];
    else if(address==0x2249340&&size==4)value=fixture_source[1];
    else if(address==0xdac3c&&size==8){const unsigned char g[]={0x20,0x30,0x9f,0xe5,0xd8,0x20,0x93,0xe5};memcpy(out,g,8);goto done;}
    else if(address==0xa9374&&size==8){const unsigned char g[]={0x80,0x00,0x80,0xe0,0xac,0x3c,0x07,0xe3};memcpy(out,g,8);goto done;}
    else if(address==0x25c868&&size==8){const unsigned char g[]={0x2a,0x12,0xc0,0xe5,0x30,0x22,0x80,0xe5};memcpy(out,g,8);goto done;}
    else{result=-1;goto done;}
    memcpy(out,&value,4);
done:pthread_mutex_unlock(&memory_lock);return result;
}
int xz_hook_arm(uint32_t address,const unsigned char guard[8],void *replacement,void **original){
    assert(address==0x25d7f4&&guard[0]==0x40&&replacement);*original=(void *)stock;return 0;
}
int xz_oc_worker_schedule(void){return 0;}
void xz_log(const char *message){(void)message;}
int xz_audio_get_status(int deck,struct xz_audio_status *out){
    pthread_mutex_lock(&memory_lock);memset(out,0,sizeof(*out));
    strcpy(out->path,fixture_usb_decks&(1u<<deck)?"/media/usb2/sdb1/Contents/track.mp3":"/mnt/link/track.mp3");
    pthread_mutex_unlock(&memory_lock);return 0;
}
void xz_eq_test_pattern(uint32_t base,int enabled,int pattern){
    assert(base==0x2000);pthread_mutex_lock(&memory_lock);
    assert(command_count<32);commands[command_count][0]=enabled;commands[command_count++][1]=pattern;
    mixer_memory[0x22a]=(unsigned char)enabled;put(0x230,(uint32_t)pattern);put(0x22c,(uint32_t)pattern);
    pthread_mutex_unlock(&memory_lock);
}
static void notified(const struct xz_eq_snapshot *snapshot){(void)snapshot;}
static void wait_status(unsigned status){
    for(unsigned i=0;i<200;i++){if(__atomic_load_n(&xz_mixer_eq_proof_v1.status,__ATOMIC_RELAXED)==status)return;delay();}
    assert(!"Timed out waiting for native EQ state");
}
int main(void){
    put(0,0x474688);put(0x148,0x05000000);
    assert(!xz_native_eq_start(notified));xz_native_eq_enable(1);wait_status(XZ_EQ_ACTIVE);
    put(0x144,0x01060200);receive(mixer_memory);assert(stock_calls==1&&revision[0][2]==1&&samples[0][2]==512);
    pthread_mutex_lock(&memory_lock);fixture_source[0]=fixture_source[1]=4;pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_SOURCE);assert(!capture_mask);
    assert(command_count>=3&&commands[command_count-2][0]==1&&commands[command_count-2][1]==0&&commands[command_count-1][0]==0);
    put(0x144,0x01060000);receive(mixer_memory);assert(revision[0][2]==1&&stock_calls==2);
    pthread_mutex_lock(&memory_lock);fixture_source[0]=2;fixture_source[1]=3;put(0x148,0x04000000);pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_ACTIVE);assert(capture_mask==2);
    put(0x144,0x010603ff);receive(mixer_memory);assert(revision[0][2]==1);
    put(0x144,0x010703ff);receive(mixer_memory);assert(revision[1][2]==1&&samples[1][2]==1023);
    pthread_mutex_lock(&memory_lock);fixture_pc_mode[1]=1;pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_EXTERNAL);assert(!capture_mask);
    pthread_mutex_lock(&memory_lock);fixture_pc_mode[1]=0;pthread_mutex_unlock(&memory_lock);
    wait_status(XZ_EQ_ACTIVE);assert(capture_mask==2);
    xz_native_eq_enable(0);wait_status(XZ_EQ_OFF);assert(!capture_mask);xz_native_eq_stop();
    assert(commands[command_count-2][0]==1&&commands[command_count-2][1]==0&&commands[command_count-1][0]==0);
    puts("PASS native EQ command lifecycle, stock input passthrough, LINK source suspension, per-channel locks and explicit exit");
}
