#ifdef NDEBUG
#error Wave viewport acceptance requires active assertions
#endif
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
static uint16_t pixels[540 * 268 + 8];
static void native_redraw(void) {
    for (unsigned y=0;y<268;y++) for (unsigned x=0;x<540;x++)
        pixels[y*540+x] = x<536 ? (uint16_t)(y*127+x) : 0xbeef;
    for (unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++) pixels[i]=0xabcd;
}
int main(void) {
    for(unsigned y=0;y<10;y++) {
        assert(xz_wave_source_row(y,92)==y);
        assert(xz_wave_source_row(y+82,92)==y+122);
        assert(xz_wave_source_row(y+132,92)==y+136);
        assert(xz_wave_source_row(y+214,92)==y+258);
    }
    assert(xz_wave_source_row(132,92)==136);
    assert(xz_wave_source_row(223,92)==267);
    assert(xz_wave_source_row(999,92)==267);
    for (unsigned height=21;height<=94;height++) {
        native_redraw();
        assert(xz_wave_compact(pixels,540*268,540,height));
        for (unsigned y=0;y<268;y++) for (unsigned x=0;x<540;x++) {
            int waveform=y<height||(y>=height+XZ_WAVE_CONTROL_HEIGHT&&y<height*2+XZ_WAVE_CONTROL_HEIGHT);
            uint16_t expected = x>=536 ? 0xbeef : !waveform ? 0 :
                (uint16_t)(xz_wave_source_row(y,height)*127+x);
            assert(pixels[y*540+x]==expected);
        }
        for (unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++) assert(pixels[i]==0xabcd);
    }
    assert(!xz_wave_compact(pixels,10,540,100));
    assert(!xz_wave_compact(pixels,540*268,530,100));
    assert(!xz_wave_compact(pixels,540*268,540,20));
    assert(!xz_wave_compact(pixels,540*268,540,95));
    assert(!xz_wave_compact(pixels,SIZE_MAX,SIZE_MAX,100));
    puts("PASS both lanes, unscaled cue bands, inverse mapping, row padding and bounds");
}
