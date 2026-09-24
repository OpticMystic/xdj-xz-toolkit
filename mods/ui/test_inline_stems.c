#ifdef NDEBUG
#error Inline acceptance requires active assertions
#endif
#include "ui.h"
#include <assert.h>
#include <stdio.h>
static uint16_t frame[540*64+8];
int main(void) {
    struct xz_ui u; struct xz_ui_model m={0}; struct xz_ui_action a[XZ_UI_ACTIONS];
    xz_ui_init(&u); m.enabled=XZ_UI_STEM;
    for(int deck=0;deck<2;deck++) {m.deck[deck].ready=XZ_UI_STEM;for(int i=0;i<3;i++)m.deck[deck].levels[i]=1;}
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    assert(xz_ui_inline_layout(&u,536,64,widgets)==8);
    for(int deck=0;deck<2;deck++)for(int slot=0;slot<4;slot++){
        struct xz_ui_widget w=widgets[deck*4+slot];
        assert(w.deck==deck&&w.y==deck*32+1&&w.h==30);
        assert(w.x==slot*134+1&&w.w==132);
        assert(w.kind==(slot==3?XZ_UI_BYPASS:XZ_UI_MUTE));
        if(slot<3)assert(w.index==2-slot);
    }
    for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=0xabcd;
    for(int theme=0;theme<7;theme++) {
        m.theme=theme;assert(xz_ui_inline_render(&u,&m,frame,540*64,540,536,64));
        for(int deck=0;deck<2;deck++)for(int col=0;col<3;col++){
            unsigned c=xz_ui_stem_color(theme,2-col);
            uint16_t expected=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));
            int x=col*134+1;assert(frame[(deck*32+1)*540+x]==expected);
        }
        for(int y=0;y<64;y++)for(int x=536;x<540;x++)assert(frame[y*540+x]==0xabcd);
        for(unsigned i=540*64;i<sizeof(frame)/sizeof(*frame);i++)assert(frame[i]==0xabcd);
    }
    u.deck=1;
    assert(xz_ui_inline_touch(&u,&m,536,64,60,10,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,64,-10,-10,0,a)==1);
    assert(a[0].kind==XZ_UI_MUTE&&a[0].index==2&&a[0].deck==0&&a[0].phase==XZ_UI_PRESS);
    assert(xz_ui_inline_touch(&u,&m,536,64,194,42,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,64,60,10,1,a)==1);
    assert(a[0].kind==XZ_UI_LEVEL&&a[0].deck==1&&a[0].index==1&&a[0].value==0);
    assert(xz_ui_inline_touch(&u,&m,536,64,900,10,1,a)==1&&a[0].deck==1&&a[0].value==1);
    assert(xz_ui_inline_touch(&u,&m,536,64,900,10,0,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,64,460,10,1,a)==1);
    assert(a[0].kind==XZ_UI_BYPASS&&a[0].deck==0&&a[0].value==1);
    xz_ui_inline_touch(&u,&m,536,64,460,10,0,a);
    assert(xz_ui_inline_touch(&u,&m,536,64,460,42,1,a)==1);
    assert(a[0].kind==XZ_UI_BYPASS&&a[0].deck==1&&a[0].value==1);
    xz_ui_inline_touch(&u,&m,536,64,460,42,0,a);
    m.enabled=0;
    assert(xz_ui_inline_touch(&u,&m,536,64,60,10,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&a[0].deck==0);
    assert(!xz_ui_inline_render(&u,&m,frame,1,540,536,64));
    assert(!xz_ui_inline_render(&u,&m,frame,540*64,540,300,64));
    assert(!xz_ui_inline_render(&u,&m,frame,540*64,540,536,60));
    u.deck=3;assert(!xz_ui_inline_render(&u,&m,frame,540*64,540,536,64));
    puts("PASS two inline decks, eight controls, touch ownership and render bounds");
}
