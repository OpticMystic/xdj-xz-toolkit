/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../ui_runtime.c"
#ifdef NDEBUG
#error Runtime UI acceptance requires active assertions
#endif
static int native_active=1,stock_calls,fixture_focus=-1,fixture_prepared=1;
static struct xz_touch_status last_stock;
static struct xz_stem_levels applied[2];
static uint32_t fixture_generation=1;
static unsigned waveform_calls[2];
int xz_native_focus_deck(void){return fixture_focus;}
int xz_native_inline_active(void){return native_active;}
int xz_native_inline_start(int(*on)(void),xz_native_wave_render draw,void *context){(void)on;(void)draw;(void)context;return 0;}
int xz_hook_arm(uint32_t a,const unsigned char g[8],void *r,void **o){(void)a;(void)g;(void)r;(void)o;return 0;}
int xz_native_led_start(void){return 0;}
void xz_native_led_stop(void){}
void xz_log(const char *s){(void)s;}
unsigned xz_runtime_cue_flags(void){return 0;}
int xz_runtime_set_cues(int a,int b){(void)a;(void)b;return 0;}
int xz_key_get_desired_semitones(int d,int *out){(void)d;*out=0;return 0;}
int xz_key_set_desired_semitones(int d,int n){(void)d;(void)n;return 0;}
void xz_audio_set_enabled(int on){(void)on;}
void xz_audio_set_levels(int deck,struct xz_stem_levels value){applied[deck]=value;}
int xz_audio_get_status(int deck,struct xz_audio_status *out){(void)deck;memset(out,0,sizeof(*out));out->generation=fixture_generation;out->prepared=fixture_prepared;out->state=XZ_AUDIO_EXPERIMENTAL_READY;return 0;}
const char *xz_audio_state_name(enum xz_audio_state state){(void)state;return "ready";}
int xz_audio_waveform(int deck,unsigned role,unsigned char *bins,size_t count,float *progress){
    (void)role;if(deck<0||deck>1)return 0;
    waveform_calls[deck]++;memset(bins,deck?24:8,count);if(progress)*progress=0.5;return 1;
}
static void stock(void *self,const struct xz_touch_status *s,const void *mode){(void)mode;stock_calls++;last_stock=*s;memcpy((char*)self+4,s,sizeof(*s));}
static void contact(void *self,unsigned x,unsigned y,int down){struct xz_touch_status s={(uint8_t)down,{0},x,y};touch_hook(self,&s,NULL);}
int main(void){
    unsigned char self[32]={0};started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;model.stems_overlay=1;
    xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
    contact(self,200,130,1);contact(self,200,130,0);assert(model.deck[0].muted==4&&applied[0].vocals==0&&stock_calls==0);
    contact(self,200,260,1);contact(self,200,260,0);assert(model.deck[1].muted==4&&applied[1].vocals==0);
    contact(self,200,130,1);contact(self,200,130,0);assert(!model.deck[0].muted&&applied[0].vocals==1);
    contact(self,200,260,1);contact(self,200,260,0);assert(!model.deck[1].muted&&applied[1].vocals==1);
    contact(self,600,130,1);contact(self,600,130,0);assert(model.deck[0].bypass&&!model.deck[1].bypass);
    contact(self,600,260,1);contact(self,600,260,0);assert(model.deck[0].bypass&&model.deck[1].bypass);
    contact(self,600,130,1);contact(self,600,130,0);
    contact(self,600,260,1);contact(self,600,260,0);assert(!model.deck[0].bypass&&!model.deck[1].bypass);
    struct xz_ui_action slider={.kind=XZ_UI_LEVEL,.deck=0,.index=2,.value=.5f};apply(NULL,&slider,1);
    contact(self,200,130,1);contact(self,160,260,1);
    assert(inline_contact_deck==0&&model.deck[0].levels[2]<.5f&&model.deck[1].levels[2]==1&&stock_calls==0);
    contact(self,160,260,0);assert(!inline_contact);
    slider.value=1;apply(NULL,&slider,1);
    contact(self,200,130,1);
    struct xz_ui_action hide={.kind=XZ_UI_STEMS_OVERLAY};apply(NULL,&hide,1);
    assert(!model.stems_overlay&&inline_cancelled&&!inline_ui.down);
    contact(self,200,130,0);assert(!inline_contact);
    apply(NULL,&hide,1);assert(model.stems_overlay);
    int calls=stock_calls;native_active=0;contact(self,799,400,1);contact(self,799,400,0);assert(stock_calls==calls+2&&!inline_contact);
    native_active=1;contact(self,30,400,1);contact(self,200,130,1);contact(self,200,130,0);assert(stock_calls==calls+5);
    contact(self,200,80,1);assert(last_stock.x==200&&last_stock.y==18+xz_wave_source_row(62,100));
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_source_row(99,100));contact(self,200,180,0);
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_source_row(162,100));
    contact(self,200,130,1);assert(last_stock.y==18+xz_wave_source_row(132,100));contact(self,200,130,0);
    contact(self,200,260,1);fixture_generation++;contact(self,650,260,1);
    assert(model.deck[1].levels[2]==1&&inline_cancelled&&!inline_ui.down);
    contact(self,650,260,0);assert(!inline_contact);
    size_t count=536*64;uint16_t *pixels=calloc(count+2,sizeof(*pixels));assert(pixels);pixels[0]=0x1234;pixels[count+1]=0xabcd;
    unsigned before_wave[2]={waveform_calls[0],waveform_calls[1]};
    assert(render_inline(NULL,pixels+1,count,536,536,64));assert(pixels[0]==0x1234&&pixels[count+1]==0xabcd);
    assert(waveform_calls[0]-before_wave[0]==4&&waveform_calls[1]-before_wave[1]==4);
    uint16_t *saved=malloc(count*sizeof(*saved));assert(saved);memcpy(saved,pixels+1,count*sizeof(*saved));
    pthread_mutex_lock(&ui_mutex);memset(pixels+1,0,count*sizeof(*pixels));
    int cached=render_inline(NULL,pixels+1,count,536,536,64);pthread_mutex_unlock(&ui_mutex);
    assert(cached&&!memcmp(saved,pixels+1,count*sizeof(*saved)));free(saved);
    model.stem_page=1;model.pad_feedback=1;
    struct xz_cue_event e={0};e.deck=0;e.pad=0;e.mode_button=-1;e.pad_page=0;e.hotcue_mode=1;
    unsigned flags,rgb;int lit;
    assert(xz_ui_runtime_pad(&e,&flags));assert(applied[0].vocals==0);
    assert(xz_ui_runtime_pad_color(0,0,&rgb,&lit)&&rgb==xz_ui_stem_color(model.theme,2)&&!lit);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    struct xz_ui_action action={0};action.kind=XZ_UI_MUTE;action.deck=0;action.index=2;action.phase=XZ_UI_PRESS;
    apply(NULL,&action,1);assert(applied[0].vocals==1);
    apply(NULL,&action,1);assert(applied[0].vocals==0);
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags)&&applied[0].vocals==1);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    native_active=0;e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    e.deck=1;e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));
    assert(applied[0].vocals==0&&applied[1].vocals==0);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));assert(applied[0].vocals==0&&applied[1].vocals==1);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));native_active=1;
    int before=stock_calls;contact(self,700,10,1);contact(self,700,10,1);contact(self,780,400,0);
    assert(!model.stems_overlay&&!inline_enabled()&&stock_calls==before);
    assert(!render_inline(NULL,pixels+1,count,536,536,64));
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    contact(self,700,10,1);contact(self,700,10,0);assert(model.stems_overlay&&inline_enabled());
    fixture_focus=0;refresh();assert(inline_ui.deck==0);
    fixture_prepared=0;fixture_focus=1;refresh();assert(inline_ui.deck==0);
    fixture_prepared=1;refresh();assert(inline_ui.deck==1&&ui.deck==1);
    inline_ui.deck=0;refresh();assert(inline_ui.deck==0);
    fixture_generation++;refresh();assert(inline_ui.deck==1);
    fixture_focus=3;refresh();assert(inline_ui.deck==1);
    model.enabled=0;e.operation=0;assert(!xz_ui_runtime_pad(&e,&flags));
    e.operation=2;assert(!xz_ui_runtime_pad(&e,&flags));model.enabled=XZ_UI_STEM;
    touch.visible=1;assert(!render_inline(NULL,pixels+1,count,536,536,64));free(pixels);
    touch.visible=0;model.connection.enabled=model.connection.connected=1;model.fb_takeover=1;
    int native_before=stock_calls;
    contact(self,350,350,1);contact(self,350,350,0);assert(stock_calls==native_before);
    contact(self,40,10,1);contact(self,40,10,0);assert(!model.fb_takeover);
    contact(self,350,350,1);contact(self,350,350,0);assert(stock_calls>native_before);
    contact(self,40,10,1);contact(self,40,10,0);assert(model.fb_takeover);
    puts("PASS two-deck stem controls, waveform gestures, VJ view and touch ownership");return 0;
}
