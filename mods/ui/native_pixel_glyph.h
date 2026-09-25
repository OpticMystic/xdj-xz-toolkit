/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_PIXEL_GLYPH_H
#define XZ_NATIVE_PIXEL_GLYPH_H
#include <stdint.h>
#include <string.h>
#include "pixel_font.h"

/* Host-neutral view; the ARM bridge reads the 28-byte native descriptor fields.
 * Never cast this view onto native memory on a host with 64-bit pointers. */
struct xz_native_text_bitmap {
    uint8_t *data;
    uint32_t capacity;
    int32_t bearing_x, bearing_y;
    uint32_t width, height, stride;
};

struct xz_native_text_glyph_scope {
    int enabled, inside_wstring;
    uintptr_t backend;
    uint32_t packing_exponent;
    uint8_t *supplied_data;
    uint32_t supplied_capacity;
};

/* Call after original FONT_GetCharGlyph. Scope must come from a nested-safe
 * WString hook; supplied pointer/capacity are captured BEFORE the original call.
 * This changes only its freshly populated scratch raster, never the metrics. */
static inline int xz_native_text_pixel_glyph(
    const struct xz_native_text_glyph_scope *scope, uint32_t original_result,
    uint32_t character, uint32_t encoding_word,
    const struct xz_native_text_bitmap *bitmap)
{
    if (!scope || !scope->enabled || !scope->inside_wstring ||
        scope->backend != UINT32_C(0x206fac) || scope->packing_exponent != 1 ||
        original_result != 0 || !bitmap || !bitmap->data ||
        bitmap->data != scope->supplied_data ||
        bitmap->capacity != scope->supplied_capacity ||
        character < 32 || character > 126)
        return 0;
    uint32_t encoding = encoding_word & 255;
    if (encoding == 2) {
        if (bitmap->width != 11 || bitmap->height != 23 || bitmap->stride != 3)
            return 0;
    } else if (encoding == 1 || encoding == 14 || encoding == 16) {
        if (bitmap->width < 5 || bitmap->width > 28 ||
            bitmap->height != 27 || bitmap->stride != 7)
            return 0;
    } else return 0;
    size_t bytes = (size_t)bitmap->stride * bitmap->height;
    if (bytes > bitmap->capacity) return 0;
    unsigned scale = bitmap->width / 5;
    if (scale > bitmap->height / 7) scale = bitmap->height / 7;
    if (!scale) return 0;
    unsigned left = (bitmap->width - 5 * scale) / 2;
    unsigned top = (bitmap->height - 7 * scale) / 2;
    memset(bitmap->data, 0, bytes);
    for (unsigned row = 0; row < 7; row++) {
        uint8_t bits = xz_pixel_font_row(character, row);
        for (unsigned column = 0; column < 5; column++) {
            if (!(bits & (1u << (4 - column)))) continue;
            for (unsigned dy = 0; dy < scale; dy++) {
                uint8_t *out = bitmap->data +
                    (top + row * scale + dy) * bitmap->stride;
                for (unsigned dx = 0; dx < scale; dx++) {
                    unsigned x = left + column * scale + dx;
                    out[x / 4] |= (uint8_t)(3u << (6 - (x % 4) * 2));
                }
            }
        }
    }
    return 1;
}
#endif
