/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error Test assertions must be enabled: compile with -UNDEBUG
#endif
#include "native_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[65536];
static void put(uint32_t address, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) memory[address - 0x10000 + i] = (unsigned char)(value >> (i * 8));
}
static int read_memory(void *context, uint32_t address, void *out, size_t n)
{
    (void)context;
    if (address < 0x10000 || (uint64_t)address + n > 0x20000) return -1;
    memcpy(out, memory + address - 0x10000, n);
    return 0;
}
int main(void)
{
    struct xz126_deck_source source;
    struct xz126_source_extent e;
    unsigned deck;
    put(0x1000c, 0x10100); put(0x10010, 0x10108);
    for (deck = 0; deck < 2; ++deck) {
        uint32_t p = 0x11000 + deck * 0x1000;
        uint32_t r = p + 0x200, impl = p + 0x300, file = p + 0xa00;
        put(0x10100 + deck * 4, p);
        put(p + 0xc8, r); put(p + 0xd0 + 0x5c, r);
        put(r + 4, impl); put(r + 0xc, 44100);
        put(impl + 0x694, file);
        put(file + 0x14, 12345); put(file + 0x18, 44100);
        snprintf((char *)memory + impl - 0x10000, 100, "/media/usb/track%u.flac", deck);
        assert(xz126_deck_source_read(read_memory, NULL, 0x10000, deck, &source) == 0);
        assert(source.player == p && source.reader == r && source.manager == p + 0xd0);
        assert(source.file_frames == 12345 && source.reader_rate == 44100);
        assert(source.path[16] == (char)('0' + deck));
    }
    put(0x11300 + 0x69c, 0x11b00);
    put(0x11b08, 13436); put(0x11b0c, 48000); put(0x1120c, 48000);
    assert(xz126_deck_source_read(read_memory, NULL, 0x10000, 0, &source) == 0);
    assert(source.reader_frames == 13436 && source.reader_rate == 48000 && source.file_rate == 44100);
    put(0x110d0 + 0x5c, 0);
    assert(xz126_deck_source_read(read_memory, NULL, 0x10000, 0, &source) == 0);
    assert(!source.manager_bound);
    assert(xz126_deck_source_read(read_memory, NULL, 0x10000, 2, &source) == -1);
    put(0x110c8, 0xfffffff0);
    assert(xz126_deck_source_read(read_memory, NULL, 0x10000, 0, &source) == -1);
    assert(!source.path[0]);
    e = xz126_source_extent(-4, 10, 100);
    assert(e.skip == 4 && e.frames == 6 && e.source_position == 0);
    e = xz126_source_extent(96, 10, 100);
    assert(e.skip == 0 && e.frames == 4 && e.source_position == 96);
    e = xz126_source_extent(INT32_MIN, 2, 100);
    assert(e.frames == 0);
    e = xz126_source_extent(INT32_MAX, UINT32_MAX, 100);
    assert(e.frames == 0);
    puts("two-deck reader identity, source/converter metadata and zero-fill extents passed");
    return 0;
}
