/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "native_reader.h"
#include <string.h>

static int address_ok(uint32_t address, size_t n)
{
    return address >= 0x10000 && n <= UINT32_MAX &&
           (uint64_t)address + n <= UINT64_C(0x100000000);
}

static int word(xz126_read_fn read, void *context, uint32_t base,
                uint32_t offset, uint32_t *out)
{
    unsigned char b[4];
    uint64_t at = (uint64_t)base + offset;
    if ((base & 3) || at > UINT32_MAX || !address_ok((uint32_t)at, 4) ||
        read(context, (uint32_t)at, b, 4)) return -1;
    *out = (uint32_t)b[0] | (uint32_t)b[1] << 8 |
           (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

static int rate_ok(uint32_t rate)
{
    return rate >= 8000 && rate <= 384000;
}

int xz126_deck_source_read(xz126_read_fn read, void *context,
                           uint32_t engine, unsigned deck,
                           struct xz126_deck_source *out)
{
    struct xz126_deck_source source = {0};
    uint32_t begin, end, owner_rate, check, manager_reader;
    size_t i;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!read || deck >= 4 || !address_ok(engine, 0x14) ||
        word(read, context, engine, 0x0c, &begin) ||
        word(read, context, engine, 0x10, &end) || end < begin ||
        ((end - begin) & 3) || (end - begin) / 4 > 4 ||
        deck >= (end - begin) / 4 ||
        word(read, context, begin, deck * 4, &source.player) ||
        !address_ok(source.player, 0x130) ||
        word(read, context, source.player, 0xc8, &source.reader) ||
        !address_ok(source.reader, 0x10)) return -1;
    source.deck_count = (end - begin) / 4;
    source.manager = source.player + 0xd0;
    if (word(read, context, source.manager, 0x5c, &manager_reader) ||
        (manager_reader && manager_reader != source.reader) ||
        word(read, context, source.reader, 4, &source.reader_impl) ||
        !address_ok(source.reader_impl, 0x6b8) ||
        word(read, context, source.reader, 0xc, &owner_rate) ||
        word(read, context, source.reader_impl, 0x694, &source.file_reader) ||
        !address_ok(source.file_reader, 0x1c) ||
        word(read, context, source.file_reader, 0x14, &source.file_frames) ||
        word(read, context, source.file_reader, 0x18, &source.file_rate) ||
        word(read, context, source.reader_impl, 0x69c, &source.converter)) return -1;
    source.manager_bound = manager_reader == source.reader;
    if (source.converter) {
        if (word(read, context, source.converter, 8, &source.reader_frames) ||
            word(read, context, source.converter, 0xc, &source.reader_rate)) return -1;
    } else {
        source.reader_frames = source.file_frames;
        source.reader_rate = source.file_rate;
    }
    if (!rate_ok(source.reader_rate) || !rate_ok(source.file_rate) ||
        (owner_rate && owner_rate != source.reader_rate) ||
        !source.reader_frames || source.reader_frames >= INT32_MAX ||
        !source.file_frames || source.file_frames >= INT32_MAX) return -1;
    for (i = 0; i < sizeof(source.path); ++i) {
        if (read(context, source.reader_impl + (uint32_t)i, source.path + i, 1))
            return -1;
        if (!source.path[i]) break;
    }
    if (!i || i == sizeof(source.path)) return -1;
    /* Reject a track switch observed during the snapshot. The worker must still
     * publish against a generation and the audio hook must match that identity. */
    if (word(read, context, source.reader, 4, &check) || check != source.reader_impl ||
        word(read, context, source.player, 0xc8, &check) || check != source.reader ||
        word(read, context, engine, 0xc, &check) || check != begin ||
        word(read, context, engine, 0x10, &check) || check != end) return -1;
    *out = source;
    return 0;
}

struct xz126_source_extent xz126_source_extent(int32_t position,
                                               uint32_t count,
                                               uint32_t length)
{
    struct xz126_source_extent out = {0};
    int64_t start = position;
    int64_t finish = start + count;
    int64_t first = start < 0 ? 0 : start;
    int64_t last = finish > length ? length : finish;
    if (last <= first) return out;
    out.skip = (uint32_t)(first - start);
    out.frames = (uint32_t)(last - first);
    out.source_position = (uint32_t)first;
    return out;
}
