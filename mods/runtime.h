#ifndef XZ_MOD_RUNTIME_H
#define XZ_MOD_RUNTIME_H
#include <stddef.h>
#include <stdint.h>

/* Constructor/loader thread only. Known ARM prologues, never a generic patch API. */
int xz_hook_arm(uint32_t address, const unsigned char expected[8],
                void *replacement, void **original);
int xz_hook_slot(uint32_t address, uint32_t expected, void *replacement, void **original);
/* Worker only, return zero after copying the complete range. */
int xz_read_memory(uint32_t address, void *out, size_t length);
void xz_log(const char *message);
int xz_runtime_set_cues(int gate, int smart);
unsigned xz_runtime_cue_flags(void);
#endif
