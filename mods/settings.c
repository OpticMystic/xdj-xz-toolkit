/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
void xz_settings_default(struct xz_settings *s) {
    *s = (struct xz_settings){0,0,0,0,0,0,1,0};
}
static int valid(const struct xz_settings *s) {
    return (unsigned)s->stems <= 1 && (unsigned)s->gate <= 1 &&
        (unsigned)s->smart <= 1 && (unsigned)s->theme <= 6 &&
        (unsigned)s->stem_page <= 3 && (unsigned)s->shift_pages <= 1 &&
        (unsigned)s->pad_feedback <= 1 && (unsigned)s->shift_keysync <= 1;
}
int xz_settings_parse(const char *text, struct xz_settings *out) {
    struct xz_settings s;
    const char *header = "XZ_MODS_SETTINGS 1\n";
    const char *keys[] = {"stems=","gate=","smart=","theme=","stem_page=","shift_pages=","pad_feedback=","shift_keysync="};
    int *values[] = {&s.stems,&s.gate,&s.smart,&s.theme,&s.stem_page,&s.shift_pages,&s.pad_feedback,&s.shift_keysync};
    if (strncmp(text,header,strlen(header))) return -1;
    text += strlen(header);
    for (unsigned i=0;i<8;i++) {
        size_t size = strlen(keys[i]);
        if (strncmp(text,keys[i],size)) return -1;
        text += size;
        if (*text < '0' || *text > '9' || text[1] != '\n') return -1;
        *values[i] = *text-'0'; text += 2;
    }
    if (*text || !valid(&s)) return -1;
    *out = s; return 0;
}
int xz_settings_format(const struct xz_settings *s, char *out, size_t size) {
    if (!valid(s)) return -1;
    int n = snprintf(out,size,"XZ_MODS_SETTINGS 1\nstems=%d\ngate=%d\nsmart=%d\ntheme=%d\nstem_page=%d\nshift_pages=%d\npad_feedback=%d\nshift_keysync=%d\n",
        s->stems,s->gate,s->smart,s->theme,s->stem_page,s->shift_pages,s->pad_feedback,s->shift_keysync);
    return n < 0 || (size_t)n >= size ? -1 : n;
}
static int paths(const char *usb, char *dir, char *file) {
    struct stat st;
    if (!usb || usb[0] != '/' || lstat(usb,&st) || !S_ISDIR(st.st_mode)) return -1;
    if (!strncmp(usb,"/media/",7)) {
        struct stat root;
        if (stat("/",&root) || root.st_dev == st.st_dev) return -1;
    }
    if (snprintf(dir,1024,"%s/VJ.Tools",usb) >= 1024 ||
        snprintf(file,1024,"%s/XZ-Mods.cfg",dir) >= 1024) return -1;
    if (!lstat(dir,&st) && !S_ISDIR(st.st_mode)) return -1;
    return 0;
}
int xz_settings_load(const char *usb, struct xz_settings *s) {
    char dir[1024], path[1024], text[512];
    if (paths(usb,dir,path)) return -1;
    int fd = open(path,O_RDONLY|O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size >= (off_t)sizeof(text)) { close(fd); return -1; }
    ssize_t n = read(fd,text,sizeof(text)-1); close(fd);
    if (n < 0 || n != st.st_size || memchr(text,0,(size_t)n)) return -1;
    text[n] = 0;
    return xz_settings_parse(text,s);
}
int xz_settings_save(const char *usb, const struct xz_settings *s) {
    char dir[1024],path[1024],temp[1060],text[512];
    int n = xz_settings_format(s,text,sizeof(text));
    if (n < 0 || paths(usb,dir,path)) return -1;
    if (mkdir(dir,0755) && errno != EEXIST) return -1;
    snprintf(temp,sizeof(temp),"%s/.XZ-Mods.XXXXXX",dir);
    int fd = mkstemp(temp);
    if (fd < 0) return -1;
    int ok = write(fd,text,(size_t)n) == n && fsync(fd) == 0;
    if (close(fd)) ok = 0;
    if (ok && rename(temp,path) == 0) {
        int directory = open(dir,O_RDONLY|O_DIRECTORY);
        if (directory < 0) return -1;
        int result = fsync(directory); close(directory); return result;
    }
    unlink(temp); return -1;
}
