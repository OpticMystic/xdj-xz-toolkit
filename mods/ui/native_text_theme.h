/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_TEXT_THEME_H
#define XZ_NATIVE_TEXT_THEME_H

#include <stdint.h>
#include <string.h>


typedef uint32_t (*xz_native_text_pixel_fn)(uint32_t coverage, uint32_t color,
                                          uint8_t *destination, uint32_t opaque);
typedef uint32_t (*xz_native_text_rgb_fn)(uint32_t rgb, const void *context);

/* Publish one immutable map per draw generation; NULL selects exact stock calls. */
struct xz_native_text_map {
    const uint16_t *rgb565; /* Exactly 65536 entries. */
    xz_native_text_rgb_fn rgb888; /* R in low byte, G next, B next. */
    const void *context;
};

static inline uint32_t xz_native_text_argb16_keyed(
    xz_native_text_pixel_fn original, const struct xz_native_text_map *map,
    int key_enabled,uint16_t key,
    uint32_t coverage, uint32_t palette_color, uint8_t *destination,
    uint32_t opaque)
{
    if (!map || !map->rgb565)
        return original(coverage, palette_color, destination, opaque);
    /* Verified 1.26 callback never reads destination and writes only on return 1. */
    uint16_t pristine = 0;
    uint32_t result = original(coverage, palette_color,
                               (uint8_t *)&pristine, opaque);
    if (result == 1) {
        uint16_t themed = key_enabled&&pristine==key?pristine:map->rgb565[pristine];
        if(key_enabled&&pristine!=key&&themed==key)themed^=1;
        memcpy(destination, &themed, sizeof(themed));
    }
    return result;
}
static inline uint32_t xz_native_text_argb16(
    xz_native_text_pixel_fn original,const struct xz_native_text_map *map,
    uint32_t coverage,uint32_t color,uint8_t *destination,uint32_t opaque){
    return xz_native_text_argb16_keyed(original,map,0,0,coverage,color,destination,opaque);
}

static inline uint32_t xz_native_text_clut8(
    xz_native_text_pixel_fn original, const struct xz_native_text_map *map,
    uint32_t coverage, uint32_t rgba, uint8_t *destination, uint32_t opaque)
{
    if (map && map->rgb888)
        rgba = (rgba & UINT32_C(0xff000000)) |
               (map->rgb888(rgba & UINT32_C(0x00ffffff), map->context) &
                UINT32_C(0x00ffffff));
    return original(coverage, rgba, destination, opaque);
}

#endif
