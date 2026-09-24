#ifdef NDEBUG
#error Pad acceptance requires active assertions
#endif
#include "stem_pads.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct xz_stem_pads s = {0};
    struct xz_cue_event e = {0, 0, 0, 1, 0, 0, 0, -1, 0};
    int toggle;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == 2 && s.muted[0] == 4);
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == -1 && s.muted[0] == 4);
    e.operation = 1;
    assert(xz_stem_pad_event(&s, &e, 0, &toggle));
    e.operation = 2; e.hotcue_mode = 0;
    assert(xz_stem_pad_event(&s, &e, 0, &toggle) && s.owned[0] == 0 && s.muted[0] == 4);
    e.operation = 0; e.hotcue_mode = 1;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && s.muted[0] == 0);
    e.operation = 3;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle));
    /* A stock-owned press must keep its repeat and release after opening STEMS. */
    e.operation = 0;
    assert(!xz_stem_pad_event(&s, &e, 0, &toggle));
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == -1);
    e.operation = 2;
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    for (int deck = 0; deck < 2; deck++) for (int pad = 0; pad < 4; pad++) {
        e = (struct xz_cue_event){deck, pad, 0, 1, 0, 0, 0, -1, 0};
        unsigned other = s.muted[1-deck];
        assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == (pad<3?2-pad:3));
        assert(s.muted[1-deck] == other);
        e.operation = 3;
        assert(xz_stem_pad_event(&s, &e, 0, &toggle));
    }
    for (int pad = 4; pad < 8; pad++) {
        e = (struct xz_cue_event){0, pad, 0, 1, 0, 0, 0, -1, 0};
        assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    }
    e = (struct xz_cue_event){0, 1, 0, 0, 0, -1, 0, -1, 0};
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    e = (struct xz_cue_event){0, -1, 0, 1, 1, 0, 0, -1, 0};
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    for (int page=0;page<4;page++) {
        s=(struct xz_stem_pads){0};
        e=(struct xz_cue_event){0,0,0,page==0,0,page,0,-1,0};
        assert(xz_stem_control_event(&s,&e,1,page,0,&toggle) && toggle==2);
        e.operation=2; e.pad_page=-1;
        assert(xz_stem_control_event(&s,&e,0,page,0,&toggle));
        e=(struct xz_cue_event){0,-1,0,0,0,-1,1,page,0};
        assert(xz_stem_control_event(&s,&e,1,0,1,&toggle) && toggle==(page<3?2-page:3));
        e.operation=2;e.shift=0;
        assert(xz_stem_control_event(&s,&e,0,0,0,&toggle));
        e.operation=0;
        assert(!xz_stem_control_event(&s,&e,1,0,1,&toggle));
        e.shift=1;
        assert(!xz_stem_control_event(&s,&e,1,0,1,&toggle));
    }
    for(int bank=0;bank<2;bank++)for(int deck=0;deck<2;deck++)for(int pad=0;pad<8;pad++){
        s=(struct xz_stem_pads){0};s.bank=bank;
        e=(struct xz_cue_event){deck,pad,0,1,0,0,0,-1,0};
        int slot=pad-bank*4,expected=slot>=0&&slot<4;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)==expected);
        assert(toggle==(expected?(slot<3?2-slot:3):-1));
        s.bank=1-bank;e.operation=2;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)==expected);
    }
    s=(struct xz_stem_pads){0};s.bank=1;
    e=(struct xz_cue_event){0,0,0,1,0,0,0,-1,0};
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));s.bank=0;
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));e.operation=2;
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));
    puts("PASS A-D/E-H banks, inactive-bank hot cues, deck independence and release ownership across bank changes");
}
