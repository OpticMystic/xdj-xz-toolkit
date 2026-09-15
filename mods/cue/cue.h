#ifndef XZ_CUE_H
#define XZ_CUE_H
#include <stdint.h>
#define XZ_CUE_PADS 8
#define XZ_CUE_DECKS 2
struct xz_cue_api {
 void *context;
 int (*cue_ms)(void *, int, int, int32_t *);
 int (*pause)(void *, int);
 int (*seek_ms)(void *, int, int32_t);
 int (*follow_memory)(void *, int, int32_t);
};
enum xz_cue_error { XZ_CUE_OK, XZ_CUE_MEMORY_FAILED, XZ_CUE_PAUSE_FAILED, XZ_CUE_SEEK_FAILED };
struct xz_cue_pad { int had; int32_t at; };
struct xz_cue_deck { unsigned held; enum xz_cue_error last_error; int latched; int latest; int latest_valid; int32_t latest_at; struct xz_cue_pad pads[8]; };
struct xz_cue { struct xz_cue_api api; int gate; int smart; struct xz_cue_deck deck[2]; };
struct xz_cue_event { int deck; int pad; int operation; int hotcue_mode; int play;
 /* Semantic page order: HOT CUE, BEAT LOOP, SLIP LOOP, BEAT JUMP; -1 unknown. */
 int pad_page; int shift; int mode_button; int sync;
};
void xz_cue_init(struct xz_cue *, struct xz_cue_api);
void xz_cue_settings(struct xz_cue *, int gate, int smart);
/* before returns1 only when PLAY is claimed as a gate latch. Otherwise stock MUST run. */
int xz_cue_before(struct xz_cue *, const struct xz_cue_event *);
/* after runs strictly after stock, including release bookkeeping. */
void xz_cue_after(struct xz_cue *, const struct xz_cue_event *, int stock_result);
#endif
