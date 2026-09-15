#ifndef XZ_MODS_BRIDGE_H
#define XZ_MODS_BRIDGE_H
#include <stdint.h>
#define XZ_MODS_BRIDGE_V1 1u

struct xz_vj_connection_v1 {
    uint32_t version;
    uint32_t size;
    uint32_t listening;
    uint32_t connected;
    uint32_t discovery;
    uint32_t active;
    uint32_t stats_valid;
    uint32_t frame_hz_milli;
    char ipv4[16];
};

typedef int (*xz_mods_visible_fn_v1)(void);
typedef int (*xz_mods_native_touch_fn_v1)(void);
typedef int (*xz_mods_render_fn_v1)(uint16_t *, uint32_t, uint32_t, uint32_t,
                                   const struct xz_vj_connection_v1 *);

/* Optional mod exports, resolved dynamically. Zero from render means no draw. */
int xz_mods_visible_v1(void);
int xz_mods_native_touch_v1(void);
int xz_mods_render_v1(uint16_t *pixels, uint32_t width, uint32_t height,
                    uint32_t stride_pixels, const struct xz_vj_connection_v1 *connection);
/* Receiver export: calibrated native press/release edges, unchanged VJTE wire. */
void xz_vj_touch_v1(int down, int x, int y);
#endif
