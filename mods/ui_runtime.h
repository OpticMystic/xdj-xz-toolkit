#ifndef XZ_UI_RUNTIME_H
#define XZ_UI_RUNTIME_H
struct xz_cue_event;
int xz_ui_runtime_pad_color(int deck,int pad,unsigned *rgb,int *enabled);
void xz_ui_runtime_pad_native_page(int deck,int page);
int xz_ui_runtime_pad(const struct xz_cue_event *event,unsigned *trace_flags);
int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled);
void xz_ui_runtime_stop(void);
void xz_ui_runtime_on_source_key(int source);
#endif
