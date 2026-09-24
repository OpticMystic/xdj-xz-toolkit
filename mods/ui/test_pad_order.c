#include "ui.h"
#include "stem_pads.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Pad order acceptance requires active assertions
#endif
int main(void) {
    struct xz_ui_model m={0};m.enabled=XZ_UI_STEM;
    for(int deck=0;deck<2;deck++){
        m.deck[deck].ready=XZ_UI_STEM;
        struct xz_ui panel;xz_ui_init(&panel);panel.deck=deck;panel.page=XZ_UI_STEMS;
        struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(&panel,&m,widgets);int column=0;
        for(size_t i=0;i<count;i++)if(widgets[i].kind==XZ_UI_MUTE){assert(widgets[i].index==2-column);column++;}
        assert(column==3);
        for(int pad=0;pad<4;pad++){
            struct xz_ui u;xz_ui_init(&u);u.deck=1-deck;
            struct xz_ui_action actions[XZ_UI_ACTIONS];
            struct xz_stem_pads state={0};
            struct xz_cue_event e={deck,pad,0,1,0,0,0,-1,0};int toggle=-1;
            assert(xz_stem_pad_event(&state,&e,1,&toggle)&&toggle==(pad<3?2-pad:3));
            int x=pad*134+60,y=deck*32+12;
            size_t n=xz_ui_inline_touch(&u,&m,536,64,x,y,1,actions);
            if(!n)n=xz_ui_inline_touch(&u,&m,536,64,x,y,0,actions);
            assert(n==1);
            if(pad<3){
                if(actions[0].kind!=XZ_UI_MUTE||actions[0].index!=toggle){
                    fprintf(stderr,"Pad %c controls stem %d but screen column %d controls stem %d\n",'A'+pad,toggle,pad+1,actions[0].index);return 1;
                }
            }else assert(actions[0].kind==XZ_UI_BYPASS);
            assert(actions[0].deck==deck);
        }
    }
    puts("PASS screen A/B/C/D agrees with physical pads on both decks");
}
