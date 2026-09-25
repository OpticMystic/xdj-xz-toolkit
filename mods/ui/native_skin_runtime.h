#ifndef XZ_NATIVE_SKIN_RUNTIME_H
#define XZ_NATIVE_SKIN_RUNTIME_H
#include <stdint.h>
int xz_native_skin_start(int theme);
void xz_native_skin_request(int theme);
const char *xz_native_skin_status(void);
void xz_native_skin_stop(void);
int xz_native_skin_on_owner_thread(void);
uint32_t xz_mods_native_color_v1(uint32_t rgba);
uint32_t xz_mods_native_surface_color_v1(uint32_t surface,uint32_t rgba);
int xz_mods_native_style_v1(void);
void xz_mods_native_pixels_v1(uint16_t *pixels,uint32_t count);
#endif
