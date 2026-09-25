/* SPDX-License-Identifier: MIT */
#include "native_window_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Native window key tests require active assertions"
#endif
static struct xz_window_key_registry registry;

int main(void)
{
    uint16_t key=0x1234;
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==0&&key==0x1234);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,9,0xff000000)==1);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==0);
    assert(xz_window_key_lookup_hw(&registry,0x1afaef0,&key)==1&&key==0);
    assert(xz_window_key_lookup_hw(&registry,0,&key)==0);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,9,0xffff00ff)==1);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==0xf81f);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,8,0xffff00ff)==1);
    key=0x1234;assert(xz_window_key_lookup(&registry,0x40001000,&key)==0&&key==0x1234);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,1,0x003478cc)==1);
    uint16_t expected=(uint16_t)((0xcc>>3)<<11|(0x78>>2)<<5|(0x34>>3));
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==expected);
    /* Alpha does not change the RGB565 key. */
    for(unsigned a=0;a<256;a++){
        assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,1,(a<<24)|0x3478cc)==1);
        assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==expected);
    }
    /* A different HW owner sharing one interface is ambiguous even for same key. */
    assert(xz_window_key_record(&registry,0x1afaf90,0x40001000,9,1,0x3478cc)==1);
    key=0xdead;assert(xz_window_key_lookup(&registry,0x40001000,&key)==-1&&key==0xdead);
    assert(xz_window_key_lookup_hw(&registry,0x1afaef0,&key)==-1&&key==0xdead);
    xz_window_key_forget_hw(&registry,0x1afaef0);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==expected);
    xz_window_key_forget_hw(&registry,0x1afaf90);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==0);
    /* Address reuse after explicit destruction and HW relocation have no stale key. */
    assert(xz_window_key_record(&registry,0x1afaef0,0x40001000,9,1,0xff)==1);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==1&&key==0xf800);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40002000,9,1,0xff00)==1);
    assert(xz_window_key_lookup(&registry,0x40001000,&key)==0);
    assert(xz_window_key_lookup(&registry,0x40002000,&key)==1&&key==0x7e0);
    assert(xz_window_key_record(&registry,0x1afaef0,0x40002000,3,1,0xff00)==0);
    assert(xz_window_key_lookup(&registry,0x40002000,&key)==0);
    assert(xz_window_key_record(&registry,0,0x40002000,9,1,0)==0);

    memset(&registry,0,sizeof(registry));
    /* Exercise colliding hashes, tombstones, full occupancy, and failed insertion. */
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i++)
        assert(xz_window_key_record(&registry,0x200000+i*0xa0,0x40000000+i*0x1000,9,1,i&255)==1);
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i++){
        assert(xz_window_key_lookup(&registry,0x40000000+i*0x1000,&key)==1);
        assert(key==((i&255)>>3)<<11);
    }
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i+=2)xz_window_key_forget_hw(&registry,0x200000+i*0xa0);
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i++)
        assert(xz_window_key_lookup(&registry,0x40000000+i*0x1000,&key)==(i%2?1:0));
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i+=2)
        assert(xz_window_key_record(&registry,0x200000+i*0xa0,0x40000000+i*0x1000,9,1,0xff0000)==1);
    assert(xz_window_key_record(&registry,0x999999,0x777777,9,1,0)==-1);
    key=0xabcd;assert(xz_window_key_lookup(&registry,0x40000000,&key)==-1&&key==0xabcd);
    assert(xz_window_key_lookup(&registry,0x888888,&key)==-1);
    assert(xz_window_key_lookup_hw(&registry,0x200000,&key)==-1);

    assert(xz_window_key_gr_valid(0x1adb0d8,0x40000000));
    assert(xz_window_key_gr_valid(0x1adf098,0x40000001));
    assert(!xz_window_key_gr_valid(0x1adf0d8,0x40000000));
    assert(!xz_window_key_gr_valid(0x1adb0d9,0x40000000));
    assert(!xz_window_key_gr_valid(0x1adb0d8,0));
    puts("native window keys: black/magenta, toggles, reuse, collision, capacity, and GR guards PASS");
    return 0;
}
