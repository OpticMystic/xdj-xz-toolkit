/* SPDX-License-Identifier: MIT OR Apache-2.0
 * On-media key and layout adapted from cdj3k-mods mods/stem/cache.c,
 * e74e199603e2a25567950ca72997c38d17ada4e8. See PROVENANCE.md. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "stems.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define xz_seek _fseeki64
#define xz_tell _ftelli64
#else
#define xz_seek fseeko
#define xz_tell ftello
#endif

/* This is upstream's actual offset basis, including its nonstandard value. */
#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)
#define KEY_WINDOW 65536

static uint64_t hash_bytes(uint64_t h, const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}

static uint64_t hash_u64le(uint64_t h, uint64_t value)
{
    unsigned char bytes[8];
    unsigned i;
    for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
    return hash_bytes(h, bytes, sizeof(bytes));
}

int xz_stem_key(const char *track, int64_t frames, char key[17])
{
    FILE *f;
    int64_t length;
    size_t wanted, got;
    unsigned char window[KEY_WINDOW];
    uint64_t h;
    if (!track || !key || frames <= 0) return XZ_STEM_INVALID;
    key[0] = 0;
    f = fopen(track, "rb");
    if (!f) return errno == ENOENT ? XZ_STEM_MISSING : XZ_STEM_IO;
    if (xz_seek(f, 0, SEEK_END) != 0 || (length = xz_tell(f)) <= 0 ||
        xz_seek(f, 0, SEEK_SET) != 0) { fclose(f); return XZ_STEM_IO; }
    h = hash_u64le(hash_u64le(FNV_OFFSET, (uint64_t)length), (uint64_t)frames);
    wanted = length < KEY_WINDOW ? (size_t)length : KEY_WINDOW;
    got = fread(window, 1, wanted, f);
    if (got != wanted) { fclose(f); return XZ_STEM_IO; }
    h = hash_bytes(h, window, got);
    if (length > KEY_WINDOW) {
        if (xz_seek(f, length - KEY_WINDOW, SEEK_SET) != 0 ||
            fread(window, 1, KEY_WINDOW, f) != KEY_WINDOW) {
            fclose(f); return XZ_STEM_IO;
        }
        h = hash_bytes(h, window, KEY_WINDOW);
    }
    if (fclose(f) != 0) return XZ_STEM_IO;
    snprintf(key, 17, "%016llx", (unsigned long long)h);
    return XZ_STEM_OK;
}

static int valid_id(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    if (!p || !*p || !strcmp(id, ".") || !strcmp(id, "..")) return 0;
    for (; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    return 1;
}

static int read_meta(const char *directory, struct xz_stem_entry *entry)
{
    char path[XZ_STEM_PATH_MAX], line[160], *end;
    unsigned seen = 0;
    FILE *f;
    int rc = XZ_STEM_INVALID;
    if (snprintf(path, sizeof(path), "%s/meta", directory) >= (int)sizeof(path))
        return XZ_STEM_INVALID;
    f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? XZ_STEM_MISSING : XZ_STEM_IO;
    while (fgets(line, sizeof(line), f)) {
        char *value = strchr(line, '=');
        unsigned bit;
        if (!value || (!strchr(line, '\n') && !feof(f))) goto done;
        *value++ = 0;
        errno = 0;
        if (!strcmp(line, "v")) {
            bit = 1;
            if (strtol(value, &end, 10) != 1) goto done;
        } else if (!strcmp(line, "frames")) {
            bit = 2;
            entry->upload_frames = strtoll(value, &end, 10);
            if (entry->upload_frames <= 0) goto done;
        } else if (!strcmp(line, "harmonics") || !strcmp(line, "vocals")) {
            float gain = strtof(value, &end);
            bit = !strcmp(line, "harmonics") ? 4 : 8;
            if (!isfinite(gain) || gain <= 0 || !isfinite(1.0f / gain)) goto done;
            if (bit == 4) entry->harmonics_gain = gain;
            else entry->vocals_gain = gain;
        } else goto done;
        if (errno || end == value || (seen & bit)) goto done;
        while (*end == '\r' || *end == '\n') ++end;
        if (*end) goto done;
        seen |= bit;
    }
    rc = ferror(f) ? XZ_STEM_IO : seen == 15 ? XZ_STEM_OK : XZ_STEM_INVALID;
done:
    fclose(f);
    return rc;
}

int xz_stem_cache_choice(const char *volume, const char *track,
                         int64_t frames, char *id, size_t capacity)
{
    char key[17], path[XZ_STEM_PATH_MAX], value[98];
    size_t length;
    FILE *file;
    int result;
    if (!volume || !*volume || !id || capacity < 97) return XZ_STEM_INVALID;
    id[0] = 0;
    result = xz_stem_key(track, frames, key);
    if (result != XZ_STEM_OK) return result;
    if (snprintf(path, sizeof(path), "%s/mods/xz-mods/cache-choice/%s.txt", volume, key) >= (int)sizeof(path))
        return XZ_STEM_INVALID;
    /* Preferences must not follow links outside their app-owned directory. */
    for (size_t i = strlen(volume) + 1; i <= strlen(path); ++i) {
        char saved = path[i];
        if (saved && saved != '/') continue;
        path[i] = 0;
#ifdef _WIN32
        DWORD attrs = GetFileAttributesA(path);
        int linked = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
#else
        struct stat info;
        int linked = !lstat(path, &info) && S_ISLNK(info.st_mode);
#endif
        path[i] = saved;
        if (linked) return XZ_STEM_INVALID;
    }
    file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? XZ_STEM_MISSING : XZ_STEM_IO;
    length = fread(value, 1, sizeof(value), file);
    result = ferror(file) ? XZ_STEM_IO : XZ_STEM_INVALID;
    if (!ferror(file) && feof(file) && length > 0 && length < sizeof(value)) {
        if (value[length-1] == '\n') --length;
        if (length && value[length-1] == '\r') --length;
        value[length] = 0;
        if (length && length <= 96 && strlen(value) == length && valid_id(value)) {
            memcpy(id, value, length+1);
            result = XZ_STEM_OK;
        }
    }
    fclose(file);
    return result;
}

static int find_part(const char *directory, const char *part, char *out)
{
    const char *extensions[] = { ".flac", ".wav" };
    struct stat st;
    unsigned i;
    for (i = 0; i < 2; ++i) {
        if (snprintf(out, XZ_STEM_PATH_MAX, "%s/%s%s", directory, part,
                     extensions[i]) >= XZ_STEM_PATH_MAX) return XZ_STEM_INVALID;
        if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
            return XZ_STEM_OK;
    }
    return XZ_STEM_MISSING;
}

int xz_stem_cache_lookup(const char *volume, const char *separation_id,
                         const char *track, int64_t upload_frames,
                         struct xz_stem_entry *out)
{
    char key[17], directory[XZ_STEM_PATH_MAX], chosen[97];
    struct xz_stem_entry entry;
    int rc;
    if (!volume || !*volume || !out)
        return XZ_STEM_INVALID;
    memset(out, 0, sizeof(*out));
    if (!separation_id || !*separation_id) {
        rc = xz_stem_cache_choice(volume, track, upload_frames, chosen, sizeof(chosen));
        if (rc != XZ_STEM_OK) return rc;
        separation_id = chosen;
    }
    if (!valid_id(separation_id)) return XZ_STEM_INVALID;
    memset(&entry, 0, sizeof(entry));
    rc = xz_stem_key(track, upload_frames, key);
    if (rc != XZ_STEM_OK) return rc;
    if (snprintf(directory, sizeof(directory), "%s/mods/stemd-cache/%s/%c%c/%s",
                 volume, separation_id, key[0], key[1], key) >= (int)sizeof(directory))
        return XZ_STEM_INVALID;
    rc = read_meta(directory, &entry);
    if (rc != XZ_STEM_OK) return rc;
    if (entry.upload_frames != upload_frames) return XZ_STEM_INVALID;
    rc = find_part(directory, "harmonics", entry.harmonics);
    if (rc != XZ_STEM_OK) return rc;
    rc = find_part(directory, "vocals", entry.vocals);
    if (rc != XZ_STEM_OK) return rc;
    *out = entry;
    return XZ_STEM_OK;
}
