#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "cue/native.h"
#include "runtime.h"
#include "audio/runtime.h"
#include "ui_runtime.h"
#include "key/runtime.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Development runtime. No constructor action unless explicitly enabled.
 * Hardware acceptance and the on-deck settings UI are still required. */
static struct xz_cue cues;
static struct xz_cue_native native;
static int (*stock_key)(void *, const void *);
static uint32_t *const key_slot = (uint32_t *)(uintptr_t)0x479090;
static int slot_protection;
static int installed;
static int observer;
static int application_verified;
static void *led_replacement;
static int memory_fd = -1;
static pthread_mutex_t cue_mutex = PTHREAD_MUTEX_INITIALIZER;
struct code_hook {
    uint32_t address;
    unsigned char saved[8];
    void *trampoline;
    int protection;
};
static struct code_hook code_hooks[16];
static unsigned code_hook_count;
struct pad_trace_record { uint32_t key,operation_byte,channel,mode,decoded,deck,pad,operation,hotcue,ui_flags; };
struct pad_trace { uint32_t version,sequence,count,enabled;struct pad_trace_record records[64]; };
__attribute__((visibility("default"))) struct pad_trace xz_mods_pad_trace_v1 = {1,0,0,0,{{0}}};

static int enabled(const char *name) {
    const char *value = getenv(name);
    return value && strcmp(value, "1") == 0;
}

void xz_log(const char *message) {
    FILE *file = fopen("/dev/shm/xz-mods.log", "a");
    if (file) { fprintf(file, "%s\n", message); fclose(file); }
}

int xz_read_memory(uint32_t address, void *out, size_t length) {
    if (!application_verified || memory_fd < 0 || !address || !out ||
        !length || length > UINT32_MAX - address) return -1;
    return pread(memory_fd, out, length, (off_t)address) == (ssize_t)length ? 0 : -1;
}

static int range_protection(uintptr_t address, size_t length) {
    char line[512], *cursor;
    const char *permissions;
    unsigned long start, end;
    FILE *maps = fopen("/proc/self/maps", "r");
    int result = -1;
    if (!maps) return -1;
    while (fgets(line, sizeof(line), maps)) {
        start = strtoul(line, &cursor, 16);
        if (*cursor != '-') continue;
        end = strtoul(cursor + 1, &cursor, 16);
        if (*cursor != ' ') continue;
        while (*cursor == ' ') cursor++;
        permissions = cursor;
        if (strlen(permissions) < 4) continue;
        if (address < start || address >= end || length > end - address) continue;
        result = (permissions[0] == 'r' ? PROT_READ : 0)
               | (permissions[1] == 'w' ? PROT_WRITE : 0)
               | (permissions[2] == 'x' ? PROT_EXEC : 0);
        break;
    }
    fclose(maps);
    return result;
}

static int known_application(void) {
    char path[256], command[96], digest[80];
    ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    FILE *result;
    if (size < 0 || size >= (ssize_t)sizeof(path) - 1) return 0;
    path[size] = 0;
    if (strcmp(path, "/root/pdj/rbp") != 0) return 0;
    /* Resolve the parent's executable, not /proc/self/exe in the checksum child.
     * The path contains only our numeric PID, never an environment string. */
    snprintf(command, sizeof(command), "/bin/busybox md5sum /proc/%ld/exe", (long)getpid());
    result = popen(command, "r");
    if (!result) return 0;
    int read_ok = fgets(digest, sizeof(digest), result) != NULL;
    int exit_status = pclose(result);
    if (!read_ok || exit_status != 0) return 0;
    return strncmp(digest, "627f39fbabb0b47f2d45f4aeac7e5899 ", 33) == 0
        || strncmp(digest, "6a7ccb454e52afa26a73f3380706c9ca ", 33) == 0;
}

int xz_hook_slot(uint32_t address, uint32_t expected, void *replacement, void **original) {
    if (!application_verified || address != 0x478d3c || expected != 0x27f074 ||
        !replacement || !original || led_replacement) return -1;
    int protection = range_protection(address,4);
    if (protection < 0 || !(protection & PROT_READ)) return -1;
    uint32_t *slot = (uint32_t *)(uintptr_t)address;
    if (*slot != expected) return -1;
    long size = sysconf(_SC_PAGESIZE);
    if (size <= 0) return -1;
    uintptr_t page = address - address % (uintptr_t)size;
    if (mprotect((void *)page,(size_t)size,protection|PROT_WRITE)) return -1;
    *original = (void *)(uintptr_t)expected;
    *slot = (uint32_t)(uintptr_t)replacement;
    if (mprotect((void *)page,(size_t)size,protection)) {
        *slot = expected; mprotect((void *)page,(size_t)size,protection); return -1;
    }
    led_replacement = replacement;
    return 0;
}

int xz_hook_arm(uint32_t address, const unsigned char expected[8],
                void *replacement, void **original) {
    static const unsigned char source_guard[8] = {0xf8,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char load_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x5c,0xd0,0x4d,0xe2};
    static const unsigned char unload_guard[8] = {0x70,0x40,0x2d,0xe9,0x28,0x50,0x80,0xe2};
    static const unsigned char touch_guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char output_guard[8] = {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char wave_lock_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char wave_unlock_guard[8] = {0x10,0x40,0x2d,0xe9,0x58,0xd0,0x4d,0xe2};
    static const unsigned char mixer_receive_guard[8] = {0x40,0x32,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    const unsigned char *guard = address == 0x8fd5c ? source_guard :
                                 address == 0x34ba0 ? load_guard :
                                 address == 0x348dc ? unload_guard :
                                 address == 0x2628b4 ? touch_guard :
                                 address == 0x76284 ? output_guard :
                                 address == 0xdf994 ? link_guard :
                                 address == 0xe0a50 ? rekordbox_guard :
                                 address == 0x156958 ? wave_lock_guard :
                                 address == 0x1656c4 ? wave_unlock_guard :
                                 address == 0x25d7f4 ? mixer_receive_guard : NULL;
    if (!application_verified || !guard || !expected || !replacement || !original ||
        code_hook_count >= sizeof(code_hooks)/sizeof(code_hooks[0]) || memcmp(expected, guard, 8) != 0) return -1;
    int protection = range_protection(address, 8);
    if (protection < 0 || !(protection & PROT_READ) ||
        memcmp((void *)(uintptr_t)address, guard, 8) != 0) return -1;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return -1;
    uint32_t *trampoline = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) return -1;
    memcpy(trampoline, guard, 8);
    trampoline[2] = 0xe51ff004; /* ARM ldr pc,[pc,#-4], followed by destination. */
    trampoline[3] = address + 8;
    __builtin___clear_cache((char *)trampoline, (char *)trampoline + 16);
    if (mprotect(trampoline, (size_t)page_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(trampoline, (size_t)page_size); return -1;
    }
    uintptr_t page = address - address % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, protection | PROT_WRITE) != 0) {
        munmap(trampoline, (size_t)page_size); return -1;
    }
    uint32_t patch[2] = {0xe51ff004, (uint32_t)(uintptr_t)replacement};
    *original = trampoline;
    memcpy((void *)(uintptr_t)address, patch, 8);
    __builtin___clear_cache((char *)(uintptr_t)address, (char *)(uintptr_t)address + 8);
    if (mprotect((void *)page, (size_t)page_size, protection) != 0) {
        memcpy((void *)(uintptr_t)address, guard, 8);
        __builtin___clear_cache((char *)(uintptr_t)address, (char *)(uintptr_t)address + 8);
        mprotect((void *)page, (size_t)page_size, protection);
        *original = NULL;
        munmap(trampoline, (size_t)page_size);
        return -1;
    }
    struct code_hook *record = &code_hooks[code_hook_count++];
    record->address = address; record->protection = protection; record->trampoline = trampoline;
    memcpy(record->saved, guard, 8);
    return 0;
}

static int physical_key(void *self, const void *input) {
    struct xz_cue_event event = {0};
    pthread_mutex_lock(&cue_mutex);
    int decoded = xz_cue_native_decode(&native, self, input, &event);
    pthread_mutex_unlock(&cue_mutex);
    unsigned flags = 0;
    int pad_consumed = decoded && !observer && xz_ui_runtime_pad(&event, &flags);
    if (xz_mods_pad_trace_v1.enabled && self && input) {
        uint16_t key; uint32_t mode;
        memcpy(&key,(const unsigned char *)input+8,2);
        memcpy(&mode,(const unsigned char *)self+0x80,4);
        pthread_mutex_lock(&cue_mutex);
        struct pad_trace *t=&xz_mods_pad_trace_v1;
        __atomic_add_fetch(&t->sequence,1,__ATOMIC_SEQ_CST);
        t->records[t->count%64]=(struct pad_trace_record){key,((const unsigned char *)input)[11],
            ((const unsigned char *)self)[0x26],mode,(uint32_t)decoded,(uint32_t)event.deck,
            (uint32_t)event.pad,(uint32_t)event.operation,(uint32_t)event.hotcue_mode,flags};
        t->count++;
        __atomic_add_fetch(&t->sequence,1,__ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&cue_mutex);
    }
    if (pad_consumed) return 1;
    pthread_mutex_lock(&cue_mutex);
    int consumed = decoded && !observer && xz_cue_before(&cues, &event);
    pthread_mutex_unlock(&cue_mutex);
    if (consumed) return 1;
    int result = stock_key(self, input);
    if (observer && decoded && (event.pad >= 0 || event.play)) {
        char line[160];
        snprintf(line, sizeof(line), "key deck=%d pad=%d op=%d hotcue=%d play=%d stock=%d",
                 event.deck, event.pad, event.operation, event.hotcue_mode, event.play, result);
        xz_log(line);
    }
    if (decoded && !observer) {
        pthread_mutex_lock(&cue_mutex);
        xz_cue_after(&cues, &event, result);
        pthread_mutex_unlock(&cue_mutex);
    }
    return result;
}

int xz_runtime_set_cues(int gate, int smart) {
    if (!installed || observer) return -1;
    pthread_mutex_lock(&cue_mutex);
    xz_cue_settings(&cues, gate, smart);
    pthread_mutex_unlock(&cue_mutex);
    return 0;
}

unsigned xz_runtime_cue_flags(void) {
    pthread_mutex_lock(&cue_mutex);
    unsigned result = (cues.gate ? 1u : 0u) | (cues.smart ? 2u : 0u);
    pthread_mutex_unlock(&cue_mutex);
    return result;
}

__attribute__((constructor)) static void start(void) {
    if (!enabled("XZ_MODS_ENABLE") || !known_application()) return;
    application_verified = 1;
    memory_fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    int protection = range_protection((uintptr_t)key_slot, sizeof(*key_slot));
    if (protection < 0 || !(protection & PROT_READ) || *key_slot != 0x28e88c) {
        xz_log("REFUSED: physical-key vtable differs from verified firmware");
        return;
    }
    observer = enabled("XZ_MODS_OBSERVER");
    xz_mods_pad_trace_v1.enabled = enabled("XZ_MODS_PAD_TRACE");
    xz_cue_init(&cues, xz_cue_native_api(&native));
    xz_cue_settings(&cues, enabled("XZ_MODS_GATE_CUE"), enabled("XZ_MODS_SMART_CUE"));
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return;
    uintptr_t page = (uintptr_t)key_slot - (uintptr_t)key_slot % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, protection | PROT_WRITE) != 0) {
        xz_log("REFUSED: cannot change vtable protection");
        return;
    }
    stock_key = (int (*)(void *, const void *))(uintptr_t)*key_slot;
    slot_protection = protection;
    native.verified = 1;
    *key_slot = (uint32_t)(uintptr_t)physical_key;
    if (mprotect((void *)page, (size_t)page_size, protection) != 0) {
        *key_slot = (uint32_t)(uintptr_t)stock_key;
        mprotect((void *)page, (size_t)page_size, protection);
        native.verified = 0;
        xz_log("REFUSED: could not restore original page protection");
        return;
    }
    installed = 1;
    xz_log(observer ? "OBSERVER installed; stock dispatch retained" : "EXPERIMENTAL gate/smart cue adapter installed");
    if (observer) return;
    int audio_ready = memory_fd >= 0 && xz_audio_start() == 0;
    int key_ready = audio_ready && enabled("XZ_MODS_KEYSHIFT") && xz_key_runtime_start() == 0;
    if (audio_ready && enabled("XZ_MODS_STEMS")) xz_audio_set_enabled(1);
    if (enabled("XZ_MODS_UI")) xz_ui_runtime_start(audio_ready, key_ready, enabled("XZ_MODS_STEMS"));
}

__attribute__((destructor)) static void stop(void) {
    xz_ui_runtime_stop();
    xz_key_runtime_stop();
    xz_audio_stop();
    if (memory_fd >= 0) { close(memory_fd); memory_fd = -1; }
    if (!installed || *key_slot != (uint32_t)(uintptr_t)physical_key) return;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return;
    uintptr_t page = (uintptr_t)key_slot - (uintptr_t)key_slot % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, slot_protection | PROT_WRITE) != 0) return;
    *key_slot = (uint32_t)(uintptr_t)stock_key;
    mprotect((void *)page, (size_t)page_size, slot_protection);
    native.verified = 0;
}
