/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_KEY_RUNTIME_H
#define XZ_KEY_RUNTIME_H

int xz_key_runtime_start(void);
void xz_key_runtime_stop(void);
int xz_key_set_desired_semitones(int deck, int semitones);
int xz_key_get_desired_semitones(int deck, int *semitones_out);

#endif
