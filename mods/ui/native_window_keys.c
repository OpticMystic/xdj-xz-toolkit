/* SPDX-License-Identifier: MIT */
#include "native_window_keys.h"
#include "../runtime.h"
#include <pthread.h>
#include <string.h>
#include <dlfcn.h>

static pthread_mutex_t keys_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_window_key_registry keys;
static int start_state;
static uint32_t (*stock_color)(void *,unsigned,uint32_t);
static uint32_t (*stock_option)(void *,unsigned,uint32_t);
static void (*style_surface)(uint32_t);
__attribute__((visibility("default"))) struct xz_native_window_keys_proof
    xz_mods_native_window_keys_v1={.version=1};
#define KEY_INC(field) __atomic_fetch_add(&xz_mods_native_window_keys_v1.field,1,__ATOMIC_RELAXED)

static uint32_t word_at(const void *pointer,size_t offset)
{ uint32_t word;memcpy(&word,(const unsigned char *)pointer+offset,4);return word; }

static void capture(void *hw)
{
    /* Called immediately after verified native setter, whose input is live HW. */
    uint32_t format=word_at(hw,0x18),options=word_at(hw,4),rgba=word_at(hw,0x24);
    uintptr_t surface=0;
    if(format==9){
        uint32_t core=word_at(hw,0x98);
        if(core)surface=word_at((const void *)(uintptr_t)core,0x1c);
    }
    pthread_mutex_lock(&keys_mutex);
    int result=xz_window_key_record(&keys,(uintptr_t)hw,surface,format,options,rgba);
    pthread_mutex_unlock(&keys_mutex);
    if(surface&&style_surface)style_surface((uint32_t)surface);
    KEY_INC(records);if(result<0)KEY_INC(overflow);
}
static uint32_t color_hook(void *hw,unsigned role,uint32_t color)
{
    uint32_t result=stock_color(hw,role,color);
    capture(hw);return result;
}
static uint32_t option_hook(void *hw,unsigned enable,uint32_t bits)
{
    uint32_t result=stock_option(hw,enable,bits);
    capture(hw);return result;
}
int xz_native_window_key(uintptr_t surface,uint16_t *out)
{
    pthread_mutex_lock(&keys_mutex);
    int result=xz_window_key_lookup(&keys,surface,out);
    pthread_mutex_unlock(&keys_mutex);
    KEY_INC(lookups);if(result==1)KEY_INC(enabled);if(result<0)KEY_INC(ambiguous);
    return result;
}
int xz_native_window_key_for_hw(uintptr_t hw,uint16_t *out)
{
    pthread_mutex_lock(&keys_mutex);
    int result=xz_window_key_lookup_hw(&keys,hw,out);
    pthread_mutex_unlock(&keys_mutex);
    KEY_INC(lookups);if(result==1)KEY_INC(enabled);if(result<0)KEY_INC(ambiguous);
    return result;
}
void xz_native_window_keys_forget(void *gr)
{
    uintptr_t address=(uintptr_t)gr;
    if(address<UINT32_C(0x1adb0d8)||address>=UINT32_C(0x1adf0d8)||
       (address-UINT32_C(0x1adb0d8))%64)return;
    if(!xz_window_key_gr_valid(address,word_at(gr,0x18)))return;
    uintptr_t hw=word_at(gr,0x1c);
    if(hw<UINT32_C(0x1afaef0)||hw>=UINT32_C(0x1b04f90)||
       (hw-UINT32_C(0x1afaef0))%0xa0)return;
    pthread_mutex_lock(&keys_mutex);xz_window_key_forget_hw(&keys,hw);pthread_mutex_unlock(&keys_mutex);
    KEY_INC(forgotten);
}
int xz_native_window_keys_start(void)
{
    /* Constructor/loader-thread API; hooks remain valid for runtime lifetime. */
    if(start_state)return start_state==1?0:-1;
    start_state=-1;
    style_surface=(void(*)(uint32_t))dlsym(RTLD_DEFAULT,"xz_vj_skin_surface_v1");
    if(!style_surface){KEY_INC(errors);return -1;}
    static const unsigned char color_guard[8]={0x10,0x40,0x2d,0xe9,0x02,0x00,0x51,0xe3};
    static const unsigned char option_guard[8]={0x70,0x40,0x2d,0xe9,0x01,0x60,0xa0,0xe1};
    if(xz_hook_arm(0x15cca8,color_guard,(void *)color_hook,(void **)&stock_color)||
       xz_hook_arm(0x15c92c,option_guard,(void *)option_hook,(void **)&stock_option)){
        pthread_mutex_lock(&keys_mutex);keys.overflow=1;pthread_mutex_unlock(&keys_mutex);
        KEY_INC(errors);return -1;
    }
    start_state=1;return 0;
}
