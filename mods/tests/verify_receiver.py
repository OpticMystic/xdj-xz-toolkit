"""Compile the actual receiver bridge functions against controlled host fixtures."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--zig", required=True)
args=parser.parse_args()
zig=str(Path(args.zig).resolve())
root = Path(__file__).resolve().parents[4]
source = (root / 'packages/xdj-xz-toolkit/vendor/tools/xz_runtime/xz_directfb_hook.c').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

declarations = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mods_bridge.h"
#ifdef NDEBUG
#error assertions required
#endif
static uint8_t composite_frame[16];
static uint16_t mods_present_frame[8];
static int visible, draw_result, vj_active, flips, probe_calls;
static int mods_token;
static void *mods_render;
static int mods_panel_visible(void){return visible;}
static int render_mods(uint16_t *p,uint32_t w,uint32_t h,uint32_t s){
 (void)w;(void)h;(void)s;if(draw_result)p[0]=0x7777;return draw_result;
}
typedef int DFBResult;
typedef int DFBSurfaceFlipFlags;
typedef struct {int x;} DFBRegion;
typedef struct {int unused;} IDirectFBSurface;
struct fake_dfb {void (*WaitIdle)(struct fake_dfb *);};
static struct fake_dfb *directfb_interface;
#define DFB_OK 0
#define DSFLIP_WAITFORSYNC 8
static int framebuffer_presenter_started,primary_60hz_presenter_started;
static uint32_t flip_count;
static const DFBRegion *last_region;
static int last_flags;
static int overlay_is_active(void){return visible||vj_active;}
static void draw_probe_overlay(IDirectFBSurface *p){(void)p;probe_calls++;}
static int draw_mods_on_surface(IDirectFBSurface *p){(void)p;return mods_render?draw_result:0;}
static int original_primary_flip(IDirectFBSurface *p,const DFBRegion *r,int f){(void)p;last_region=r;last_flags=f;flips++;return 19;}
static void write_runtime_stats_if_due(void){}
static int touch_calls,touch_type,touch_px,touch_py,send_ok=1;
static uint32_t touch_sequence;
typedef intptr_t ssize_t;
#define MSG_DONTWAIT 1
#define MSG_NOSIGNAL 2
struct __attribute__((packed)) vjte_event {
 char magic[4];uint8_t version,type;uint16_t flags;uint32_t sequence;
 uint16_t x,y;int32_t value;
};
static ssize_t send(int fd,const void *p,size_t n,int flags){
 const struct vjte_event *e=(const struct vjte_event*)p;(void)fd;(void)flags;
 assert(n==20&&memcmp(e->magic,"VJTE",4)==0&&e->version==1);
 if(!send_ok)return -1;touch_calls++;touch_type=e->type;touch_px=e->x;touch_py=e->y;return (ssize_t)n;
}
static void log_line(const char *s){(void)s;}

static uint32_t mock_now,vj_received_image_count,vj_listener_ready,vj_discovery_ready;
static int vjfs_client_fd=-1,ip_queries,lock_busy;
static int mods_status_mutex;
static uint32_t wallclock_ms(void){return mock_now;}
static int pthread_mutex_trylock(int *m){(void)m;return lock_busy;}
static int pthread_mutex_unlock(int *m){(void)m;return 0;}
static int vj_overlay_is_active(void){return vj_active;}
#define AF_INET 2
#define SOCK_DGRAM 2
#define SIOCGIFADDR 32
#define IFNAMSIZ 16
struct sockaddr_in {uint32_t sin_addr;};
struct ifreq {char ifr_name[16];struct sockaddr_in ifr_addr;};
static int socket(int a,int b,int c){(void)a;(void)b;(void)c;ip_queries++;return 7;}
static int close(int fd){(void)fd;return 0;}
static int ioctl(int fd,unsigned long request,void *p){(void)fd;(void)request;(void)p;return 0;}
static const char *inet_ntop(int family,const void *a,char *s,size_t n){(void)family;(void)a;snprintf(s,n,"192.0.2.3");return s;}
'''
functions = '\n'.join(extract(s) for s in [
    'static const uint8_t *composite_for_present(',
    'static DFBResult hooked_primary_flip(',
    'static int send_touch_event_checked(',
    'static void send_touch_event(uint8_t type, int x, int y, int value) {',
    'void xz_vj_touch_v1(',
    'static void connection_snapshot(',
])
checks = r'''
int main(void){
 IDirectFBSurface surface={0};DFBRegion region={1};struct xz_vj_connection_v1 status;
 const uint8_t *out;uint8_t pristine[16];
 assert(sizeof(status)==48);
 memset(composite_frame,0x12,sizeof(composite_frame));memcpy(pristine,composite_frame,16);
 assert(composite_for_present()==composite_frame);
 mods_render=&mods_token;visible=1;draw_result=1;
 out=composite_for_present();assert(out==(const uint8_t*)mods_present_frame);assert(((const uint16_t*)out)[0]==0x7777);
 assert(memcmp(composite_frame,pristine,16)==0);
 draw_result=0;assert(composite_for_present()==NULL);assert(memcmp(composite_frame,pristine,16)==0);
 visible=0;assert(composite_for_present()==composite_frame);assert(memcmp(composite_frame,pristine,16)==0);
 mods_render=NULL;assert(hooked_primary_flip(&surface,&region,55)==19);assert(last_region==&region&&last_flags==55);
 mods_render=&mods_token;draw_result=0;assert(hooked_primary_flip(&surface,&region,55)==19);assert(last_region==&region&&last_flags==55);
 draw_result=1;assert(hooked_primary_flip(&surface,&region,55)==19);assert(last_region==NULL&&last_flags==DSFLIP_WAITFORSYNC);
 visible=1;draw_result=0;{int previous=flips;assert(hooked_primary_flip(&surface,&region,55)==DFB_OK);assert(flips==previous);}
 visible=0;mods_render=NULL;vj_active=1;
 assert(hooked_primary_flip(&surface,&region,55)==19);assert(last_region==NULL&&last_flags==DSFLIP_WAITFORSYNC);
 framebuffer_presenter_started=1;{int previous=flips;assert(hooked_primary_flip(&surface,&region,55)==DFB_OK);assert(flips==previous);}
 framebuffer_presenter_started=0;vj_active=1;vjfs_client_fd=9;
 xz_vj_touch_v1(1,100,200);assert(touch_calls==1&&touch_type==1&&touch_px==100&&touch_py==200);
 xz_vj_touch_v1(1,101,201);assert(touch_calls==1);
 xz_vj_touch_v1(0,102,202);assert(touch_calls==2&&touch_type==2);
 visible=1;xz_vj_touch_v1(1,100,200);xz_vj_touch_v1(0,100,200);assert(touch_calls==2);
 visible=0;xz_vj_touch_v1(1,800,10);assert(touch_calls==2);
 xz_vj_touch_v1(1,50,50);xz_vj_touch_v1(0,50,50);assert(touch_calls==4);
 xz_vj_touch_v1(1,60,60);assert(touch_calls==5&&touch_type==1);
 visible=1;xz_vj_touch_v1(0,65,65);assert(touch_calls==6&&touch_type==2);
 xz_vj_touch_v1(0,65,65);assert(touch_calls==6);
 xz_vj_touch_v1(1,70,70);xz_vj_touch_v1(0,70,70);assert(touch_calls==6);
 send_touch_event(1,70,70,1);send_touch_event(2,70,70,0);assert(touch_calls==6);
 assert(!send_touch_event_checked(9,1,70,70,1,1));assert(touch_calls==6);
 visible=0;send_ok=0;xz_vj_touch_v1(1,70,70);send_ok=1;
 visible=1;xz_vj_touch_v1(0,70,70);assert(touch_calls==6);
 visible=0;xz_vj_touch_v1(1,70,70);assert(touch_calls==7);
 vjfs_client_fd=10;visible=1;xz_vj_touch_v1(0,70,70);assert(touch_calls==7);
 visible=0;xz_vj_touch_v1(1,80,80);assert(touch_calls==8);
 visible=1;xz_vj_touch_v1(0,-1,-1);assert(touch_calls==9&&touch_type==2&&touch_px==80&&touch_py==80);
 vjfs_client_fd=-1;vj_active=0;
 mock_now=1000;connection_snapshot(&status);assert(status.version==1&&status.size==48);
 assert(!status.listening&&!status.connected&&!status.stats_valid);assert(ip_queries==1);
 mock_now=1500;connection_snapshot(&status);assert(ip_queries==1);
 vj_listener_ready=1;vj_discovery_ready=1;vjfs_client_fd=9;vj_received_image_count=24;mock_now=2000;
 connection_snapshot(&status);assert(ip_queries==2);assert(status.listening&&status.connected&&status.discovery);
 assert(status.stats_valid&&status.frame_hz_milli==24000);assert(strcmp(status.ipv4,"192.0.2.3")==0);
 visible=1;vj_active=0;connection_snapshot(&status);assert(!status.active);
 vj_active=1;connection_snapshot(&status);assert(status.active);
 lock_busy=1;connection_snapshot(&status);assert(status.version==1&&status.size==48&&!status.stats_valid&&status.listening);
 lock_busy=0;vjfs_client_fd=-1;connection_snapshot(&status);assert(!status.stats_valid&&!status.frame_hz_milli);
 puts("PASS actual receiver functions: persistent base, busy skip, closed badge, legacy flips, native ownership release, measured status");
 return 0;
}
'''
temporary=tempfile.TemporaryDirectory(prefix='xz-receiver-test-')
build=Path(temporary.name)
test=build/'mods_bridge_test.c'
test.write_text(declarations+functions+checks)
subprocess.run([zig,'cc','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
 '-I',str(root/'packages/xdj-xz-toolkit/vendor/tools/xz_runtime'),str(test),'-o',str(build/'mods_bridge_test.exe')],check=True)
subprocess.run([str(build/'mods_bridge_test.exe')],check=True)

# Bind readiness must describe a bound socket, never merely a launched thread.
listener=extract('static void *vjfs_listener(')
assert listener.index('listen(server, 1)') < listener.index('&vj_listener_ready, 1u')
assert listener.index('&vj_listener_ready, 0u') < listener.rindex('close(server)')
discovery=extract('static void *vj_discovery_listener(')
assert discovery.index('bind(fd,') < discovery.index('&vj_discovery_ready, 1u')
assert '"VJXZ 1 %s 50005"' in discovery
assert 'mods_owns_native_touch()' in extract('ssize_t read(')
assert '!mods_owns_native_touch()' in extract('static DFBResult hooked_input_get_event(')
print('PASS source: bind-based readiness, discovery wire unchanged, duplicate raw touch paths gated')
