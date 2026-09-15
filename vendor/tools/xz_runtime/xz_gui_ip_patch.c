#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_SIZE 44u
#define LOGO_INDEX 1487u

static uint16_t read_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t *glyph(char c) {
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static const uint8_t dot[7] = {0,0,0,0,0,6,6};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '.') return dot;
    return dash;
}

static void put(uint16_t *pixels, int width, int height, int x, int y, uint16_t color) {
    if (x >= 0 && y >= 0 && x < width && y < height) pixels[y * width + x] = color;
}

static void draw_text(uint16_t *pixels, int width, int height, int x, int y, const char *text) {
    const int scale = 3;
    const uint16_t color = 0xffe0; /* vivid yellow RGB565 */
    for (; *text; ++text, x += 6 * scale) {
        const uint8_t *rows = glyph(*text);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (!(rows[row] & (1u << (4 - col)))) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx)
                        put(pixels, width, height, x + col * scale + sx, y + row * scale + sy, color);
            }
        }
    }
}

int main(int argc, char **argv) {
    uint8_t entry[ENTRY_SIZE];
    uint16_t width, height;
    uint32_t data_offset;
    uint16_t *pixels;
    size_t pixel_bytes;
    FILE *file;
    if (argc != 3) {
        fprintf(stderr, "usage: %s imagedata.dat ipv4\n", argv[0]);
        return 2;
    }
    file = fopen(argv[1], "r+b");
    if (!file) return 3;
    if (fseek(file, (long)(LOGO_INDEX * ENTRY_SIZE), SEEK_SET) != 0 ||
        fread(entry, 1, sizeof(entry), file) != sizeof(entry)) return 4;
    width = read_le16(entry + 4);
    height = read_le16(entry + 6);
    data_offset = read_le32(entry + 32);
    if (width != 560 || height != 92 || read_le32(entry + 24) != 2) return 5;
    pixel_bytes = (size_t)width * height * 2u;
    pixels = (uint16_t *)malloc(pixel_bytes);
    if (!pixels) return 6;
    if (fseek(file, (long)data_offset, SEEK_SET) != 0 || fread(pixels, 1, pixel_bytes, file) != pixel_bytes) return 7;
    for (int y = 52; y < 86; ++y)
        for (int x = 108; x < 552; ++x)
            pixels[y * width + x] = 0x0000;
    {
        int text_width = (int)strlen(argv[2]) * 18;
        int start_x = 112 + (438 - text_width) / 2;
        draw_text(pixels, width, height, start_x, 58, argv[2]);
    }
    if (fseek(file, (long)data_offset, SEEK_SET) != 0 || fwrite(pixels, 1, pixel_bytes, file) != pixel_bytes) return 8;
    free(pixels);
    fclose(file);
    printf("GUI_IP_PATCHED address=%s entry=%u offset=%u\n", argv[2], LOGO_INDEX, data_offset);
    return 0;
}
