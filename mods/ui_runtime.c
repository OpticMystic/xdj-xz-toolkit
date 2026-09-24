#define _GNU_SOURCE
#include "ui_runtime.h"
#include "runtime.h"
#include "audio/runtime.h"
#include "key/runtime.h"
#include "ui/native_touch.h"
#include "ui/stem_pads.h"
#include "ui/mixer_eq.h"
#include "ui/native_mixer_eq.h"
#include "ui/native_led.h"
#include "ui/native_wave_runtime.h"
#include "ui/wave_viewport.h"
#include "settings.h"
#include "../vendor/tools/xz_runtime/mods_bridge.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_ui ui;
static struct xz_ui inline_ui;
static struct xz_ui_model model;
static struct xz_native_touch touch;
static struct xz_audio_status audio_status[2];
static xz_stock_touch stock_touch;
static void (*forward_touch)(int, int, int);
static int started, visible, audio_available, key_available, forwarded_down;
static int fb_takeover_active;
static int network_enabled, network_connected;
static char device_ip[16];
static struct xz_stem_pads pads;
static int deck_page[2] = {-1,-1};
static unsigned sync_down, sync_owned;
static pthread_cond_t settings_changed = PTHREAD_COND_INITIALIZER;
static pthread_t settings_thread;
static int settings_running, settings_pending;
static int requested_stems;
static struct xz_eq_pickup eq_pickup[2][3];
static uint32_t eq_revision[2][3];
static int native_focus=-1;
static uint32_t followed_generation;
static int inline_requested, inline_contact, inline_wave_contact, inline_cancelled;
static uint32_t displayed_generation[2], reported_generation[2];
static unsigned char overview[2][192];
struct prepared_proof_deck { uint32_t state,rate,frames,mixed,missed; int32_t lag; float correlation; };
struct prepared_proof { uint32_t version,sequence; struct prepared_proof_deck deck[2]; };
__attribute__((visibility("default"))) struct prepared_proof xz_prepared_proof_v1={1,0,{{0}}};
static char settings_usb[1024];
static struct stat settings_volume;

static int (*stock_uikey_link)(void *);
static int (*stock_uikey_rekordbox)(void *);

static int uikey_link_hook(void *arg0) {
    xz_ui_runtime_on_source_key(0);
    return stock_uikey_link ? stock_uikey_link(arg0) : 0;
}

static int uikey_rekordbox_hook(void *arg0) {
    xz_ui_runtime_on_source_key(1);
    return stock_uikey_rekordbox ? stock_uikey_rekordbox(arg0) : 0;
}

static struct xz_settings settings_snapshot(void) {
    return (struct xz_settings){requested_stems,
        !!(model.enabled & XZ_UI_GATE), !!(model.enabled & XZ_UI_SMART), model.theme,
        model.stem_page, model.shift_pages, model.pad_feedback, model.shift_keysync,
        model.fb_takeover, model.takeover_assign, model.spare_eq, model.stem_bank};
}
static void settings_queue(void) {
    if (!settings_running) { model.settings_status = "SETTINGS NOT SAVED: START FROM USB"; return; }
    model.settings_status = "SAVING SETTINGS TO USB";
    settings_pending = 1; pthread_cond_signal(&settings_changed);
}
static void *settings_writer(void *unused) {
    (void)unused;
    pthread_mutex_lock(&ui_mutex);
    while (settings_running || settings_pending) {
        while (settings_running && !settings_pending) pthread_cond_wait(&settings_changed,&ui_mutex);
        if (!settings_running && !settings_pending) break;
        struct xz_settings saved = settings_snapshot();
        settings_pending = 0;
        pthread_mutex_unlock(&ui_mutex);
        struct stat current;
        int same_volume = !stat(settings_usb,&current) && current.st_dev == settings_volume.st_dev && current.st_ino == settings_volume.st_ino;
        int rc = same_volume ? xz_settings_save(settings_usb,&saved) : -1;
        pthread_mutex_lock(&ui_mutex);
        if (!settings_pending) model.settings_status = rc ? "SETTINGS NOT SAVED: CHECK USB" : "SETTINGS SAVED TO USB";
    }
    pthread_mutex_unlock(&ui_mutex); return NULL;
}

static void levels(int deck) {
    if (deck < 0 || deck > 1) return;
    struct xz_ui_deck *d = &model.deck[deck];
    struct xz_stem_levels value = {1, 1, 1};
    if (!d->bypass) {
        value.drums = d->muted & 1 ? 0 : d->levels[0];
        value.harmonics = d->muted & 2 ? 0 : d->levels[1];
        value.vocals = d->muted & 4 ? 0 : d->levels[2];
    }
    xz_audio_set_levels(deck, value);
}

static void mixer_eq_update(const struct xz_eq_snapshot *snapshot) {
    pthread_mutex_lock(&ui_mutex);
    model.eq_status=snapshot->status;model.eq_allowed=snapshot->allowed;
    for(int deck=0;deck<2;deck++)for(int stem=0;stem<3;stem++){
        if(!requested_stems||!(snapshot->allowed&(1u<<deck))){
            memset(&eq_pickup[deck][stem],0,sizeof(eq_pickup[deck][stem]));eq_revision[deck][stem]=snapshot->revision[deck][stem];continue;
        }
        if(eq_revision[deck][stem]==snapshot->revision[deck][stem])continue;
        eq_revision[deck][stem]=snapshot->revision[deck][stem];
        if(xz_eq_pickup(&eq_pickup[deck][stem],snapshot->gain[deck][stem],model.deck[deck].levels[stem])){
            model.deck[deck].levels[stem]=snapshot->gain[deck][stem];levels(deck);
        }
    }
    pthread_mutex_unlock(&ui_mutex);
}

static void apply(void *context, const struct xz_ui_action *actions, size_t count) {
    (void)context;
    for (size_t i = 0; i < count; i++) {
        const struct xz_ui_action *a = &actions[i];
        switch (a->kind) {
        case XZ_UI_STEMS_OVERLAY:
            model.stems_overlay=!model.stems_overlay;
            __atomic_store_n(&inline_requested,requested_stems && model.stems_overlay && !touch.visible,__ATOMIC_RELEASE);
            break;
        case XZ_UI_CLOSE:
            xz_native_touch_visible(&touch, 0);
            __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
            break;
        case XZ_UI_DECK:
            if (a->index >= 0 && a->index < 2) { ui.deck = a->index; inline_ui.deck = a->index; }
            break;
        case XZ_UI_ENABLE:
            if (a->index == XZ_UI_GATE || a->index == XZ_UI_SMART ||
                (a->index == XZ_UI_STEM && audio_available)) {
                uint32_t next = a->value != 0 ? model.enabled | (uint32_t)a->index : model.enabled & ~(uint32_t)a->index;
                if (a->index == XZ_UI_STEM) {
                    requested_stems = a->value != 0;
                    xz_native_eq_enable(model.spare_eq&&requested_stems);
                    xz_audio_set_enabled(a->value != 0); model.enabled = next;
                    if (a->value != 0) { levels(0); levels(1); }
                }
                else if (xz_runtime_set_cues(!!(next & XZ_UI_GATE), !!(next & XZ_UI_SMART)) == 0) model.enabled = next;
            }
            break;
        case XZ_UI_STEM_BANK:
            if(a->index>=0&&a->index<=1){model.stem_bank=a->index;pads.bank=a->index;}break;
        case XZ_UI_SPARE_EQ:
            model.spare_eq=a->value!=0;memset(eq_pickup,0,sizeof(eq_pickup));xz_native_eq_enable(model.spare_eq&&requested_stems);break;
        case XZ_UI_LEVEL:
            if (a->deck >= 0 && a->deck < 2 && a->index >= 0 && a->index < 3) {
                memset(&eq_pickup[a->deck][a->index],0,sizeof(eq_pickup[a->deck][a->index]));
                model.deck[a->deck].levels[a->index] = a->value; levels(a->deck);
            }
            break;
        case XZ_UI_MUTE:
            if (a->deck >= 0 && a->deck < 2 && a->index >= 0 && a->index < 3) {
                if (a->phase != XZ_UI_PRESS) break;
                pads.muted[a->deck] ^= 1u << a->index;
                model.deck[a->deck].muted = pads.muted[a->deck];
                levels(a->deck);
            }
            break;
        case XZ_UI_BYPASS:
            if (a->deck >= 0 && a->deck < 2) { model.deck[a->deck].bypass = a->value != 0; levels(a->deck); }
            break;
        case XZ_UI_SET_THEME:
            if (a->index >= 0 && a->index <= 6) model.theme = a->index;
            break;
        case XZ_UI_STEM_PAGE:
            if (a->index >= 0 && a->index < 4) model.stem_page = a->index;
            break;
        case XZ_UI_SHIFT_PAGES: model.shift_pages = a->value != 0; break;
        case XZ_UI_PAD_FEEDBACK: model.pad_feedback = a->value != 0; break;
        case XZ_UI_SHIFT_KEYSYNC: model.shift_keysync = a->value != 0; break;
        case XZ_UI_KEY_SHIFT:
            if (key_available && a->deck >= 0 && a->deck < 2) {
                int current;
                if (xz_key_get_desired_semitones(a->deck, &current) == 0) {
                    int target = a->index == 0 ? 0 : current + a->index;
                    if (xz_key_set_desired_semitones(a->deck, target) != 0)
                        snprintf(ui.notice, sizeof(ui.notice), "KEY RANGE IS -12 TO +12 SEMITONES");
                }
            }
            break;
        case XZ_UI_TAKEOVER_TOGGLE:
            model.fb_takeover = !model.fb_takeover;
            __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
            snprintf(ui.notice, sizeof(ui.notice), "VJ.TOOLS VIEW %s", model.fb_takeover ? "ENABLED" : "DISABLED");
            break;
        case XZ_UI_TAKEOVER_ASSIGN:
            if (a->index >= 0 && a->index <= 2) {
                model.takeover_assign = a->index;
                snprintf(ui.notice, sizeof(ui.notice), "TAKEOVER KEY: %s",
                    model.takeover_assign == 0 ? "LINK" : (model.takeover_assign == 1 ? "REKORDBOX" : "ONSCREEN"));
            }
            break;
        default:
            break; /* Unconnected capabilities remain disabled by the model. */
        }
        if (a->kind == XZ_UI_ENABLE || a->kind == XZ_UI_SET_THEME ||
            a->kind == XZ_UI_STEM_PAGE || a->kind == XZ_UI_SHIFT_PAGES ||
            a->kind == XZ_UI_PAD_FEEDBACK || a->kind == XZ_UI_SHIFT_KEYSYNC ||
            a->kind == XZ_UI_TAKEOVER_TOGGLE || a->kind == XZ_UI_TAKEOVER_ASSIGN) settings_queue();
    }
}

static void refresh(void) {
    __atomic_store_n(&inline_requested, requested_stems && model.stems_overlay && !touch.visible, __ATOMIC_RELEASE);
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
    for (int deck = 0; deck < 2; deck++) {
        model.deck[deck].ready = XZ_UI_GATE | XZ_UI_SMART | XZ_UI_THEME;
        if (key_available) {
            model.deck[deck].ready |= XZ_UI_KEY;
            xz_key_get_desired_semitones(deck, &model.deck[deck].key_semitones);
        }
        if (audio_available) {
            model.deck[deck].ready |= XZ_UI_STEM;
            if (xz_audio_get_status(deck, &audio_status[deck]) == 0) {
                struct xz_audio_status *s = &audio_status[deck];
                if (displayed_generation[deck] != s->generation) {
                    displayed_generation[deck] = s->generation;
                    memset(eq_pickup[deck],0,sizeof(eq_pickup[deck]));
                    struct xz_ui_action cancelled[XZ_UI_ACTIONS];
                    if (inline_ui.deck == deck) { xz_ui_cancel(&inline_ui,cancelled); inline_cancelled = inline_contact; }
                    if (ui.deck == deck) { xz_ui_cancel(&ui,cancelled); if (touch.capture) touch.opening_contact = 1; }
                    pads.muted[deck] = model.deck[deck].muted = 0;
                    model.deck[deck].levels[0] = model.deck[deck].levels[1] = model.deck[deck].levels[2] = 1;
                    model.deck[deck].bypass = 0;
                }
                xz_prepared_proof_v1.deck[deck] = (struct prepared_proof_deck){s->state,s->reader_rate,s->reader_frames,s->mixed_blocks,s->skipped_blocks,s->alignment_frames,s->alignment_correlation};
                if (s->prepared && s->state == XZ_AUDIO_EXPERIMENTAL_READY && reported_generation[deck] != s->generation) {
                    char line[160];snprintf(line,sizeof(line),"PREPARED_STEMS_READY: deck=%d rate=%u frames=%u lag=%d correlation=%.6f",deck+1,s->reader_rate,s->reader_frames,s->alignment_frames,s->alignment_correlation);
                    xz_log(line);reported_generation[deck] = s->generation;
                }
                const char *path = audio_status[deck].path;
                const char *name = strrchr(path, '/');
                model.deck[deck].track = path[0] ? (name ? name + 1 : path) : "NO TRACK CAPTURED";
                model.deck[deck].status = xz_audio_state_name(audio_status[deck].state);
                model.deck[deck].stem_loading = s->state==XZ_AUDIO_PREPARED_ALIGNING || s->state==XZ_AUDIO_PREPARED_BUFFERING;
                model.deck[deck].wave_peaks = NULL; model.deck[deck].wave_count = 0;
                if (xz_audio_waveform(deck, 2, overview[deck], sizeof(overview[deck]), NULL)) {
                    for (unsigned i=0;i<sizeof(overview[deck]);i++) overview[deck][i]=(unsigned char)(overview[deck][i]*255/31);
                    model.deck[deck].wave_peaks=overview[deck];model.deck[deck].wave_count=sizeof(overview[deck]);
                }
            }
        }
    }
    int focus=xz_native_focus_deck();
    if(focus!=native_focus){native_focus=focus;followed_generation=0;}
    if(focus>=0&&focus<2&&audio_status[focus].prepared&&
       audio_status[focus].generation!=followed_generation){
        followed_generation=audio_status[focus].generation;
        if(inline_ui.deck!=focus){
            struct xz_ui_action cancelled[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,cancelled);
            inline_cancelled=inline_contact;inline_ui.deck=focus;
            if(!touch.visible)ui.deck=focus;
        }
    }
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
}

static int inline_enabled(void) { return __atomic_load_n(&started,__ATOMIC_ACQUIRE) && __atomic_load_n(&inline_requested,__ATOMIC_ACQUIRE); }
static int render_inline(void *context,uint16_t *pixels,size_t count,size_t stride,int width,int height) {
    (void)context;
    static _Thread_local uint16_t cached[536*64];
    static _Thread_local int cached_valid;
    if (!pixels || width!=536 || height!=64 || stride<536 || stride>count/64) return 0;
    if (pthread_mutex_trylock(&ui_mutex) != 0) {
        if (!cached_valid || !inline_enabled()) return 0;
        for (int y=0;y<64;y++) memcpy(pixels+y*stride,cached+y*536,536*sizeof(*pixels));
        return 1;
    }
    refresh();
    if (touch.visible || !requested_stems || !model.stems_overlay) { pthread_mutex_unlock(&ui_mutex); return 0; }
    int result=xz_ui_inline_render(&inline_ui,&model,pixels,count,stride,width,height);
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    size_t widget_count=xz_ui_inline_layout(&inline_ui,width,height,widgets);
    if(result&&!model.deck[inline_ui.deck].stem_loading)for(size_t column=0;column<widget_count;column++){
        struct xz_ui_widget widget=widgets[column];if(widget.kind!=XZ_UI_MUTE)continue;
        unsigned role=(unsigned)widget.index;
        int left=widget.x+4,right=widget.x+widget.w-4;
        unsigned char peaks[256];float progress=0;
        size_t n=(size_t)(right-left);if(n>sizeof(peaks))n=sizeof(peaks);
        if(!xz_audio_waveform(inline_ui.deck,role,peaks,n,&progress))continue;
        uint32_t rgb=xz_ui_stem_color(model.theme,(int)role);
        uint16_t color=(uint16_t)(((rgb>>19)&31)<<11|((rgb>>10)&63)<<5|((rgb>>3)&31));
        float gain=model.deck[inline_ui.deck].muted&(1u<<role)?0:model.deck[inline_ui.deck].levels[role];
        for(size_t x=0;x<n;x++){
            int amplitude=(int)(peaks[x]*gain*6/31);if(amplitude>6)amplitude=6;
            for(int y=28;y<41;y++)pixels[(size_t)y*stride+(size_t)left+x]=0;
            for(int y=34-amplitude;y<=34+amplitude;y++)pixels[(size_t)y*stride+(size_t)left+x]=gain?color:0x3186;
        }
        int needle=left+(int)(progress*(float)(n-1));for(int y=28;y<41;y++)pixels[(size_t)y*stride+(size_t)needle]=0xffff;
    }
    if (result) {
        for (int y=0;y<64;y++) memcpy(cached+y*536,pixels+y*stride,536*sizeof(*pixels));
        cached_valid=1;
    } else cached_valid=0;
    pthread_mutex_unlock(&ui_mutex);return result;
}

static int stem_controls_active(int deck) {
    return deck>=0 && deck<2 && audio_available && (model.enabled & XZ_UI_STEM);
}

int xz_ui_runtime_pad(const struct xz_cue_event *event,unsigned *trace_flags) {
    *trace_flags = 0;
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return 0;
    pthread_mutex_lock(&ui_mutex);
    if (event->deck >= 0 && event->deck < 2) deck_page[event->deck] = event->pad_page;
    if (event->sync && event->deck >= 0 && event->deck < 2) {
        unsigned bit = 1u << event->deck;
        int owned = !!(sync_owned & bit);
        if (event->operation == 2 || event->operation == 3) {
            sync_down &= ~bit; sync_owned &= ~bit;
        } else if (event->operation == 0 && !(sync_down & bit)) {
            sync_down |= bit;
            if (event->shift && model.shift_keysync) {
                sync_owned |= bit; owned = 1;
                snprintf(ui.notice,sizeof(ui.notice),"KEY SYNC NOT READY: TRACK KEY / MASTER METADATA REQUIRED");
            }
        }
        pthread_mutex_unlock(&ui_mutex); return owned;
    }
    *trace_flags = 1u | (touch.visible ? 2u : 0) | (ui.page == XZ_UI_STEMS ? 4u : 0) |
        (ui.deck == event->deck ? 8u : 0) | (model.enabled & XZ_UI_STEM ? 16u : 0) |
        (audio_available ? 32u : 0) | (event->hotcue_mode ? 64u : 0);
    int toggle;
    int active = stem_controls_active(event->deck);
    int page=event->pad_page==0?0:model.stem_page;
    int consumed = xz_stem_control_event(&pads, event, active, page, model.shift_pages, &toggle);
    if (consumed) *trace_flags |= 128u;
    if (toggle >= 0) *trace_flags |= 256u;
    if (toggle >= 0) {
        int deck = event->deck;
        model.deck[deck].muted = pads.muted[deck];
        if (toggle == 3) model.deck[deck].bypass = !model.deck[deck].bypass;
        levels(deck);
    }
    if (event->deck >= 0 && event->deck < 2) {
        if (model.deck[event->deck].bypass) *trace_flags |= 512u;
        *trace_flags |= (model.deck[event->deck].muted & 7u) << 10;
        if (audio_status[event->deck].state == XZ_AUDIO_EXPERIMENTAL_READY) *trace_flags |= 8192u;
    }
    pthread_mutex_unlock(&ui_mutex);
    return consumed;
}

int xz_ui_runtime_pad_color(int deck,int pad,unsigned *rgb,int *lit) {
    if (!__atomic_load_n(&started,__ATOMIC_ACQUIRE) || deck < 0 || deck > 1 || pad < 0 || pad > 7) return 0;
    if (pthread_mutex_trylock(&ui_mutex)) return 0;
    int slot=pad-(model.stem_bank==1?4:0);
    int active = slot>=0&&slot<4&&stem_controls_active(deck) && model.pad_feedback && (deck_page[deck] == 0 || deck_page[deck] == model.stem_page);
    if (active) {
        *rgb = slot < 3 ? xz_ui_stem_color(model.theme,xz_stem_for_pad(slot)) : 0xffffffu;
        *lit = slot < 3 ? !(model.deck[deck].muted & (1u << xz_stem_for_pad(slot))) && !model.deck[deck].bypass : model.deck[deck].bypass;
    }
    pthread_mutex_unlock(&ui_mutex); return active;
}
void xz_ui_runtime_pad_native_page(int deck,int page) {
    if (deck < 0 || deck > 1) return;
    pthread_mutex_lock(&ui_mutex); deck_page[deck] = page; pthread_mutex_unlock(&ui_mutex);
}

static void touch_hook(void *self, const struct xz_touch_status *status, const void *mode) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) { stock_touch(self, status, mode); return; }
    pthread_mutex_lock(&ui_mutex);
    refresh();
    int previous_down=((const unsigned char *)self)[4]!=0;
    if(!touch.visible&&!previous_down&&status->down&&status->x<115&&status->y>=24){
        if(status->y>=18&&status->y<159)ui.deck=inline_ui.deck=0;
        else if(status->y>=161&&status->y<302)ui.deck=inline_ui.deck=1;
    }
    int active=!touch.visible&&!(model.fb_takeover&&model.connection.connected)&&xz_native_inline_active();
    if(active&&!inline_contact&&!previous_down&&status->down&&status->x>=131&&status->x<667&&status->y>=222&&status->y<286){inline_contact=1;inline_cancelled=0;}
    int owned;
    if(inline_contact){
        struct xz_ui_action actions[XZ_UI_ACTIONS];
        size_t n=inline_cancelled?0:xz_ui_inline_touch(&inline_ui,&model,536,64,(int)status->x-131,(int)status->y-222,!!status->down,actions);
        if(n)apply(NULL,actions,n);
        if(!status->down){inline_contact=0;inline_cancelled=0;}
        owned=1;
    }else{
        if(!previous_down&&status->down)inline_wave_contact=active&&status->x>=131&&status->x<667&&status->y>=18&&status->y<222;
        struct xz_touch_status mapped=*status;
        if(inline_wave_contact&&mapped.y>=18){
            if(mapped.y<222)mapped.y=18+xz_wave_source_row(mapped.y-18,100);
            else mapped.y=mapped.y>=416?479:mapped.y+64;
        }
        owned=xz_native_touch_dispatch(&touch,self,&mapped,mode,stock_touch);
        if(!status->down)inline_wave_contact=0;
    }
    __atomic_store_n(&visible, touch.visible, __ATOMIC_RELEASE);
    if ((owned || !model.fb_takeover || !model.connection.connected) && forwarded_down && forward_touch) {
        forward_touch(0, (int)status->x, (int)status->y); forwarded_down = 0;
    } else if (!owned && model.fb_takeover && model.connection.connected && forward_touch && !!status->down != forwarded_down) {
        forwarded_down = !!status->down;
        forward_touch(forwarded_down, (int)status->x, (int)status->y);
    }
    pthread_mutex_unlock(&ui_mutex);
}

__attribute__((visibility("default"))) int xz_mods_visible_v1(void) {
    return __atomic_load_n(&visible, __ATOMIC_ACQUIRE);
}

__attribute__((visibility("default"))) int xz_mods_native_touch_v1(void) {
    return __atomic_load_n(&started, __ATOMIC_ACQUIRE);
}

__attribute__((visibility("default"))) int xz_mods_takeover_v1(void) {
    return __atomic_load_n(&fb_takeover_active, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&network_enabled, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&network_connected, __ATOMIC_ACQUIRE);
}

static void badge(uint16_t *pixels, uint32_t stride) {
    xz_ui_render_badge(pixels,stride);
    xz_ui_render_stems_button(pixels,stride,model.stems_overlay);
    if (model.connection.enabled && model.connection.connected)
        xz_ui_render_vj_button(pixels, stride, model.fb_takeover);
}

void xz_ui_runtime_on_source_key(int source) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&ui_mutex);
    if (model.takeover_assign == source) {
        struct xz_ui_action act = { .kind = XZ_UI_TAKEOVER_TOGGLE };
        apply(NULL, &act, 1);
    }
    pthread_mutex_unlock(&ui_mutex);
}

__attribute__((visibility("default"))) int xz_mods_render_v1(uint16_t *pixels, uint32_t width,
        uint32_t height, uint32_t stride, const struct xz_vj_connection_v1 *connection) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE) || !pixels || width != 800 || height != 480 || stride < 800 || stride > 16384)
        return 0;
    if (pthread_mutex_trylock(&ui_mutex) != 0) return 0;
    refresh();
    if (connection && connection->version == 1 && connection->size == sizeof(*connection)) {
        memcpy(device_ip, connection->ipv4, sizeof(device_ip)); device_ip[15] = 0;
        model.connection.ready = 1;
        model.connection.enabled = connection->listening != 0;
        model.connection.connected = connection->connected != 0;
        __atomic_store_n(&network_enabled, model.connection.enabled, __ATOMIC_RELEASE);
        __atomic_store_n(&network_connected, model.connection.connected, __ATOMIC_RELEASE);
        model.connection.discoverable = connection->discovery != 0;
        model.connection.device_ip = device_ip;
        model.connection.port = 50005;
        model.connection.protocol = "VJFS / VJVP / VJTE + VJXZ";
        model.connection.stats_valid = connection->stats_valid != 0;
        model.connection.frame_hz = connection->frame_hz_milli / 1000.0f;
        model.connection.status = "CONNECTION SERVICE CONFIGURED BY THE MOD LOADER";
    }
    int result;
    if (touch.visible) result = xz_ui_render(&ui, &model, pixels, (size_t)stride * height, stride);
    else { badge(pixels, stride); result = 1; }
    pthread_mutex_unlock(&ui_mutex);
    return result;
}

int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled) {
    static const unsigned char guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    if (started) return 0;
    settings_usb[0] = 0; settings_pending = 0;
    forward_touch = (void (*)(int,int,int))dlsym(RTLD_DEFAULT, "xz_vj_touch_v1");
    if (!forward_touch) { xz_log("UI unavailable: receiver does not expose the optional display bridge"); return -1; }
    audio_available = audio_ready;
    key_available = key_ready;
    memset(&model, 0, sizeof(model));
    model.pad_feedback = 1;
    model.stems_overlay = 1;
    model.fb_takeover = 0;
    model.takeover_assign = XZ_TAKEOVER_LINK;
    __atomic_store_n(&fb_takeover_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_connected, 0, __ATOMIC_RELEASE);
    requested_stems = !!stems_enabled;
    model.settings_status = "SETTINGS NOT SAVED: START FROM USB";
    model.enabled = xz_runtime_cue_flags();
    if (audio_ready && stems_enabled) model.enabled |= XZ_UI_STEM;
    const char *usb = getenv("XZ_MODS_USB");
    if (usb && strlen(usb) < sizeof(settings_usb) && !stat(usb,&settings_volume) && S_ISDIR(settings_volume.st_mode)) {
        strcpy(settings_usb,usb);
        struct xz_settings saved;
        int rc = xz_settings_load(usb,&saved);
        if (!rc) {
            requested_stems = saved.stems;
            model.theme = saved.theme; model.stem_page = saved.stem_page;
            model.shift_pages = saved.shift_pages; model.pad_feedback = saved.pad_feedback;
            model.shift_keysync = saved.shift_keysync;
            model.fb_takeover = saved.fb_takeover;
            model.takeover_assign = saved.takeover_assign;model.spare_eq=saved.spare_eq;model.stem_bank=saved.stem_bank;pads.bank=saved.stem_bank;
            __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
            xz_runtime_set_cues(saved.gate,saved.smart);
            model.enabled = xz_runtime_cue_flags();
            if (audio_ready) { xz_audio_set_enabled(saved.stems); if (saved.stems) model.enabled |= XZ_UI_STEM; }
        }
        model.settings_status = rc < 0 ? "INVALID USB SETTINGS: USING DEFAULTS" : rc ? "USB SETTINGS READY" : "SETTINGS RESTORED FROM USB";
    }
    const char *force_stems = getenv("XZ_MODS_STEMS_FORCE");
    const char *force_eq = getenv("XZ_MODS_SPARE_EQ_FORCE");
    if (force_eq && !strcmp(force_eq,"1")) model.spare_eq=1;
    const char *native_view = getenv("XZ_MODS_NATIVE_VIEW");
    if (native_view && !strcmp(native_view,"1")) { model.fb_takeover=0; __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE); }
    if (audio_ready && force_stems && !strcmp(force_stems,"1")) {
        requested_stems=1;model.enabled|=XZ_UI_STEM;xz_audio_set_enabled(1);
    }
    for (int i = 0; i < 4; i++) {
        model.deck[i].groove_active = model.deck[i].sample_active = -1;
        model.deck[i].levels[0] = model.deck[i].levels[1] = model.deck[i].levels[2] = 1;
        model.deck[i].sample_volume = 1;
        model.deck[i].track = i < 2 ? "NO TRACK CAPTURED" : "EXTERNAL USB AUDIO";
        model.deck[i].status = i < 2 ? "NATIVE ADAPTER EXPERIMENTAL" : "CONTROL IN REKORDBOX OR SERATO";
    }
    xz_ui_init(&ui);
    xz_ui_init(&inline_ui);
    xz_native_touch_init(&touch, &ui, &model, apply, NULL);
    refresh();
    if (xz_hook_arm(0x2628b4, guard, (void *)touch_hook, (void **)&stock_touch) != 0) return -1;
    if (xz_hook_arm(0xdf994, link_guard, (void *)uikey_link_hook, (void **)&stock_uikey_link) != 0)
        xz_log("Source key LINK hook unavailable");
    if (xz_hook_arm(0xe0a50, rekordbox_guard, (void *)uikey_rekordbox_hook, (void **)&stock_uikey_rekordbox) != 0)
        xz_log("Source key REKORDBOX hook unavailable");
    if (settings_usb[0]) {
        settings_running = 1;
        if (pthread_create(&settings_thread,NULL,settings_writer,NULL)) {
            settings_running = 0; model.settings_status = "SETTINGS SAVE UNAVAILABLE";
        }
    }
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    if (xz_native_inline_start(inline_enabled,render_inline,NULL)) xz_log("Native inline stems unavailable: window ABI guard failed");
    model.eq_available=xz_native_eq_start(mixer_eq_update)==0;
    if(!model.eq_available)model.eq_status=XZ_EQ_UNAVAILABLE;
    xz_native_eq_enable(model.spare_eq&&requested_stems);
    if (xz_native_led_start()) xz_log("Native pad LED feedback unavailable");
    xz_log("UI touch adapter installed; MODS badge opens the panel");
    return 0;
}

void xz_ui_runtime_stop(void) {
    xz_native_eq_stop();
    xz_native_led_stop();
    __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
    pthread_mutex_lock(&ui_mutex);
    int join = settings_running;
    settings_running = 0; pthread_cond_signal(&settings_changed);
    pthread_mutex_unlock(&ui_mutex);
    if (join) pthread_join(settings_thread,NULL);
}
