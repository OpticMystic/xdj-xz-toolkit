/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_RUNTIME_H
#define XZ_NATIVE_WAVE_RUNTIME_H
#include "native_wave.h"
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context);
int xz_native_inline_active(void);
int xz_native_focus_deck(void);
#endif
