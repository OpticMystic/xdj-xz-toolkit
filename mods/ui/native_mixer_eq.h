#ifndef XZ_NATIVE_MIXER_EQ_H
#define XZ_NATIVE_MIXER_EQ_H
#include <stdint.h>
enum xz_eq_status { XZ_EQ_OFF, XZ_EQ_ACTIVE, XZ_EQ_SOURCE, XZ_EQ_EXTERNAL, XZ_EQ_WAITING, XZ_EQ_UNAVAILABLE };
struct xz_eq_snapshot { unsigned allowed,status; uint32_t revision[2][3]; float gain[2][3]; };
typedef void (*xz_eq_callback)(const struct xz_eq_snapshot *);
int xz_native_eq_start(xz_eq_callback);
void xz_native_eq_enable(int);
void xz_native_eq_stop(void);
#endif
