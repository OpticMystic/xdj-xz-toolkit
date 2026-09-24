#ifndef XZ_MOD_UI_H
#define XZ_MOD_UI_H
#include <stddef.h>
#include <stdint.h>

#define XZ_UI_WIDTH 800
#define XZ_UI_HEIGHT 480
#define XZ_UI_WIDGETS 64
#define XZ_UI_ACTIONS 4

enum xz_ui_page { XZ_UI_STEMS, XZ_UI_XPAD, XZ_UI_SETTINGS, XZ_UI_THEMES, XZ_UI_CONNECTION, XZ_UI_CONTROLS, XZ_UI_PAGE_COUNT };
enum xz_ui_cap {
    XZ_UI_GATE=1u, XZ_UI_SMART=2u, XZ_UI_PREVIEW=4u, XZ_UI_STEM=8u,
    XZ_UI_GROOVE=16u, XZ_UI_SAMPLE=32u, XZ_UI_THEME=64u, XZ_UI_SERVER=128u,
    XZ_UI_KEY=256u, XZ_UI_HYBRID=512u, XZ_UI_HOTCUE=1024u,
    XZ_UI_VJ_CONNECTION=2048u, XZ_UI_VJ_DISCOVERY=4096u, XZ_UI_KEYSYNC=8192u
};
enum xz_ui_action_kind {
    XZ_UI_NONE, XZ_UI_PANEL, XZ_UI_DECK, XZ_UI_CLOSE, XZ_UI_UNAVAILABLE,
    XZ_UI_ENABLE, XZ_UI_LEVEL, XZ_UI_MUTE, XZ_UI_BYPASS, XZ_UI_GROOVE_PAD,
    XZ_UI_SAMPLE_PAD, XZ_UI_STRIP, XZ_UI_HOLD, XZ_UI_OVERDUB, XZ_UI_VOLUME,
    XZ_UI_SET_THEME, XZ_UI_SERVER_AUTO, XZ_UI_SERVER_ADDRESS, XZ_UI_KEY_SHIFT,
    XZ_UI_KEY_SYNC, XZ_UI_HOTCUE_PAD, XZ_UI_CONNECTION_ENABLE, XZ_UI_DISCOVERY,
    XZ_UI_STEM_PAGE, XZ_UI_SHIFT_PAGES, XZ_UI_PAD_FEEDBACK, XZ_UI_SHIFT_KEYSYNC,
    XZ_UI_TAKEOVER_TOGGLE, XZ_UI_TAKEOVER_ASSIGN, XZ_UI_STEMS_OVERLAY, XZ_UI_SPARE_EQ
};
enum xz_ui_phase { XZ_UI_PRESS, XZ_UI_MOVE, XZ_UI_RELEASE };
struct xz_ui_action {
    enum xz_ui_action_kind kind;
    enum xz_ui_phase phase;
    int deck, index;
    float value, secondary;
};
struct xz_ui_deck {
    uint32_t ready;
    const char *track, *status;
    float bpm, levels[3], sample_volume;
    unsigned muted, groove_assigned, groove_loaded, sample_loaded;
    int bypass, groove_active, sample_active, hold, overdub, loop_index;
    float pitch;
    int key_semitones;
    int stem_loading;
    unsigned char groove_stem[8];
    const unsigned char *wave_peaks;
    size_t wave_count;
};
struct xz_ui_connection {
    int ready, enabled, connected, discoverable;
    int can_enable, can_discover, stats_valid;
    const char *device_ip, *protocol, *status;
    unsigned port;
    float frame_hz;
};
struct xz_ui_model {
    struct xz_ui_deck deck[4];
    uint32_t enabled;
    int theme, server_auto;
    const char *server_address;
    unsigned blink;
    struct xz_ui_connection connection;
    int stem_page, shift_pages, pad_feedback, shift_keysync;
    int fb_takeover, takeover_assign, stems_overlay;
    int spare_eq, eq_available; unsigned eq_status, eq_allowed;
    const char *settings_status;
};
struct xz_ui {
    enum xz_ui_page page;
    int deck, down, capture;
    enum xz_ui_action_kind held_kind;
    int held_index, held_deck;
    int touch_start_x, dragged;
    float touch_start_level;
    char notice[96];
};
struct xz_ui_widget {
    int x,y,w,h;
    enum xz_ui_action_kind kind;
    int index;
    uint32_t requires;
    const char *label;
};
void xz_ui_init(struct xz_ui *ui);
/* The model is a runtime snapshot. Rendering and touch never change it. */
size_t xz_ui_layout(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget out[XZ_UI_WIDGETS]);
int xz_ui_render(const struct xz_ui *, const struct xz_ui_model *, uint16_t *pixels, size_t count, size_t stride);
/* Up to XZ_UI_ACTIONS actions. Runtime applies these and supplies the next model. */
size_t xz_ui_touch(struct xz_ui *, const struct xz_ui_model *, int x, int y, int down, struct xz_ui_action out[XZ_UI_ACTIONS]);
/* Call on close, focus loss or device removal to release a held mute/strip/pad. */
size_t xz_ui_cancel(struct xz_ui *, struct xz_ui_action out[XZ_UI_ACTIONS]);
const char *xz_ui_theme_name(int theme);
uint32_t xz_ui_stem_color(int theme,int index);
size_t xz_ui_inline_layout(const struct xz_ui *,int width,int height,struct xz_ui_widget out[XZ_UI_WIDGETS]);
int xz_ui_inline_render(const struct xz_ui *,const struct xz_ui_model *,uint16_t *,size_t count,size_t stride,int width,int height);
size_t xz_ui_inline_touch(struct xz_ui *,const struct xz_ui_model *,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]);
void xz_ui_render_badge(uint16_t *pixels,size_t stride);
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled);
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active);
#endif
