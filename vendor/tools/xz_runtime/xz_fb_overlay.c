// SPDX-License-Identifier: MPL-2.0
// Volatile i.MX6 foreground-framebuffer control for XDJ-XZ experiments.

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct mxcfb_gbl_alpha {
    int enable;
    int alpha;
};

struct mxcfb_color_key {
    int enable;
    uint32_t color_key;
};

struct mxcfb_pos {
    uint16_t x;
    uint16_t y;
};

#define MXCFB_SET_GBL_ALPHA _IOW('F', 0x21, struct mxcfb_gbl_alpha)
#define MXCFB_SET_CLR_KEY _IOW('F', 0x22, struct mxcfb_color_key)
#define MXCFB_SET_OVERLAY_POS _IOWR('F', 0x24, struct mxcfb_pos)

static int run_ioctl(int fd, unsigned long request, void *value, const char *name) {
    int result = ioctl(fd, request, value);
    if (result < 0) {
        fprintf(stderr, "%s failed: errno=%d %s\n", name, errno, strerror(errno));
    } else {
        printf("%s ok\n", name);
    }
    return result;
}

static void print_state(int fd) {
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0) {
        printf(
            "var xres=%u yres=%u virtual=%ux%u offset=%u,%u bpp=%u activate=0x%x\n",
            var.xres, var.yres, var.xres_virtual, var.yres_virtual,
            var.xoffset, var.yoffset, var.bits_per_pixel, var.activate
        );
    } else {
        fprintf(stderr, "FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0) {
        printf(
            "fix id=%s smem_len=%u line_length=%u visual=%u type=%u\n",
            fix.id, fix.smem_len, fix.line_length, fix.visual, fix.type
        );
    } else {
        fprintf(stderr, "FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
    }
}

int main(int argc, char **argv) {
    const char *command = argc > 1 ? argv[1] : "state";
    const char *device = argc > 2 ? argv[2] : "/dev/fb1";
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", device, strerror(errno));
        return 2;
    }

    print_state(fd);
    if (strcmp(command, "state") == 0) {
        close(fd);
        return 0;
    }

    if (strcmp(command, "enable") == 0 || strcmp(command, "enable-keyed") == 0) {
        struct mxcfb_pos pos = { .x = 0, .y = 0 };
        struct mxcfb_color_key key = {
            .enable = strcmp(command, "enable-keyed") == 0,
            .color_key = 0,
        };
        struct mxcfb_gbl_alpha alpha = { .enable = 1, .alpha = 255 };
        int blank = FB_BLANK_UNBLANK;
        run_ioctl(fd, MXCFB_SET_OVERLAY_POS, &pos, "MXCFB_SET_OVERLAY_POS");
        run_ioctl(fd, MXCFB_SET_CLR_KEY, &key, "MXCFB_SET_CLR_KEY");
        run_ioctl(fd, MXCFB_SET_GBL_ALPHA, &alpha, "MXCFB_SET_GBL_ALPHA");
        run_ioctl(fd, FBIOBLANK, &blank, "FBIOBLANK(UNBLANK)");
        print_state(fd);
        close(fd);
        return 0;
    }

    if (strcmp(command, "page1") == 0) {
        struct fb_var_screeninfo var;
        struct mxcfb_pos pos = { .x = 0, .y = 0 };
        struct mxcfb_color_key key = { .enable = 0, .color_key = 0 };
        struct mxcfb_gbl_alpha alpha = { .enable = 1, .alpha = 255 };
        int blank = FB_BLANK_UNBLANK;
        memset(&var, 0, sizeof(var));
        run_ioctl(fd, MXCFB_SET_OVERLAY_POS, &pos, "MXCFB_SET_OVERLAY_POS");
        run_ioctl(fd, MXCFB_SET_CLR_KEY, &key, "MXCFB_SET_CLR_KEY");
        run_ioctl(fd, MXCFB_SET_GBL_ALPHA, &alpha, "MXCFB_SET_GBL_ALPHA");
        run_ioctl(fd, FBIOBLANK, &blank, "FBIOBLANK(UNBLANK)");
        if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0) {
            var.yoffset = 480;
            var.activate = FB_ACTIVATE_VBL;
            run_ioctl(fd, FBIOPAN_DISPLAY, &var, "FBIOPAN_DISPLAY(page1)");
        } else {
            fprintf(stderr, "FBIOGET_VSCREENINFO before pan failed: %s\n", strerror(errno));
        }
        print_state(fd);
        close(fd);
        return 0;
    }

    if (strcmp(command, "disable") == 0) {
        struct mxcfb_gbl_alpha alpha = { .enable = 1, .alpha = 0 };
        int blank = FB_BLANK_NORMAL;
        run_ioctl(fd, MXCFB_SET_GBL_ALPHA, &alpha, "MXCFB_SET_GBL_ALPHA(0)");
        run_ioctl(fd, FBIOBLANK, &blank, "FBIOBLANK(NORMAL)");
        print_state(fd);
        close(fd);
        return 0;
    }

    fprintf(stderr, "usage: %s [state|enable|enable-keyed|page1|disable] [/dev/fbN]\n", argv[0]);
    close(fd);
    return 2;
}
