/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WINDOW_KEYS_H
#define XZ_NATIVE_WINDOW_KEYS_H
#include <stdint.h>
#include <stddef.h>

#define XZ_WINDOW_KEY_CAPACITY 1024u
struct xz_window_key_entry {
    uintptr_t surface, hw;
    uint16_t key;
    uint8_t state, enabled;
};
struct xz_window_key_registry {
    struct xz_window_key_entry entries[XZ_WINDOW_KEY_CAPACITY];
    unsigned overflow;
};

static inline unsigned xz_window_key_hash(uintptr_t surface)
{ return (unsigned)((surface >> 3) ^ (surface >> 13)) & (XZ_WINDOW_KEY_CAPACITY-1); }

static inline void xz_window_key_forget_hw(struct xz_window_key_registry *registry,uintptr_t hw)
{
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i++)
        if(registry->entries[i].state==1 && registry->entries[i].hw==hw)
            registry->entries[i].state=2;
}

/* Pure capture of native scalar fields; caller serializes access. */
static inline int xz_window_key_record(struct xz_window_key_registry *registry,
    uintptr_t hw,uintptr_t surface,uint32_t format,uint32_t options,uint32_t rgba)
{
    if(!hw)return 0;
    uint16_t key=(uint16_t)((((rgba&255)>>3)<<11)|((((rgba>>8)&255)>>2)<<5)|(((rgba>>16)&255)>>3));
    if(surface&&format==9){
        unsigned start=xz_window_key_hash(surface);
        for(unsigned n=0;n<XZ_WINDOW_KEY_CAPACITY;n++){
            struct xz_window_key_entry *entry=&registry->entries[(start+n)&(XZ_WINDOW_KEY_CAPACITY-1)];
            if(!entry->state)break;
            if(entry->state==1&&entry->surface==surface&&entry->hw==hw){entry->key=key;entry->enabled=(uint8_t)((options&1)!=0);return 1;}
        }
    }
    xz_window_key_forget_hw(registry,hw);
    if(!surface || format!=9)return 0;
    unsigned first=XZ_WINDOW_KEY_CAPACITY,index=xz_window_key_hash(surface);
    for(unsigned probe=0;probe<XZ_WINDOW_KEY_CAPACITY;probe++) {
        unsigned slot=(index+probe)&(XZ_WINDOW_KEY_CAPACITY-1);
        if(registry->entries[slot].state!=1) {first=slot;break;}
    }
    if(first==XZ_WINDOW_KEY_CAPACITY){registry->overflow=1;return -1;}
    registry->entries[first]=(struct xz_window_key_entry){surface,hw,
        key,
        1,(uint8_t)((options&1)!=0)};
    return 1;
}

/* 1 enabled key; 0 disabled/unregistered; -1 ambiguous or capacity exhausted.
 * A -1 result requires stock rendering, not treating the surface as unkeyed. */
static inline int xz_window_key_lookup(const struct xz_window_key_registry *registry,
    uintptr_t surface,uint16_t *out)
{
    if(registry->overflow)return -1;
    if(!surface)return 0;
    unsigned index=xz_window_key_hash(surface),matches=0;
    const struct xz_window_key_entry *found=NULL;
    for(unsigned probe=0;probe<XZ_WINDOW_KEY_CAPACITY;probe++) {
        const struct xz_window_key_entry *entry=&registry->entries[(index+probe)&(XZ_WINDOW_KEY_CAPACITY-1)];
        if(entry->state==0)break;
        if(entry->state==1&&entry->surface==surface){found=entry;if(++matches>1)return -1;}
    }
    if(!found||!found->enabled)return 0;
    if(out)*out=found->key;
    return 1;
}

static inline int xz_window_key_gr_valid(uintptr_t gr,uint32_t flags)
{
    return gr>=UINT32_C(0x1adb0d8)&&gr<UINT32_C(0x1adf0d8)&&
        (gr-UINT32_C(0x1adb0d8))%64==0&&(flags&UINT32_C(0x40000000));
}

static inline int xz_window_key_lookup_hw(const struct xz_window_key_registry *registry,
    uintptr_t hw,uint16_t *out)
{
    if(registry->overflow)return -1;
    for(unsigned i=0;i<XZ_WINDOW_KEY_CAPACITY;i++)
        if(registry->entries[i].state==1 && registry->entries[i].hw==hw)
            return xz_window_key_lookup(registry,registry->entries[i].surface,out);
    return 0;
}

struct xz_native_window_keys_proof {
    uint32_t version,records,lookups,enabled,ambiguous,overflow,forgotten,errors;
};
extern struct xz_native_window_keys_proof xz_mods_native_window_keys_v1;
int xz_native_window_keys_start(void);
int xz_native_window_key(uintptr_t surface,uint16_t *out);
int xz_native_window_key_for_hw(uintptr_t hw,uint16_t *out);
void xz_native_window_keys_forget(void *gr);
#endif
