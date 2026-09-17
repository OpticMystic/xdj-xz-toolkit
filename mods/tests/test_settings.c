#define _POSIX_C_SOURCE 200809L
#include "../settings.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    struct xz_settings a,b;
    char text[512], root[]="/tmp/xz-settings-XXXXXX",path[1024];
    xz_settings_default(&a);
    assert(!a.shift_keysync && a.pad_feedback && !a.stems);
    assert(a.fb_takeover == 1 && a.takeover_assign == XZ_TAKEOVER_LINK);
    const char *legacy = "XZ_MODS_SETTINGS 1\nstems=0\ngate=0\nsmart=0\ntheme=0\nstem_page=0\nshift_pages=0\npad_feedback=1\nshift_keysync=0\n";
    assert(xz_settings_parse(legacy, &b) == 0 && b.fb_takeover == 1 && b.takeover_assign == XZ_TAKEOVER_LINK);
    for (int page=0;page<4;page++) {
        a.stems=1;a.stem_page=page;a.shift_pages=1;a.shift_keysync=1;a.theme=6;
        a.fb_takeover=page%2;a.takeover_assign=page%3;
        assert(xz_settings_format(&a,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b) && !memcmp(&a,&b,sizeof(a)));
    }
    b=a;
    assert(xz_settings_parse("XZ_MODS_SETTINGS 2\n",&b)<0 && !memcmp(&a,&b,sizeof(a)));
    strcat(text,"unexpected=1\n");assert(xz_settings_parse(text,&b)<0);
    a.stem_page=4;assert(xz_settings_format(&a,text,sizeof(text))<0);a.stem_page=3;
    assert(mkdtemp(root));assert(xz_settings_load(root,&b)==1);
    assert(!xz_settings_save(root,&a));assert(!xz_settings_load(root,&b));assert(!memcmp(&a,&b,sizeof(a)));
    a.shift_keysync=0;assert(!xz_settings_save(root,&a));assert(!xz_settings_load(root,&b) && !b.shift_keysync);
    snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",root);assert(!unlink(path));
    assert(!symlink("/dev/null",path));assert(xz_settings_load(root,&b)<0);assert(!unlink(path));
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);assert(!rmdir(path));
    assert(!symlink("/tmp",path));assert(xz_settings_save(root,&a)<0);assert(!unlink(path));assert(!rmdir(root));
    puts("PASS USB settings defaults, round-trip, atomic replacement, version/range validation and symlink refusal");
}
