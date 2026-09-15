#include "wave_viewport.h"
#include <string.h>
static unsigned lane_row(unsigned row,unsigned lane_height) {
    if(row<10)return row;
    if(row>=lane_height-10)return 132-(lane_height-row);
    return 10+(row-10)*112/(lane_height-20);
}
unsigned xz_wave_source_row(unsigned row,unsigned lane_height) {
    if(lane_height<=20||lane_height>132)return 0;
    if(row<lane_height)return lane_row(row,lane_height);
    if(row<lane_height+4)return 132+row-lane_height;
    if(row<lane_height*2+4)return 136+lane_row(row-lane_height-4,lane_height);
    return 267;
}
int xz_wave_compact(uint16_t *pixels,size_t count,size_t stride,unsigned lane_height) {
    if(!pixels||lane_height<=20||lane_height>132||stride<536||
       stride>SIZE_MAX/sizeof(*pixels)/268||count<stride*268)return 0;
    unsigned visible=lane_height*2+4;
    for(unsigned y=0;y<visible;y++) {
        unsigned source=xz_wave_source_row(y,lane_height);
        memmove(pixels+(size_t)y*stride,pixels+(size_t)source*stride,536*sizeof(*pixels));
    }
    for(unsigned y=visible;y<268;y++)memset(pixels+(size_t)y*stride,0,536*sizeof(*pixels));
    return 1;
}
