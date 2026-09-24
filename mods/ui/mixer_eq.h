#ifndef XZ_MIXER_EQ_H
#define XZ_MIXER_EQ_H
#include <stdint.h>
struct xz_eq_pickup { float previous; int seen, acquired; };
int xz_eq_decode(uint32_t report,int *deck,int *stem,float *gain);
unsigned xz_eq_allowed(uint32_t switches,unsigned local_usb_decks);
int xz_eq_pickup(struct xz_eq_pickup *,float input,float current);
#endif
