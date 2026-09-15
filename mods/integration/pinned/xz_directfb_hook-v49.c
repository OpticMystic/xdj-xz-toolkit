// SPDX-License-Identifier: MPL-2.0
// Reboot-reversible in-process DirectFB overlay seam for XDJ-XZ firmware 1.26.

#define _GNU_SOURCE
#include <directfb.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/fb.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef DFBResult (*DirectFBCreateFn)(IDirectFB **ret_interface);
typedef DFBResult (*CreateSurfaceFn)(
    IDirectFB *thiz,
    const DFBSurfaceDescription *desc,
    IDirectFBSurface **ret_interface
);
typedef DFBResult (*GetDisplayLayerFn)(
    IDirectFB *thiz,
    DFBDisplayLayerID id,
    IDirectFBDisplayLayer **ret_interface
);
typedef DFBResult (*GetLayerSurfaceFn)(
    IDirectFBDisplayLayer *thiz,
    IDirectFBSurface **ret_interface
);
typedef DFBResult (*CreateInputEventBufferFn)(
    IDirectFB *thiz,
    DFBInputDeviceCapabilities caps,
    DFBBoolean global,
    IDirectFBEventBuffer **buffer
);
typedef DFBResult (*CreateEventBufferFn)(
    IDirectFB *thiz,
    IDirectFBEventBuffer **buffer
);
typedef DFBResult (*GetInputDeviceFn)(
    IDirectFB *thiz,
    DFBInputDeviceID id,
    IDirectFBInputDevice **device
);
typedef DFBResult (*InputCreateEventBufferFn)(
    IDirectFBInputDevice *thiz,
    IDirectFBEventBuffer **buffer
);
typedef DFBResult (*GetInputEventFn)(
    IDirectFBEventBuffer *thiz,
    DFBEvent *event
);
typedef int (*OpenFn)(const char *path, int flags, ...);
typedef int (*OpenAtFn)(int dirfd, const char *path, int flags, ...);
typedef ssize_t (*ReadFn)(int fd, void *buffer, size_t count);
typedef ssize_t (*WriteFn)(int fd, const void *buffer, size_t count);
typedef int (*IoctlFn)(int fd, unsigned long request, void *argument);
typedef DFBResult (*FlipFn)(
    IDirectFBSurface *thiz,
    const DFBRegion *region,
    DFBSurfaceFlipFlags flags
);

static CreateSurfaceFn original_create_surface;
static GetDisplayLayerFn original_get_display_layer;
static GetLayerSurfaceFn original_get_layer_surface;
static CreateInputEventBufferFn original_create_input_event_buffer;
static CreateEventBufferFn original_create_event_buffer;
static GetInputDeviceFn original_get_input_device;
static InputCreateEventBufferFn original_input_create_event_buffer;
static GetInputEventFn original_get_input_event;
static OpenFn original_open;
static OpenFn original_open64;
static OpenAtFn original_openat;
static ReadFn original_read;
static WriteFn original_write;
static IoctlFn original_ioctl;
static FlipFn original_primary_flip;
static IDirectFBSurface *primary_surface;
static IDirectFB *directfb_interface;
static int directfb_suspended;
static int logged_overlay;
static int primary_poll_started;
static int listener_started;
static int discovery_started;
static volatile uint32_t flip_count;
static volatile uint32_t filmstrip_draw_count;
static volatile uint32_t tick_count;
static volatile uint32_t preview_draw_count;
static volatile uint32_t preview_frame_count;
static volatile uint32_t framebuffer_present_count;
static struct timeval stats_window_started;
static int framebuffer_presenter_started;
static int primary_60hz_presenter_started;
static pthread_mutex_t primary_present_mutex = PTHREAD_MUTEX_INITIALIZER;

#define VJFS_PORT 50005
#define VJFS_MAX_WIDTH 800
#define VJFS_MAX_HEIGHT 480
#define VJFS_MAX_BYTES (VJFS_MAX_WIDTH * VJFS_MAX_HEIGHT * 2)
/* Hold the overlay through a stalled second of network input rather than
 * blinking the stock UI back on between packets. */
#define VJFS_OVERLAY_GRACE_MS 2500u
#define VJFS_MAX_ASSET_WIDTH 4096
#define VJFS_MAX_ASSET_HEIGHT 120
#define VJFS_MAX_ASSET_BYTES (VJFS_MAX_ASSET_WIDTH * VJFS_MAX_ASSET_HEIGHT * 2)
#define VJFS_MAX_PREVIEW_WIDTH 400
#define VJFS_MAX_PREVIEW_HEIGHT 225
#define VJFS_MAX_PREVIEW_BYTES (VJFS_MAX_PREVIEW_WIDTH * VJFS_MAX_PREVIEW_HEIGHT * 2)
#define VJFS_PREVIEW_DEST_WIDTH 440
#define VJFS_PREVIEW_DEST_HEIGHT 248
#define VJFS_PREVIEW_REGION_BOTTOM 300

struct __attribute__((packed)) vjfs_header {
    char magic[4];
    uint8_t version;
    uint8_t deck;
    uint16_t flags;
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    uint32_t payload_length;
    float playhead;
};

struct __attribute__((packed)) vjte_event {
    char magic[4];
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t sequence;
    uint16_t x;
    uint16_t y;
    int32_t value;
};

static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t latest_frame[VJFS_MAX_BYTES];
static uint8_t incoming_frame[VJFS_MAX_BYTES];
static uint8_t filmstrip_asset[VJFS_MAX_ASSET_BYTES];
static uint8_t incoming_asset[VJFS_MAX_ASSET_BYTES];
static uint8_t preview_frame[VJFS_MAX_PREVIEW_BYTES];
static uint8_t previous_preview_frame[VJFS_MAX_PREVIEW_BYTES];
static uint8_t incoming_preview[VJFS_MAX_PREVIEW_BYTES];
static uint16_t blended_preview[VJFS_MAX_PREVIEW_WIDTH * VJFS_MAX_PREVIEW_HEIGHT];
static uint8_t composite_frame[VJFS_MAX_BYTES];
static uint32_t composite_base_sequence = UINT32_MAX;
static uint32_t latest_frame_length;
static uint16_t latest_width;
static uint16_t latest_height;
static uint16_t latest_x;
static uint16_t latest_y;
static uint32_t latest_sequence;
static int latest_visible;
/* Flag bit 2 on a base frame: this frame owns the whole surface, so the cached
 * preview and filmstrip regions must not be drawn over it. The clip gallery is
 * a full-screen view, and without this the deck keeps compositing the last
 * video frame and strip it received into the middle of it. */
#define VJFS_FLAG_EXCLUSIVE 4u
static int latest_exclusive;
static struct timeval latest_received_at;
static uint16_t asset_width;
static uint16_t asset_height;
static uint16_t asset_top;
static int asset_valid;
static uint16_t latest_scroll_px;
static uint16_t previous_scroll_px;
static float latest_scroll_px_f;
static float previous_scroll_px_f;
static float scroll_velocity_px_ms;
static int tick_valid;
static struct timeval latest_tick_received_at;
static uint16_t preview_width;
static uint16_t preview_height;
static uint16_t preview_x;
static uint16_t preview_y;
static uint16_t preview_x_map[VJFS_PREVIEW_DEST_WIDTH];
static uint16_t preview_y_map[VJFS_PREVIEW_DEST_HEIGHT];
static int preview_valid;
static struct timeval latest_preview_received_at;
static int previous_preview_valid;
static float preview_frame_interval_ms = 41.667f;
static struct timeval last_present_started;
static float present_interval_min_ms;
static float present_interval_max_ms;
/* Milliseconds since boot, restamped by every accepted VJ packet. The overlay
 * gate reads this without frame_mutex: a torn or stale read costs one frame,
 * whereas the old trylock handed the entire screen back to the stock UI every
 * time the receive thread happened to hold the lock. */
static volatile uint32_t last_activity_ms;

static volatile int vjfs_client_fd = -1;
static volatile uint32_t touch_sequence;
static volatile int touch_x = -1;
static volatile int touch_y = -1;
static volatile int touch_down;
static volatile int raw_touch_fd = -1;
/* The jog-wheel displays are not framebuffers: rbp drives them over SPI links
 * to sub-microcontrollers. Nothing documents that framing, so the first step to
 * drawing on them is a bounded capture of what rbp already sends. */
#define XZ_SUBUCOM_TRACE_LIMIT 30
#define SUBUCOM_TRANSFER_IOCTL 0x40107000ul
static volatile int subucom_fd_a = -1;
static volatile int subucom_fd_b = -1;
static volatile int subucom_traced;
/* Serato can set the jog-wheel logo, and the community protocol notes put that
 * behind an authenticated HID channel with an extended header for large
 * transfers. On this side of the wire that traffic lands on /dev/hidg0, so a
 * bounded trace here is what will show the upload the first time a host does
 * it. Nothing on the SPI links carried it. */
#define XZ_HIDG_TRACE_LIMIT 24
static volatile int hidg_fd = -1;
static volatile int hidg_traced;

/* AK4187 word 1 decreases from left to right; word 2 increases downward.
 * Calibrated from half-tempo, TAG and pad-8 center taps on the 800x480 panel. */
#define XZ_TOUCH_RAW_TOP 1093
#define XZ_TOUCH_RAW_BOTTOM 3970
#define XZ_TOUCH_RAW_LEFT 4019
#define XZ_TOUCH_RAW_RIGHT 772

#define XZ_RBP_PRIMARY_SURFACE_GLOBAL ((volatile IDirectFBSurface **)0x1c8a1d4u)

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

#define MXCFB_WAIT_FOR_VSYNC _IOW('F', 0x20, uint32_t)
#define MXCFB_SET_GBL_ALPHA _IOW('F', 0x21, struct mxcfb_gbl_alpha)
#define MXCFB_SET_CLR_KEY _IOW('F', 0x22, struct mxcfb_color_key)
#define MXCFB_SET_OVERLAY_POS _IOWR('F', 0x24, struct mxcfb_pos)

static void send_touch_event(uint8_t type, int x, int y, int value);

static void log_line(const char *line) {
    int fd = open("/tmp/xz_directfb_hook.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;
    /* /tmp shares the firmware's 512 KiB writable filesystem. */
    if (lseek(fd, 0, SEEK_END) >= 64 * 1024 && ftruncate(fd, 0) < 0) {
        close(fd);
        return;
    }
    write(fd, line, strlen(line));
    write(fd, "\n", 1);
    close(fd);
}

static void capture_touch_fd(const char *path, int fd) {
    if (fd < 0) return;
    if (strstr(path, "ak4187") || strstr(path, "ti_tsc")) {
        raw_touch_fd = fd;
        log_line("TOUCH_DEVICE_HOOKED");
    } else if (strstr(path, "subucom_spi3.0")) {
        subucom_fd_a = fd;
        log_line("SUBUCOM_A_HOOKED");
    } else if (strstr(path, "subucom_spi3.1")) {
        subucom_fd_b = fd;
        log_line("SUBUCOM_B_HOOKED");
    } else if (strstr(path, "hidg")) {
        hidg_fd = fd;
        log_line("HIDG_HOOKED");
    }
}

static int call_open(OpenFn function, const char *path, int flags, va_list *args) {
    mode_t mode = 0;
    int fd;
    if (flags & O_CREAT) {
        mode = (mode_t)va_arg(*args, int);
        fd = function(path, flags, mode);
    } else {
        fd = function(path, flags);
    }
    capture_touch_fd(path, fd);
    return fd;
}

int open(const char *path, int flags, ...) {
    va_list args;
    int result;
    if (!original_open) original_open = (OpenFn)dlsym(RTLD_NEXT, "open");
    va_start(args, flags);
    result = call_open(original_open, path, flags, &args);
    va_end(args);
    return result;
}

int open64(const char *path, int flags, ...) {
    va_list args;
    int result;
    if (!original_open64) original_open64 = (OpenFn)dlsym(RTLD_NEXT, "open64");
    va_start(args, flags);
    result = call_open(original_open64, path, flags, &args);
    va_end(args);
    return result;
}

int openat(int dirfd, const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0;
    int fd;
    if (!original_openat) original_openat = (OpenAtFn)dlsym(RTLD_NEXT, "openat");
    va_start(args, flags);
    if (flags & O_CREAT) {
        mode = (mode_t)va_arg(args, int);
        fd = original_openat(dirfd, path, flags, mode);
    } else {
        fd = original_openat(dirfd, path, flags);
    }
    va_end(args);
    capture_touch_fd(path, fd);
    return fd;
}

ssize_t read(int fd, void *buffer, size_t count) {
    ssize_t result;
    if (!original_read) original_read = (ReadFn)dlsym(RTLD_NEXT, "read");
    result = original_read(fd, buffer, count);
    if (fd == hidg_fd && result > 8 && hidg_traced < XZ_HIDG_TRACE_LIMIT) {
        const uint8_t *bytes = (const uint8_t *)buffer;
        char line[192];
        int offset;
        size_t index;
        size_t shown = (size_t)result < 32 ? (size_t)result : 32;
        hidg_traced++;
        offset = snprintf(line, sizeof(line), "HIDG_RX len=%d |", (int)result);
        for (index = 0; index < shown && offset > 0 && offset < (int)sizeof(line) - 4; ++index) {
            offset += snprintf(line + offset, sizeof(line) - offset, " %02x", bytes[index]);
        }
        log_line(line);
    }
    if (fd == raw_touch_fd && result >= 6) {
        const uint8_t *bytes = (const uint8_t *)buffer;
        int pressed = bytes[0] != 0;
        int raw_horizontal = (int)bytes[2] | ((int)bytes[3] << 8);
        int raw_vertical = (int)bytes[4] | ((int)bytes[5] << 8);
        int x = (raw_horizontal - XZ_TOUCH_RAW_LEFT) * 799 /
                (XZ_TOUCH_RAW_RIGHT - XZ_TOUCH_RAW_LEFT);
        int y = (raw_vertical - XZ_TOUCH_RAW_TOP) * 479 /
                (XZ_TOUCH_RAW_BOTTOM - XZ_TOUCH_RAW_TOP);
        if (x < 0) x = 0;
        if (x > 799) x = 799;
        if (y < 0) y = 0;
        if (y > 479) y = 479;
        touch_x = x;
        touch_y = y;
        /* The 6-byte layout above was inferred, not documented. Logging the
         * raw report on every press/release edge is what makes a wrong guess
         * visible instead of silently mapping touches to the wrong pixel. */
        if (pressed != touch_down) {
            char raw_line[160];
            snprintf(raw_line, sizeof(raw_line),
                     "TOUCH_RAW n=%d %02x %02x %02x %02x %02x %02x -> %s %d,%d",
                     (int)result, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                     pressed ? "down" : "up", x, y);
            log_line(raw_line);
        }
        if (pressed && !touch_down) send_touch_event(1, x, y, 1);
        else if (!pressed && touch_down) send_touch_event(2, x, y, 0);
        touch_down = pressed;
    }
    return result;
}

ssize_t write(int fd, const void *buffer, size_t count) {
    if (!original_write) original_write = (WriteFn)dlsym(RTLD_NEXT, "write");
    if ((fd == subucom_fd_a || fd == subucom_fd_b) && subucom_traced < XZ_SUBUCOM_TRACE_LIMIT) {
        const uint8_t *bytes = (const uint8_t *)buffer;
        size_t shown = count < 16 ? count : 16;
        size_t index;
        char line[160];
        int offset;
        subucom_traced++;
        offset = snprintf(line, sizeof(line), "SUBUCOM_TX %c len=%u",
                          fd == subucom_fd_a ? 'a' : 'b', (unsigned)count);
        for (index = 0; index < shown && offset > 0 && offset < (int)sizeof(line) - 4; ++index) {
            offset += snprintf(line + offset, sizeof(line) - offset, " %02x", bytes[index]);
        }
        log_line(line);
    }
    return original_write(fd, buffer, count);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    void *argument;
    if (!original_ioctl) original_ioctl = (IoctlFn)dlsym(RTLD_NEXT, "ioctl");
    va_start(args, request);
    argument = va_arg(args, void *);
    va_end(args);
    /* The sub-MCU links carry a 116-byte and a 20-byte control frame every
     * tick, which drowns anything interesting. A jog-display image would be a
     * large, rare transfer, so only those are worth a log line. */
    if ((fd == subucom_fd_a || fd == subucom_fd_b)
        && request == SUBUCOM_TRANSFER_IOCTL
        && argument
        && subucom_traced < XZ_SUBUCOM_TRACE_LIMIT) {
        const uint32_t *fields = (const uint32_t *)argument;
        if (fields[1] > 512u) {
            const uint8_t *payload = (const uint8_t *)(uintptr_t)fields[2];
            char line[256];
            int offset;
            size_t index;
            subucom_traced++;
            offset = snprintf(line, sizeof(line), "SUBUCOM_BULK %c len=%u f0=%08x |",
                              fd == subucom_fd_a ? 'a' : 'b', fields[1], fields[0]);
            for (index = 0; index < 32 && offset > 0 && offset < (int)sizeof(line) - 4; ++index) {
                offset += snprintf(line + offset, sizeof(line) - offset, " %02x", payload[index]);
            }
            log_line(line);
        }
    }
    return original_ioctl(fd, request, argument);
}

static void write_runtime_stats_if_due(void) {
    struct timeval now;
    long elapsed_ms;
    char line[512];
    int length;
    int fd;
    gettimeofday(&now, NULL);
    if (stats_window_started.tv_sec == 0) {
        stats_window_started = now;
        return;
    }
    elapsed_ms = (now.tv_sec - stats_window_started.tv_sec) * 1000L +
                 (now.tv_usec - stats_window_started.tv_usec) / 1000L;
    if (elapsed_ms < 1000) return;
    length = snprintf(
        line,
        sizeof(line),
        "window_ms=%ld flip_hz=%.1f framebuffer_present_hz=%.1f present_interval_min_ms=%.2f present_interval_max_ms=%.2f preview_draw_hz=%.1f preview_frame_hz=%.1f filmstrip_draw_hz=%.1f tick_hz=%.1f preview=%ux%u asset=%ux%u scroll=%.2f\n",
        elapsed_ms,
        (double)flip_count * 1000.0 / (double)elapsed_ms,
        (double)framebuffer_present_count * 1000.0 / (double)elapsed_ms,
        present_interval_min_ms,
        present_interval_max_ms,
        (double)preview_draw_count * 1000.0 / (double)elapsed_ms,
        (double)preview_frame_count * 1000.0 / (double)elapsed_ms,
        (double)filmstrip_draw_count * 1000.0 / (double)elapsed_ms,
        (double)tick_count * 1000.0 / (double)elapsed_ms,
        preview_width,
        preview_height,
        asset_width,
        asset_height,
        latest_scroll_px_f
    );
    if (length < 0) return;
    if ((size_t)length >= sizeof(line)) length = (int)sizeof(line) - 1;
    fd = open("/tmp/xz_directfb_stats", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        write(fd, line, (size_t)length);
        close(fd);
    }
    flip_count = 0;
    filmstrip_draw_count = 0;
    tick_count = 0;
    preview_draw_count = 0;
    preview_frame_count = 0;
    framebuffer_present_count = 0;
    present_interval_min_ms = 0.0f;
    present_interval_max_ms = 0.0f;
    stats_window_started = now;
}

static void put_pixel(uint16_t *row, int x, uint16_t value) {
    row[x] = value;
}

static int frame_is_fresh(void) {
    struct timeval now;
    long age_ms;
    if (!latest_visible) return 0;
    gettimeofday(&now, NULL);
    age_ms = (now.tv_sec - latest_received_at.tv_sec) * 1000L +
             (now.tv_usec - latest_received_at.tv_usec) / 1000L;
    return age_ms >= 0 && age_ms <= 2500;
}

/* gettimeofday only: this firmware's glibc predates the GLIBC_2.17
 * clock_gettime symbol, and linking it makes rbp refuse to start. A backwards
 * wall-clock step is treated as "active" by the caller, so a clock adjustment
 * cannot blink the stock UI back on. */
static uint32_t wallclock_ms(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32_t)((uint32_t)now.tv_sec * 1000u + (uint32_t)(now.tv_usec / 1000));
}

static void mark_overlay_activity(void) {
    uint32_t stamp = wallclock_ms();
    last_activity_ms = stamp ? stamp : 1u;
}

static int overlay_is_active(void) {
    uint32_t last;
    uint32_t now;
    if (access("/tmp/xz_overlay_enabled", F_OK) == 0) return 1;
    last = last_activity_ms;
    if (last == 0u) return 0;
    now = wallclock_ms();
    if (now < last) return 1;
    return (now - last) <= VJFS_OVERLAY_GRACE_MS;
}

static void draw_latest_frame(uint8_t *pixels, int pitch, int surface_width, int surface_height) {
    int width;
    int height;
    int x;
    int y;
    if (pthread_mutex_trylock(&frame_mutex) != 0) return;
    if (!frame_is_fresh()) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    width = latest_width;
    height = latest_height;
    x = latest_x;
    y = latest_y;
    if (x + width > surface_width) width = surface_width - x;
    if (y + height > surface_height) height = surface_height - y;
    if (x >= 0 && y >= 0 && width > 0 && height > 0) {
        for (int row = 0; row < height; ++row) {
            memcpy(
                pixels + (y + row) * pitch + x * 2,
                latest_frame + row * latest_width * 2,
                (size_t)width * 2
            );
        }
    }
    pthread_mutex_unlock(&frame_mutex);
}

static void draw_cached_filmstrip(uint8_t *pixels, int pitch, int surface_width, int surface_height) {
    uint16_t width;
    uint16_t height;
    uint16_t top;
    int scroll;
    struct timeval now;
    long age_ms;
    if (pthread_mutex_trylock(&frame_mutex) != 0) return;
    if (!asset_valid || !tick_valid) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    gettimeofday(&now, NULL);
    age_ms = (now.tv_sec - latest_tick_received_at.tv_sec) * 1000L +
             (now.tv_usec - latest_tick_received_at.tv_usec) / 1000L;
    if (age_ms < 0 || age_ms > 2500) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    width = asset_width;
    height = asset_height;
    top = asset_top;
    scroll = (int)(latest_scroll_px_f + scroll_velocity_px_ms * (float)age_ms + 0.5f);
    if (width <= (uint16_t)surface_width) scroll = 0;
    else if (scroll < 0) scroll = 0;
    else if (scroll > (int)width - surface_width) scroll = (int)width - surface_width;
    if (top >= (uint16_t)surface_height || height == 0) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    if (top + height > (uint16_t)surface_height) height = (uint16_t)surface_height - top;
    for (int row = 0; row < height; ++row) {
        int copy_width = surface_width;
        if (scroll + copy_width > width) copy_width = width - scroll;
        if (copy_width > 0) {
            memcpy(
                pixels + (top + row) * pitch,
                filmstrip_asset + (row * width + scroll) * 2,
                (size_t)copy_width * 2
            );
        }
    }
    filmstrip_draw_count += 1;
    pthread_mutex_unlock(&frame_mutex);
}

static uint16_t blend_rgb565(uint16_t from, uint16_t to, int phase256) {
    int inverse = 256 - phase256;
    int from_r = (from >> 11) & 0x1f;
    int from_g = (from >> 5) & 0x3f;
    int from_b = from & 0x1f;
    int to_r = (to >> 11) & 0x1f;
    int to_g = (to >> 5) & 0x3f;
    int to_b = to & 0x1f;
    int red = (from_r * inverse + to_r * phase256) >> 8;
    int green = (from_g * inverse + to_g * phase256) >> 8;
    int blue = (from_b * inverse + to_b * phase256) >> 8;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void draw_cached_preview(uint8_t *pixels, int pitch, int surface_width, int surface_height) {
    struct timeval now;
    long age_ms;
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    const uint16_t *scaled_source;
    if (pthread_mutex_trylock(&frame_mutex) != 0) return;
    if (!preview_valid) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    gettimeofday(&now, NULL);
    age_ms = (now.tv_sec - latest_preview_received_at.tv_sec) * 1000L +
             (now.tv_usec - latest_preview_received_at.tv_usec) / 1000L;
    if (age_ms < 0 || age_ms > 1000) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    width = preview_width;
    height = preview_height;
    x = preview_x;
    y = preview_y;
    if (x + VJFS_PREVIEW_DEST_WIDTH > (uint16_t)surface_width ||
        y + VJFS_PREVIEW_DEST_HEIGHT > (uint16_t)surface_height) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    if (previous_preview_valid) {
        int phase256 = (int)((float)age_ms * 256.0f / preview_frame_interval_ms);
        int count = width * height;
        const uint16_t *previous = (const uint16_t *)previous_preview_frame;
        const uint16_t *current = (const uint16_t *)preview_frame;
        if (phase256 < 0) phase256 = 0;
        if (phase256 > 256) phase256 = 256;
        for (int index = 0; index < count; ++index) {
            blended_preview[index] = blend_rgb565(previous[index], current[index], phase256);
        }
        scaled_source = blended_preview;
    } else {
        scaled_source = (const uint16_t *)preview_frame;
    }
    for (int dest_y = 0; dest_y < VJFS_PREVIEW_DEST_HEIGHT; ++dest_y) {
        uint16_t *row = (uint16_t *)(pixels + (y + dest_y) * pitch) + x;
        const uint16_t *source = scaled_source + preview_y_map[dest_y] * width;
        for (int dest_x = 0; dest_x < VJFS_PREVIEW_DEST_WIDTH; ++dest_x) {
            row[dest_x] = source[preview_x_map[dest_x]];
        }
    }
    preview_draw_count += 1;
    pthread_mutex_unlock(&frame_mutex);
}

static void draw_composite_frame(void) {
    if (pthread_mutex_trylock(&frame_mutex) == 0) {
        if (frame_is_fresh() && latest_sequence != composite_base_sequence) {
            int width = latest_width;
            int height = latest_height;
            int x = latest_x;
            int y = latest_y;
            if (x + width > 800) width = 800 - x;
            if (y + height > 480) height = 480 - y;
            if (x >= 0 && y >= 0 && width > 0 && height > 0) {
                for (int row = 0; row < height; ++row) {
                    memcpy(
                        composite_frame + (y + row) * 1600 + x * 2,
                        latest_frame + row * latest_width * 2,
                        (size_t)width * 2
                    );
                }
                composite_base_sequence = latest_sequence;
            }
        }
        pthread_mutex_unlock(&frame_mutex);
    }
    if (!latest_exclusive) {
        draw_cached_preview(composite_frame, 1600, 800, 480);
        draw_cached_filmstrip(composite_frame, 1600, 800, 480);
    }
}

static void *framebuffer_presenter(void *unused) {
    int fd;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    uint8_t *mapping;
    size_t mapping_length;
    (void)unused;
    {
        struct sched_param scheduling = { .sched_priority = 20 };
        cpu_set_t affinity;
        int priority_ok = pthread_setschedparam(pthread_self(), SCHED_FIFO, &scheduling) == 0;
        CPU_ZERO(&affinity);
        CPU_SET(3, &affinity);
        int affinity_ok = pthread_setaffinity_np(pthread_self(), sizeof(affinity), &affinity) == 0;
        if (priority_ok && affinity_ok) log_line("FRAMEBUFFER_PRESENTER_RT priority=20 cpu=3");
        else log_line("FRAMEBUFFER_PRESENTER_RT unavailable");
    }
    fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        if (fd >= 0) close(fd);
        log_line("ERROR framebuffer presenter setup failed");
        return NULL;
    }
    if (var.bits_per_pixel != 16 || var.xres < 800 || var.yres < 480 ||
        var.yres_virtual < 480 || fix.line_length < 1600 ||
        fix.smem_len < (uint32_t)fix.line_length * 480u) {
        close(fd);
        log_line("ERROR framebuffer presenter incompatible mode");
        return NULL;
    }
    mapping_length = fix.smem_len;
    mapping = mmap(NULL, mapping_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        close(fd);
        log_line("ERROR framebuffer presenter mmap failed");
        return NULL;
    }
    log_line("FRAMEBUFFER_BG_SCANOUT_PRESENTER_STARTED target_hz=60 mode=800x480x1");
    for (;;) {
        struct timeval started;
        struct timeval finished;
        long elapsed_us;
        gettimeofday(&started, NULL);
        if (overlay_is_active()) {
            if (0 && !directfb_suspended && directfb_interface) {
                directfb_interface->WaitIdle(directfb_interface);
                if (directfb_interface->Suspend(directfb_interface) == DFB_OK) {
                    directfb_suspended = 1;
                    log_line("DIRECTFB_SUSPENDED_FOR_EXCLUSIVE_SCANOUT");
                } else {
                    log_line("ERROR DirectFB suspend failed");
                }
            }
            if (last_present_started.tv_sec != 0) {
                float interval_ms = (float)((started.tv_sec - last_present_started.tv_sec) * 1000L) +
                                    (float)(started.tv_usec - last_present_started.tv_usec) / 1000.0f;
                if (interval_ms >= 0.0f && interval_ms < 1000.0f) {
                    if (present_interval_min_ms == 0.0f || interval_ms < present_interval_min_ms) present_interval_min_ms = interval_ms;
                    if (interval_ms > present_interval_max_ms) present_interval_max_ms = interval_ms;
                }
            }
            last_present_started = started;
            draw_composite_frame();
            for (int row = 0; row < 480; ++row) {
                memcpy(
                    mapping + (size_t)row * fix.line_length,
                    composite_frame + (size_t)row * 1600u,
                    1600u
                );
            }
            framebuffer_present_count += 1;
            write_runtime_stats_if_due();
        } else if (0 && directfb_suspended && directfb_interface) {
            if (directfb_interface->Resume(directfb_interface) == DFB_OK) {
                directfb_suspended = 0;
                log_line("DIRECTFB_RESUMED_AFTER_EXCLUSIVE_SCANOUT");
            } else {
                log_line("ERROR DirectFB resume failed");
            }
        }
        gettimeofday(&finished, NULL);
        elapsed_us = (finished.tv_sec - started.tv_sec) * 1000000L +
                     (finished.tv_usec - started.tv_usec);
        if (elapsed_us < 16667) usleep((useconds_t)(16667 - elapsed_us));
    }
    munmap(mapping, mapping_length);
    close(fd);
    return NULL;
}

static void draw_probe_overlay(IDirectFBSurface *surface) {
    int width = 0;
    int height = 0;
    int pitch = 0;
    void *pixels = NULL;
    DFBSurfacePixelFormat format = DSPF_UNKNOWN;
    const int top = 176;
    const int overlay_height = 120;
    const uint16_t probe_color = (63u << 5) | 31u;  // cyan, visually distinct from XZ amber

    if (!overlay_is_active()) return;
    if (surface->GetSize(surface, &width, &height) != DFB_OK) return;
    if (surface->GetPixelFormat(surface, &format) != DFB_OK || format != DSPF_RGB16) return;
    if (width < 16 || height < top + overlay_height) return;
    if (surface->Lock(surface, DSLF_WRITE, &pixels, &pitch) != DFB_OK || !pixels) return;

    draw_latest_frame((uint8_t *)pixels, pitch, width, height);
    if (!latest_exclusive) {
        draw_cached_preview((uint8_t *)pixels, pitch, width, height);
        draw_cached_filmstrip((uint8_t *)pixels, pitch, width, height);
    }

    if (access("/tmp/xz_overlay_enabled", F_OK) == 0) {
        for (int y = 0; y < overlay_height; ++y) {
            uint16_t *row = (uint16_t *)((uint8_t *)pixels + (top + y) * pitch);
            if (y < 4 || y >= overlay_height - 4) {
                for (int x = 0; x < width; ++x) put_pixel(row, x, probe_color);
            } else {
                for (int x = 0; x < 4; ++x) put_pixel(row, x, probe_color);
                for (int x = width - 4; x < width; ++x) put_pixel(row, x, probe_color);
                if (y >= 48 && y < 72) {
                    for (int x = 60; x < width - 60; ++x) put_pixel(row, x, probe_color);
                }
            }
        }
    }
    surface->Unlock(surface);
    if (!logged_overlay) {
        log_line("OVERLAY_DRAWN primary RGB16 surface");
        logged_overlay = 1;
    }
}

static DFBResult hooked_primary_flip(
    IDirectFBSurface *thiz,
    const DFBRegion *region,
    DFBSurfaceFlipFlags flags
) {
    int overlay_enabled = overlay_is_active();
    DFBResult result;
    if (framebuffer_presenter_started && overlay_enabled) {
        flip_count += 1;
        return DFB_OK;
    }
    if (primary_60hz_presenter_started && overlay_enabled) {
        /* The dedicated presenter owns full-surface flips while VJ output is
         * active. Stock rendering may continue updating the backbuffer. */
        flip_count += 1;
        return DFB_OK;
    }
    if (directfb_interface && overlay_enabled) directfb_interface->WaitIdle(directfb_interface);
    if (!framebuffer_presenter_started) draw_probe_overlay(thiz);
    if (directfb_interface && overlay_enabled) directfb_interface->WaitIdle(directfb_interface);
    // The XZ normally flips only its dirty region. Overlay pixels can lie
    // outside that region, so promote the whole backbuffer only while an
    // overlay frame is active. The stock partial-flip path is untouched when
    // the overlay is disabled.
    result = original_primary_flip(
        thiz,
        overlay_enabled ? NULL : region,
        overlay_enabled ? DSFLIP_WAITFORSYNC : flags
    );
    flip_count += 1;
    write_runtime_stats_if_due();
    return result;
}

static void *primary_60hz_presenter(void *unused) {
    void *pixels = NULL;
    int pitch = 0;
    int locked = 0;
    (void)unused;
    log_line("PRIMARY_60HZ_FRONTBUFFER_PRESENTER_STARTED target_hz=60");
    for (;;) {
        struct timeval started;
        struct timeval finished;
        long elapsed_us;
        gettimeofday(&started, NULL);
        if (overlay_is_active() && primary_surface && original_primary_flip) {
            pthread_mutex_lock(&primary_present_mutex);
            if (!locked) {
                /* A read lock maps the currently visible/front buffer on this
                 * DirectFB 1.4 flipping surface. The mapping is writable on
                 * the XZ framebuffer; keep it locked to block stock drawing
                 * and update scanout memory directly without another flip. */
                if (primary_surface->Lock(primary_surface, DSLF_READ, &pixels, &pitch) == DFB_OK && pixels) {
                    locked = 1;
                    log_line("PRIMARY_60HZ_FRONTBUFFER_LOCK_ACQUIRED");
                } else {
                    log_line("ERROR primary 60Hz exclusive lock failed");
                }
            }
            if (locked && pitch >= 1600) {
                draw_composite_frame();
                for (int row = 0; row < 480; ++row) {
                    memcpy((uint8_t *)pixels + (size_t)row * pitch,
                           composite_frame + (size_t)row * 1600u,
                           1600u);
                }
                framebuffer_present_count += 1;
                write_runtime_stats_if_due();
            }
            pthread_mutex_unlock(&primary_present_mutex);
        } else if (locked && primary_surface) {
            primary_surface->Unlock(primary_surface);
            locked = 0;
            pixels = NULL;
            pitch = 0;
            log_line("PRIMARY_60HZ_FRONTBUFFER_LOCK_RELEASED");
        }
        gettimeofday(&finished, NULL);
        elapsed_us = (finished.tv_sec - started.tv_sec) * 1000000L +
                     (finished.tv_usec - started.tv_usec);
        if (elapsed_us < 16667) usleep((useconds_t)(16667 - elapsed_us));
    }
    return NULL;
}

static int read_full(int fd, void *buffer, size_t length) {
    uint8_t *cursor = (uint8_t *)buffer;
    while (length > 0) {
        ssize_t received = recv(fd, cursor, length, 0);
        if (received == 0) return 0;
        if (received < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        cursor += received;
        length -= (size_t)received;
    }
    return 1;
}

static uint16_t rgb332_to_rgb565(uint8_t pixel) {
    uint16_t r3 = (uint16_t)((pixel >> 5) & 0x07u);
    uint16_t g3 = (uint16_t)((pixel >> 2) & 0x07u);
    uint16_t b2 = (uint16_t)(pixel & 0x03u);
    uint16_t r5 = (uint16_t)((r3 << 2) | (r3 >> 1));
    uint16_t g6 = (uint16_t)((g3 << 3) | g3);
    uint16_t b5 = (uint16_t)((b2 << 3) | (b2 << 1) | (b2 >> 1));
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void handle_vjfs_client(int client) {
    struct vjfs_header header;
    while (read_full(client, &header, sizeof(header))) {
        uint32_t expected;
        if (memcmp(header.magic, "VJVP", 4) == 0) {
            struct timeval now;
            long interval_ms;
            if ((header.version != 1 && header.version != 2) || header.width == 0 || header.height == 0 ||
                header.width > VJFS_MAX_PREVIEW_WIDTH || header.height > VJFS_MAX_PREVIEW_HEIGHT ||
                header.x + VJFS_PREVIEW_DEST_WIDTH > 800 ||
                header.y + VJFS_PREVIEW_DEST_HEIGHT > VJFS_PREVIEW_REGION_BOTTOM ||
                header.payload_length != (uint32_t)header.width * (uint32_t)header.height * (header.version == 2 ? 1u : 2u)) {
                log_line("VJFS_REJECT_BAD_PREVIEW");
                return;
            }
            expected = header.payload_length;
            if (!read_full(client, incoming_preview, expected)) {
                log_line("VJFS_REJECT_SHORT_PREVIEW");
                return;
            }
            if (header.version == 2) {
                uint32_t pixel_count = (uint32_t)header.width * (uint32_t)header.height;
                uint32_t index = pixel_count;
                while (index > 0) {
                    uint16_t expanded;
                    index -= 1;
                    expanded = rgb332_to_rgb565(incoming_preview[index]);
                    incoming_preview[index * 2u] = (uint8_t)(expanded & 0xffu);
                    incoming_preview[index * 2u + 1u] = (uint8_t)(expanded >> 8);
                }
                expected = pixel_count * 2u;
            }
            gettimeofday(&now, NULL);
            pthread_mutex_lock(&frame_mutex);
            interval_ms = (now.tv_sec - latest_preview_received_at.tv_sec) * 1000L +
                          (now.tv_usec - latest_preview_received_at.tv_usec) / 1000L;
            if (preview_valid && preview_width == header.width && preview_height == header.height) {
                memcpy(previous_preview_frame, preview_frame, expected);
                previous_preview_valid = 1;
                if (interval_ms >= 10 && interval_ms <= 250) {
                    preview_frame_interval_ms = preview_frame_interval_ms * 0.75f + (float)interval_ms * 0.25f;
                }
            } else {
                previous_preview_valid = 0;
            }
            memcpy(preview_frame, incoming_preview, expected);
            preview_width = header.width;
            preview_height = header.height;
            preview_x = header.x;
            preview_y = header.y;
            for (int map_x = 0; map_x < VJFS_PREVIEW_DEST_WIDTH; ++map_x) {
                preview_x_map[map_x] = (uint16_t)((uint32_t)map_x * header.width / VJFS_PREVIEW_DEST_WIDTH);
            }
            for (int map_y = 0; map_y < VJFS_PREVIEW_DEST_HEIGHT; ++map_y) {
                preview_y_map[map_y] = (uint16_t)((uint32_t)map_y * header.height / VJFS_PREVIEW_DEST_HEIGHT);
            }
            preview_valid = 1;
            latest_preview_received_at = now;
            preview_frame_count += 1;
            pthread_mutex_unlock(&frame_mutex);
            mark_overlay_activity();
            continue;
        }
        if (memcmp(header.magic, "VJFA", 4) == 0) {
            /* Version 2 carries RGB332, one byte per pixel, and is expanded
             * here exactly as the preview already is. Halving the strip on the
             * wire is what lets it be sent at the full display cadence over the
             * deck's 100 Mbit link. Version 1 RGB565 stays accepted. */
            int packed_asset = header.version == 2;
            uint32_t asset_bytes_per_pixel = packed_asset ? 1u : 2u;
            if ((header.version != 1 && header.version != 2) ||
                header.width == 0 || header.height == 0 ||
                header.width > VJFS_MAX_ASSET_WIDTH || header.height > VJFS_MAX_ASSET_HEIGHT ||
                header.y + header.height > 480 ||
                header.payload_length !=
                    (uint32_t)header.width * (uint32_t)header.height * asset_bytes_per_pixel) {
                log_line("VJFS_REJECT_BAD_ASSET");
                return;
            }
            expected = header.payload_length;
            if (!read_full(client, incoming_asset, expected)) {
                log_line("VJFS_REJECT_SHORT_ASSET");
                return;
            }
            pthread_mutex_lock(&frame_mutex);
            if (packed_asset) {
                uint16_t *destination = (uint16_t *)filmstrip_asset;
                for (uint32_t index = 0; index < expected; ++index) {
                    destination[index] = rgb332_to_rgb565(incoming_asset[index]);
                }
            } else {
                memcpy(filmstrip_asset, incoming_asset, expected);
            }
            asset_width = header.width;
            asset_height = header.height;
            asset_top = header.y;
            asset_valid = 1;
            pthread_mutex_unlock(&frame_mutex);
            mark_overlay_activity();
            continue;
        }
        if (memcmp(header.magic, "VJFT", 4) == 0) {
            struct timeval now;
            long dt_ms;
            float next_scroll;
            if (header.version != 1 || header.payload_length != 0 ||
                header.height == 0 || header.height > VJFS_MAX_ASSET_WIDTH ||
                header.x == 0 || header.x > VJFS_MAX_ASSET_HEIGHT || header.y + header.x > 480) {
                log_line("VJFS_REJECT_BAD_TICK");
                return;
            }
            gettimeofday(&now, NULL);
            next_scroll = (header.flags & 2u) != 0 ? header.playhead : (float)header.width;
            if (!(next_scroll >= 0.0f && next_scroll <= 65535.0f)) next_scroll = (float)header.width;
            pthread_mutex_lock(&frame_mutex);
            dt_ms = (now.tv_sec - latest_tick_received_at.tv_sec) * 1000L +
                    (now.tv_usec - latest_tick_received_at.tv_usec) / 1000L;
            previous_scroll_px = latest_scroll_px;
            previous_scroll_px_f = latest_scroll_px_f;
            if (tick_valid && dt_ms > 0 && dt_ms < 1000) {
                scroll_velocity_px_ms = (next_scroll - previous_scroll_px_f) / (float)dt_ms;
                if (scroll_velocity_px_ms > 4.0f) scroll_velocity_px_ms = 4.0f;
                if (scroll_velocity_px_ms < -4.0f) scroll_velocity_px_ms = -4.0f;
            }
            latest_scroll_px = header.width;
            latest_scroll_px_f = next_scroll;
            latest_tick_received_at = now;
            tick_valid = 1;
            tick_count += 1;
            pthread_mutex_unlock(&frame_mutex);
            mark_overlay_activity();
            continue;
        }
        if (memcmp(header.magic, "VJFS", 4) != 0 || (header.version != 1 && header.version != 2)) {
            log_line("VJFS_REJECT_BAD_MAGIC_OR_VERSION");
            return;
        }
        {
            uint32_t pixel_count = (uint32_t)header.width * (uint32_t)header.height;
            uint32_t wire_bytes = header.version == 2 ? pixel_count : pixel_count * 2u;
            expected = pixel_count * 2u;
        if (
            header.width == 0 || header.height == 0 ||
            header.width > VJFS_MAX_WIDTH || header.height > VJFS_MAX_HEIGHT ||
            header.x + header.width > 800 || header.y + header.height > 480 ||
            header.payload_length != wire_bytes || expected > VJFS_MAX_BYTES
        ) {
            log_line("VJFS_REJECT_BAD_DIMENSIONS");
            return;
        }
        if (!read_full(client, incoming_frame, wire_bytes)) {
            log_line("VJFS_REJECT_SHORT_PAYLOAD");
            return;
        }
        if (header.version == 2) {
            uint32_t index = pixel_count;
            while (index > 0) {
                uint16_t expanded;
                index -= 1;
                expanded = rgb332_to_rgb565(incoming_frame[index]);
                incoming_frame[index * 2u] = (uint8_t)(expanded & 0xffu);
                incoming_frame[index * 2u + 1u] = (uint8_t)(expanded >> 8);
            }
        }
        }
        pthread_mutex_lock(&frame_mutex);
        memcpy(latest_frame, incoming_frame, expected);
        latest_frame_length = expected;
        latest_width = header.width;
        latest_height = header.height;
        latest_x = header.x;
        latest_y = header.y;
        latest_sequence = header.sequence;
        latest_visible = (header.flags & 1u) != 0;
        latest_exclusive = (header.flags & VJFS_FLAG_EXCLUSIVE) != 0;
        gettimeofday(&latest_received_at, NULL);
        pthread_mutex_unlock(&frame_mutex);
        if (latest_visible) mark_overlay_activity();
    }
}

static void *vjfs_client_thread(void *argument) {
    int client = (int)(intptr_t)argument;
    log_line("VJFS_CLIENT_CONNECTED");
    handle_vjfs_client(client);
    close(client);
    pthread_mutex_lock(&frame_mutex);
    if (vjfs_client_fd == client) {
        vjfs_client_fd = -1;
        latest_visible = 0;
        latest_exclusive = 0;
        preview_valid = 0;
        tick_valid = 0;
        last_activity_ms = 0u;
    }
    pthread_mutex_unlock(&frame_mutex);
    log_line("VJFS_CLIENT_DISCONNECTED");
    return NULL;
}

/* Discovery opens and closes TCP without sending a frame. It must not evict
 * the display owner or clear the overlay. Peek preserves the first header for
 * handle_vjfs_client; the timeout bounds clients that connect but stay silent. */
static int vjfs_client_has_header(int client) {
    struct vjfs_header header;
    struct timeval timeout = {0, 500000};
    ssize_t received;
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) return 0;
    do {
        received = recv(client, &header, sizeof(header), MSG_PEEK | MSG_WAITALL);
    } while (received < 0 && errno == EINTR);
    timeout.tv_usec = 0;
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) return 0;
    if (received != sizeof(header)) return 0;
    if (memcmp(header.magic, "VJFT", 4) == 0) return header.version == 1;
    return (header.version == 1 || header.version == 2) &&
        (memcmp(header.magic, "VJFS", 4) == 0 ||
         memcmp(header.magic, "VJFA", 4) == 0 ||
         memcmp(header.magic, "VJVP", 4) == 0);
}

static void *vjfs_listener(void *unused) {
    int server;
    int one = 1;
    struct sockaddr_in address;
    (void)unused;
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        log_line("ERROR VJFS socket failed");
        return NULL;
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(VJFS_PORT);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 1) < 0) {
        log_line("ERROR VJFS bind/listen failed");
        close(server);
        return NULL;
    }
    log_line("VJFS_LISTENING port=50005");
    for (;;) {
        int client = accept(server, NULL, NULL);
        pthread_t client_thread;
        int previous;
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!vjfs_client_has_header(client)) {
            close(client);
            continue;
        }
        previous = vjfs_client_fd;
        if (previous >= 0 && previous != client) shutdown(previous, SHUT_RDWR);
        vjfs_client_fd = client;
        if (pthread_create(&client_thread, NULL, vjfs_client_thread, (void *)(intptr_t)client) == 0) {
            pthread_detach(client_thread);
        } else {
            vjfs_client_fd = -1;
            close(client);
            log_line("ERROR VJFS client thread failed");
        }
    }
    close(server);
    return NULL;
}

static void *vj_discovery_listener(void *unused) {
    int fd;
    int one = 1;
    struct sockaddr_in address;
    (void)unused;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(50006);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        log_line("ERROR VJ discovery bind failed");
        return NULL;
    }
    log_line("VJ_DISCOVERY_LISTENING udp=50006");
    for (;;) {
        char request[64];
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        ssize_t count = recvfrom(fd, request, sizeof(request) - 1, 0, (struct sockaddr *)&peer, &peer_length);
        if (count <= 0) continue;
        request[count] = '\0';
        if (strncmp(request, "VJDISCOVER", 10) == 0) {
            struct ifreq interface_request;
            char response[96];
            const char *ip = "0.0.0.0";
            memset(&interface_request, 0, sizeof(interface_request));
            strncpy(interface_request.ifr_name, "eth0", IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFADDR, &interface_request) == 0) {
                struct sockaddr_in *resolved = (struct sockaddr_in *)&interface_request.ifr_addr;
                ip = inet_ntoa(resolved->sin_addr);
            }
            snprintf(response, sizeof(response), "VJXZ 1 %s 50005", ip);
            sendto(fd, response, strlen(response), 0, (struct sockaddr *)&peer, peer_length);
        }
    }
    return NULL;
}

static void send_touch_event(uint8_t type, int x, int y, int value) {
    struct vjte_event event;
    int client = vjfs_client_fd;
    if (client < 0 || !overlay_is_active() || x < 0 || y < 0) return;
    if (x > 65535) x = 65535;
    if (y > 65535) y = 65535;
    memset(&event, 0, sizeof(event));
    memcpy(event.magic, "VJTE", 4);
    event.version = 1;
    event.type = type;
    event.sequence = touch_sequence++;
    event.x = (uint16_t)x;
    event.y = (uint16_t)y;
    event.value = value;
    if (send(client, &event, sizeof(event), MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)sizeof(event)) {
        char sent_line[96];
        snprintf(sent_line, sizeof(sent_line), "VJTE_SENT type=%u %d,%d", (unsigned)type, x, y);
        log_line(sent_line);
    } else {
        log_line("VJTE_SEND_FAILED");
    }
}

static DFBResult hooked_input_get_event(IDirectFBEventBuffer *thiz, DFBEvent *event) {
    DFBResult result = original_get_input_event(thiz, event);
    if (result == DFB_OK && event && event->clazz == DFEC_INPUT) {
        DFBInputEvent *input = &event->input;
        if (input->type == DIET_AXISMOTION && (input->flags & DIEF_AXISABS)) {
            if (input->axis == DIAI_X) touch_x = input->axisabs;
            else if (input->axis == DIAI_Y) touch_y = input->axisabs;
        } else if (input->type == DIET_BUTTONPRESS) {
            touch_down = 1;
            send_touch_event(1, touch_x, touch_y, input->button);
        } else if (input->type == DIET_BUTTONRELEASE) {
            if (touch_down) send_touch_event(2, touch_x, touch_y, input->button);
            touch_down = 0;
        }
    }
    return result;
}

static void hook_input_event_buffer(IDirectFBEventBuffer *buffer, const char *log_message) {
    if (!buffer || buffer->GetEvent == hooked_input_get_event) return;
    original_get_input_event = buffer->GetEvent;
    buffer->GetEvent = hooked_input_get_event;
    log_line(log_message);
}

static DFBResult hooked_create_input_event_buffer(
    IDirectFB *thiz,
    DFBInputDeviceCapabilities caps,
    DFBBoolean global,
    IDirectFBEventBuffer **buffer
) {
    DFBResult result = original_create_input_event_buffer(thiz, caps, global, buffer);
    if (result == DFB_OK && buffer) hook_input_event_buffer(*buffer, "INPUT_EVENT_BUFFER_HOOKED");
    return result;
}

static DFBResult hooked_create_event_buffer(IDirectFB *thiz, IDirectFBEventBuffer **buffer) {
    DFBResult result = original_create_event_buffer(thiz, buffer);
    if (result == DFB_OK && buffer) hook_input_event_buffer(*buffer, "GENERIC_EVENT_BUFFER_HOOKED");
    return result;
}

static DFBResult hooked_input_create_event_buffer(
    IDirectFBInputDevice *thiz,
    IDirectFBEventBuffer **buffer
) {
    DFBResult result = original_input_create_event_buffer(thiz, buffer);
    if (result == DFB_OK && buffer) hook_input_event_buffer(*buffer, "DEVICE_EVENT_BUFFER_HOOKED");
    return result;
}

static DFBResult hooked_get_input_device(
    IDirectFB *thiz,
    DFBInputDeviceID id,
    IDirectFBInputDevice **device
) {
    DFBResult result = original_get_input_device(thiz, id, device);
    if (result == DFB_OK && device && *device && (*device)->CreateEventBuffer != hooked_input_create_event_buffer) {
        original_input_create_event_buffer = (*device)->CreateEventBuffer;
        (*device)->CreateEventBuffer = hooked_input_create_event_buffer;
        log_line("INPUT_DEVICE_HOOKED");
    }
    return result;
}

static DFBResult hooked_create_surface(
    IDirectFB *thiz,
    const DFBSurfaceDescription *desc,
    IDirectFBSurface **ret_interface
) {
    DFBResult result = original_create_surface(thiz, desc, ret_interface);
    if (
        result == DFB_OK && ret_interface && *ret_interface && desc &&
        (desc->flags & DSDESC_CAPS) && (desc->caps & DSCAPS_PRIMARY)
    ) {
        primary_surface = *ret_interface;
        original_primary_flip = primary_surface->Flip;
        primary_surface->Flip = hooked_primary_flip;
        log_line("PRIMARY_SURFACE_HOOKED");
    }
    return result;
}

static void hook_primary_surface(IDirectFBSurface *surface, const char *source) {
    if (!surface || surface->Flip == hooked_primary_flip) return;
    primary_surface = surface;
    original_primary_flip = surface->Flip;
    surface->Flip = hooked_primary_flip;
    log_line(source);
}

static void *poll_rbp_primary_surface(void *unused) {
    int attempt = 0;
    (void)unused;
    /* rbp can publish s_primary later than any fixed deadline, especially on a
     * hot restart. Giving up used to leave Flip() unhooked for the rest of the
     * session, which lets the stock UI paint straight over VJ output. Converge
     * instead, and re-arm if rbp swaps the surface out from under us. */
    for (;;) {
        IDirectFBSurface *surface = (IDirectFBSurface *)*XZ_RBP_PRIMARY_SURFACE_GLOBAL;
        if (surface && surface->Flip && surface->Flip != hooked_primary_flip) {
            hook_primary_surface(surface, "RBP_S_PRIMARY_SURFACE_HOOKED");
        }
        usleep(attempt < 200 ? 50000 : 250000);
        if (attempt < 200) attempt += 1;
    }
    return NULL;
}

static DFBResult hooked_layer_get_surface(
    IDirectFBDisplayLayer *thiz,
    IDirectFBSurface **ret_interface
) {
    DFBResult result = original_get_layer_surface(thiz, ret_interface);
    if (result == DFB_OK && ret_interface && *ret_interface) {
        hook_primary_surface(*ret_interface, "PRIMARY_LAYER_SURFACE_HOOKED");
    }
    return result;
}

static DFBResult hooked_get_display_layer(
    IDirectFB *thiz,
    DFBDisplayLayerID id,
    IDirectFBDisplayLayer **ret_interface
) {
    DFBResult result = original_get_display_layer(thiz, id, ret_interface);
    if (result == DFB_OK && ret_interface && *ret_interface) {
        if ((*ret_interface)->GetSurface != hooked_layer_get_surface) {
            original_get_layer_surface = (*ret_interface)->GetSurface;
            (*ret_interface)->GetSurface = hooked_layer_get_surface;
            log_line("DISPLAY_LAYER_HOOKED");
        }
    }
    return result;
}

DFBResult DirectFBCreate(IDirectFB **ret_interface) {
    static DirectFBCreateFn real_create;
    DFBResult result;
    if (!real_create) real_create = (DirectFBCreateFn)dlsym(RTLD_NEXT, "DirectFBCreate");
    if (!real_create) {
        log_line("ERROR DirectFBCreate RTLD_NEXT missing");
        return DFB_FAILURE;
    }
    result = real_create(ret_interface);
    if (result == DFB_OK && ret_interface && *ret_interface) {
        directfb_interface = *ret_interface;
        original_create_surface = (*ret_interface)->CreateSurface;
        (*ret_interface)->CreateSurface = hooked_create_surface;
        original_get_display_layer = (*ret_interface)->GetDisplayLayer;
        (*ret_interface)->GetDisplayLayer = hooked_get_display_layer;
        original_create_input_event_buffer = (*ret_interface)->CreateInputEventBuffer;
        (*ret_interface)->CreateInputEventBuffer = hooked_create_input_event_buffer;
        original_create_event_buffer = (*ret_interface)->CreateEventBuffer;
        (*ret_interface)->CreateEventBuffer = hooked_create_event_buffer;
        original_get_input_device = (*ret_interface)->GetInputDevice;
        (*ret_interface)->GetInputDevice = hooked_get_input_device;
        log_line("DIRECTFB_INTERFACE_HOOKED");
        if (!primary_poll_started) {
            pthread_t thread;
            primary_poll_started = 1;
            if (pthread_create(&thread, NULL, poll_rbp_primary_surface, NULL) == 0) {
                pthread_detach(thread);
            } else {
                log_line("ERROR primary surface poll thread failed");
            }
        }
        if (!listener_started) {
            pthread_t listener;
            listener_started = 1;
            if (pthread_create(&listener, NULL, vjfs_listener, NULL) == 0) {
                pthread_detach(listener);
            } else {
                log_line("ERROR VJFS listener thread failed");
            }
        }
        if (!discovery_started) {
            pthread_t discovery;
            discovery_started = 1;
            if (pthread_create(&discovery, NULL, vj_discovery_listener, NULL) == 0) {
                pthread_detach(discovery);
            } else {
                discovery_started = 0;
                log_line("ERROR VJ discovery thread failed");
            }
        }
        /*
         * Render into rbp's real DirectFB primary backbuffer in
         * hooked_primary_flip(). The separate /dev/fb1 foreground-page
         * presenter produced correct framebuffer memory but DISP3 FG stayed at
         * yoffset=0 on this kernel, so the LCD never composited page 1. The
         * primary surface is the display path the stock UI already proves.
         */
        if (!framebuffer_presenter_started) {
            pthread_t framebuffer_thread;
            framebuffer_presenter_started = 1;
            if (pthread_create(&framebuffer_thread, NULL, framebuffer_presenter, NULL) == 0) {
                pthread_detach(framebuffer_thread);
            } else {
                framebuffer_presenter_started = 0;
                log_line("ERROR framebuffer presenter thread failed");
            }
        }
        /* v30 visibility control: use only the stock-proven primary Flip()
         * path. Independent presenter experiments targeted non-visible
         * buffers on this firmware and remain disabled until surface
         * ownership can be proven. */
        if (0 && !primary_60hz_presenter_started) {
            pthread_t presenter;
            primary_60hz_presenter_started = 1;
            if (pthread_create(&presenter, NULL, primary_60hz_presenter, NULL) == 0) {
                pthread_detach(presenter);
            } else {
                primary_60hz_presenter_started = 0;
                log_line("ERROR primary 60Hz presenter thread failed");
            }
        }
        log_line("PRIMARY_SURFACE_COMPOSITOR_ENABLED");
    }
    return result;
}
