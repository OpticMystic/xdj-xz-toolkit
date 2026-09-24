#include "native_mixer_eq.h"
#include "../audio/runtime.h"
#include <assert.h>
#include <stdio.h>
static int calls;
static void sample(const struct xz_eq_snapshot *value){(void)value;calls++;}
void xz_log(const char *message){(void)message;}
int xz_read_memory(uint32_t address,void *out,size_t size){(void)address;(void)out;(void)size;return -1;}
int xz_hook_arm(uint32_t address,const unsigned char guard[8],void *replacement,void **original){(void)address;(void)guard;(void)replacement;(void)original;return -1;}
int xz_audio_get_status(int deck,struct xz_audio_status *out){(void)deck;(void)out;return -1;}
int main(void){
    assert(xz_native_eq_start(sample)<0);
    xz_native_eq_enable(1);xz_native_eq_enable(0);xz_native_eq_stop();
    assert(!calls);
    puts("PASS native EQ stays unavailable when guarded normal mixer telemetry is absent");
}
