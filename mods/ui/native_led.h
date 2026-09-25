#ifndef XZ_NATIVE_LED_H
#define XZ_NATIVE_LED_H
#include <stddef.h>
#include <stdint.h>

#define XZ_LED_RECORD_BYTES 0x2cu
#define XZ_LED_MAX_RECORDS 256u
typedef int (*xz_led_color_fn)(int deck,int pad,uint32_t *rgb,int *enabled);
/* Physical indicators need full-intensity colors regardless of display theme. */
static inline uint32_t xz_stem_hardware_color(int stem){
 static const uint32_t colors[4]={0xff3b30,0x2997ff,0x3bff6b,0xffffff};
 return colors[stem>=0&&stem<4?stem:3];
}

/* Existing records only. rgb is 0xRRGGBB; enabled means bright, else dim. */
size_t xz_native_led_apply(void *records,size_t bytes,unsigned count,
                           unsigned channel,xz_led_color_fn color);
int xz_native_led_start(void);
void xz_native_led_stop(void);
#endif
