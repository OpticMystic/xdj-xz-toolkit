#include "stem_pads.h"
int xz_stem_control_event(struct xz_stem_pads *s, const struct xz_cue_event *e, int active,
        int selected_page, int shift_pages, int *toggle) {
    *toggle = -1;
    if (e->deck < 0 || e->deck > 1) return 0;
    int action = e->pad;
    int is_page = e->mode_button >= 0 && e->mode_button < 4;
    if (is_page) action = e->mode_button;
    if (action < 0 || action > 3) return 0;
    unsigned bit = 1u << (action + (is_page ? 8 : 0));
    unsigned *down = &s->down[e->deck], *owned = &s->owned[e->deck];
    int consumed = !!(*owned & bit);
    if (e->operation == 2 || e->operation == 3) {
        *down &= ~bit; *owned &= ~bit;
        return consumed;
    }
    if (e->operation != 0) return consumed;
    if (*down & bit) return consumed;
    *down |= bit;
    if (!active || (is_page ? !shift_pages || !e->shift : e->pad_page != selected_page || e->shift)) return 0;
    *owned |= bit;
    *toggle = action;
    if (action < 3) s->muted[e->deck] ^= 1u << action;
    return 1;
}
int xz_stem_pad_event(struct xz_stem_pads *s, const struct xz_cue_event *e, int active, int *toggle) {
    struct xz_cue_event legacy = *e;
    legacy.mode_button = -1; legacy.pad_page = e->hotcue_mode ? 0 : -1; legacy.shift = 0;
    return xz_stem_control_event(s,&legacy,active,0,0,toggle);
}
