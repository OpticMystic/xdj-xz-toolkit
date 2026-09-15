/* SPDX-License-Identifier: MIT */
#ifndef XZ_SETTINGS_H
#define XZ_SETTINGS_H
#include <stddef.h>
struct xz_settings {
    int stems, gate, smart, theme, stem_page, shift_pages, pad_feedback, shift_keysync;
};
void xz_settings_default(struct xz_settings *);
int xz_settings_parse(const char *, struct xz_settings *);
int xz_settings_format(const struct xz_settings *, char *, size_t);
/* These run on the settings worker, never the audio or input callback. */
int xz_settings_load(const char *usb, struct xz_settings *);
int xz_settings_save(const char *usb, const struct xz_settings *);
#endif
