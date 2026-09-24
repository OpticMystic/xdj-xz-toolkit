#ifdef NDEBUG
#error Inline acceptance requires active assertions
#endif
#include "ui.h"
#include <assert.h>
#include <stdio.h>
static uint16_t frame[540*60+8];
int main(void) {
    struct xz_ui u; struct xz_ui_model m={0}; struct xz_ui_action a[XZ_UI_ACTIONS];
    xz_ui_init(&u); m.enabled=XZ_UI_STEM;
    for(int deck=0;deck<2;deck++) {m.deck[deck].ready=XZ_UI_STEM;for(int i=0;i<3;i++)m.deck[deck].levels[i]=1;}
    for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=0xabcd;
    for(int theme=0;theme<7;theme++) {
        m.theme=theme;assert(xz_ui_inline_render(&u,&m,frame,540*60,540,536,60));
        for(int col=0;col<3;col++){
            const int order[3]={2,0,1};unsigned c=xz_ui_stem_color(theme,order[col]);
            uint16_t expected=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));
            int x=100+436*col/3;assert(frame[2*540+x]==expected);
            m.deck[0].muted=7;assert(xz_ui_inline_render(&u,&m,frame,540*60,540,536,60));
            assert(frame[2*540+x]==expected);m.deck[0].muted=0;
        }
        for(int y=0;y<60;y++)for(int x=536;x<540;x++)assert(frame[y*540+x]==0xabcd);
        for(unsigned i=540*60;i<sizeof(frame)/sizeof(*frame);i++)assert(frame[i]==0xabcd);
    }
    assert(xz_ui_inline_touch(&u,&m,536,60,120,10,1,a)==0);
    u.deck=1;
    assert(xz_ui_inline_touch(&u,&m,536,60,-10,-10,0,a)==1);
    assert(a[0].kind==XZ_UI_MUTE&&a[0].index==2&&a[0].deck==0&&a[0].phase==XZ_UI_PRESS);
    assert(xz_ui_inline_touch(&u,&m,536,60,120,40,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,60,80,40,1,a)==1);
    assert(a[0].kind==XZ_UI_LEVEL&&a[0].deck==1&&a[0].value>0&&a[0].value<1);
    assert(xz_ui_inline_touch(&u,&m,536,60,900,40,1,a)==1&&a[0].value==1);
    assert(xz_ui_inline_touch(&u,&m,536,60,900,40,0,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,60,50,10,1,a)==1);
    assert(a[0].kind==XZ_UI_BYPASS&&a[0].value==1);
    xz_ui_inline_touch(&u,&m,536,60,50,10,0,a);
    assert(xz_ui_inline_touch(&u,&m,536,60,10,10,1,a)==1);
    assert(a[0].kind==XZ_UI_DECK&&a[0].deck==1&&a[0].index==0&&u.deck==0);
    xz_ui_inline_touch(&u,&m,536,60,10,10,0,a);
    m.enabled=0;
    assert(xz_ui_inline_touch(&u,&m,536,60,120,10,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE);
    assert(!xz_ui_inline_render(&u,&m,frame,1,540,536,60));
    assert(!xz_ui_inline_render(&u,&m,frame,540*60,540,300,60));
    u.deck=3;assert(!xz_ui_inline_render(&u,&m,frame,540*60,540,536,60));
    puts("PASS inline stems render bounds, native deck choice, mute capture, slider and readiness");
}
