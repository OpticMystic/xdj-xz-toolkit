#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error assertions required
#endif
#include <assert.h>
#include "../runtime.c"
#include <sys/mman.h>
static unsigned char test_memory[65536];
static uint32_t expect_generation;
static int load_result;
static unsigned hook_count, stream_calls, guarded_read_calls;
static uint32_t stream_reader;
static int stream_fill, stream_action;
enum { FILL_ONLY, CHANGE_RING, CHANGE_GENERATION, DISABLE_DURING_READ, CHANGE_READY };
static int fake_load(void *self, const void *track);
static void fake_unload(void *self);
static uint32_t fake_stream(void *self, int32_t position, void *buffer, uint32_t frames)
{
    (void)self;
    ++stream_calls;
    if (stream_fill) {
        float *pcm = buffer;
        for (uint32_t i = 0; i < frames; ++i) {
            int64_t source = (int64_t)position + i;
            pcm[i * 2] = pcm[i * 2 + 1] = source >= 0 && source < 2 ? 0.75f : 0;
        }
        if (stream_action == CHANGE_RING)
            __atomic_store_n((uint32_t *)(uintptr_t)(stream_reader + 0x84), 3, __ATOMIC_RELAXED);
        if (stream_action == CHANGE_GENERATION)
            __atomic_add_fetch(&decks[0].generation, 1, __ATOMIC_SEQ_CST);
        if (stream_action == DISABLE_DURING_READ) xz_audio_set_enabled(0);
        if (stream_action == CHANGE_READY)
            __atomic_store_n((unsigned char *)(uintptr_t)(stream_reader + 0x69), 0, __ATOMIC_RELAXED);
    }
    return 0x12345678;
}
static void put(uint32_t at,uint32_t value) { memcpy(test_memory+at-0x10000,&value,4); }
int xz_read_memory(uint32_t at,void*out,size_t n) {
 ++guarded_read_calls;
 if(at==XZ126_PLAY_ENGINE_CELL && n==4) { uint32_t value=0x10000;memcpy(out,&value,4);return 0; }
 if(at<0x10000 || (uint64_t)at+n>0x20000)return -1;
 memcpy(out,test_memory+at-0x10000,n);return 0;
}
void xz_log(const char *s) {(void)s;}
int xz_hook_arm(uint32_t address, const unsigned char guard[8], void *replacement, void **original)
{
    (void)guard;
    assert(replacement && original);
    switch (address) {
    case 0x34ba0: *original = (void *)fake_load; break;
    case 0x348dc: *original = (void *)fake_unload; break;
    case 0x8fd5c: *original = (void *)fake_stream; break;
    default: assert(0);
    }
    ++hook_count;
    return 0;
}
static int fake_load(void*self,const void*track) {
 (void)track;assert((uintptr_t)self==0x11200);
 assert(decks[0].generation==expect_generation);
 if(decks[0].published)assert(decks[0].published->generation!=decks[0].generation);
 strcpy((char*)test_memory+0x1300,"/media/usb/new.flac");return load_result;
}
static void fake_unload(void*self) {(void)self;assert(decks[0].generation==expect_generation);}
static void setup(void) {
 put(0x1000c,0x10100);put(0x10010,0x10108);put(0x10100,0x11000);put(0x10104,0x12000);
 put(0x110c8,0x11200);put(0x1112c,0x11200);put(0x11204,0x11300);put(0x1120c,44100);
 put(0x11994,0x11a00);put(0x11a14,12345);put(0x11a18,44100);
 strcpy((char*)test_memory+0x1300,"/media/usb/old.flac");
}

static struct deck_state concurrent;
static unsigned test_run, observed;
static void *race_reader(void *unused) {
 (void)unused;
 while(__atomic_load_n(&test_run,__ATOMIC_ACQUIRE)) {
  struct publication *p=acquire(&concurrent);
  if(p) { assert(p->generation==42); __atomic_fetch_add(&observed,1,__ATOMIC_RELAXED); release(&concurrent); }
 }
 return NULL;
}
static void race_test(void) {
 pthread_t a,b; unsigned i;
 __atomic_store_n(&test_run,1,__ATOMIC_RELEASE);
 assert(!pthread_create(&a,NULL,race_reader,NULL));assert(!pthread_create(&b,NULL,race_reader,NULL));
 for(i=0;i<1000;++i) {
  struct publication *p=calloc(1,sizeof(*p));assert(p);p->generation=42;
  __atomic_store_n(&concurrent.published,p,__ATOMIC_SEQ_CST);
  sched_yield();free_publication(retire(&concurrent));
 }
 __atomic_store_n(&test_run,0,__ATOMIC_RELEASE);pthread_join(a,NULL);pthread_join(b,NULL);
 assert(observed>0 && concurrent.users==0 && !concurrent.published);
 puts("concurrent publication retirement passed with two readers");
}

static void disabled_start_test(void)
{
    struct xz_audio_status status;
    float untouched[] = {1, 2, 3, 4};
    _Static_assert(DECKS == 2, "Only two standalone internal decks are supported");
    assert(!enabled);
    assert(xz_audio_start() == 0);
    assert(hook_count == 3 && !enabled && worker_created);
    assert(xz_audio_get_status(0, &status) == 0 && status.state == XZ_AUDIO_DISABLED);
    assert(xz_audio_get_status(2, &status) == -1);
    assert(xz_audio_get_status(3, &status) == -1);
    xz_audio_set_levels(2, (struct xz_stem_levels){0, 0, 0});
    /* Deliberately invalid native pointer proves disabled execution never reads
     * native memory and forwards the original result without changing PCM. */
    assert(stream_hook((void *)(uintptr_t)0xdeadbeef, 0, untouched, 2) == 0x12345678);
    assert(stream_calls == 1 && untouched[0] == 1 && untouched[3] == 4);
    xz_audio_set_enabled(1);
    assert(xz_audio_get_status(0, &status) == 0 && status.state == XZ_AUDIO_WAITING_TRACK);
    xz_audio_stop();
    assert(!running && !enabled && !worker_created);
    puts("successful startup is disabled; unknown loaded tracks require reload; decks 3/4 rejected");
}

/* ARM32 stores native pointers in four bytes. Allocate an ordinary private
 * mapping below 4 GiB so the real hook's direct loads run unchanged on a 64-bit
 * Linux host. MAP_32BIT is used where available; elsewhere a low address hint
 * is accepted only if the kernel actually returns a suitable address. No fixed
 * mapping replaces an existing mapping. Failure is a failed test, not a skip. */
static unsigned char *low_native_fixture(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    unsigned char *memory;
#ifdef MAP_32BIT
    flags |= MAP_32BIT;
#endif
    memory = mmap((void *)(uintptr_t)0x20000000, 4096, PROT_READ | PROT_WRITE, flags, -1, 0);
    assert(memory != MAP_FAILED);
    assert((uintptr_t)memory >= 0x10000 && (uintptr_t)memory <= UINT32_MAX - 4096);
    return memory;
}

static void native_put(uint32_t at, uint32_t value)
{
    __atomic_store_n((uint32_t *)(uintptr_t)at, value, __ATOMIC_RELAXED);
}

static void ready_publication(struct publication *p, unsigned char *memory)
{
    uint32_t self = (uint32_t)(uintptr_t)memory;
    stream_reader = self + 0x100;
    memset(memory, 0, 4096);
    native_put(self + 4, stream_reader);
    native_put(stream_reader, PCM_READER_VPTR);
    native_put(stream_reader + 4, self + 0x300);
    native_put(stream_reader + 0x6c, 0);
    native_put(stream_reader + 0x84, 2);
    native_put(stream_reader + 0x88, 0);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x9c) = 1;
    *(unsigned char *)(uintptr_t)(stream_reader + 0x69) = 1;
    p->reader = stream_reader;
    p->reader_impl = self + 0x300;
    p->reader_frames = 2;
    p->generation = 42;
    __atomic_store_n(&decks[0].generation, 42, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[0].published, p, __ATOMIC_SEQ_CST);
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
    xz_audio_set_levels(0, (struct xz_stem_levels){0.5f, 1.5f, 0});
    stream_fill = 1;
    stream_action = FILL_ONLY;
}

static void exercise_enabled_stream(unsigned char *self, int32_t position, int expect_mix,
                                      const int16_t *h, const int16_t *v)
{
    float out[8];
    unsigned calls = stream_calls, reads = guarded_read_calls;
    for (unsigned i = 0; i < 8; ++i) out[i] = -12345;
    assert(stream_hook(self, position, out, 4) == 0x12345678);
    assert(stream_calls == calls + 1);
    assert(guarded_read_calls == reads); /* no guarded pread bridge on audio path */
    assert(decks[0].users == 0 && decks[1].users == 0);
    for (unsigned frame = 0; frame < 4; ++frame) {
        int64_t source = (int64_t)position + frame;
        for (unsigned channel = 0; channel < 2; ++channel) {
            float expected = 0;
            if (source >= 0 && source < 2) {
                expected = 0.75f;
                if (expect_mix) {
                    size_t k = (size_t)source * 2 + channel;
                    expected = 0.375f + (float)h[k] / 32767.0f -
                               0.5f * (float)v[k] / 32767.0f;
                }
            }
            assert(fabsf(out[frame * 2 + channel] - expected) < 1e-6f);
        }
    }
}

static void enabled_stream_test(void)
{
    static const int16_t h[] = {16384, -16384, 8192, -8192};
    static const int16_t v[] = {4096, 4096, -4096, -4096};
    unsigned char *memory = low_native_fixture();
    struct publication p = {0};
    assert(!worker_created);
    memset(decks, 0, sizeof(decks));
    p.decoded.pcm = (struct xz_stem_pcm){h, v, 2, 1, 1};

    ready_publication(&p, memory);
    exercise_enabled_stream(memory, 0, 1, h, v); /* mixed head, untouched zero tail */
    assert(decks[0].mixed == 1);
    exercise_enabled_stream(memory, -1, 1, h, v); /* leading zero and trailing zero */
    assert(decks[0].mixed == 2);

    /* The same native fixture can represent deck 2 only when its publication
     * belongs to deck 2. Deck 1's changed levels must not bleed into its unity. */
    __atomic_store_n(&decks[0].published, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[1].generation, 42, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[1].published, &p, __ATOMIC_SEQ_CST);
    xz_audio_set_levels(1, (struct xz_stem_levels){1, 1, 1});
    exercise_enabled_stream(memory, 0, 0, h, v);
    xz_audio_set_levels(1, (struct xz_stem_levels){0.5f, 1.5f, 0});
    exercise_enabled_stream(memory, 0, 1, h, v);
    assert(decks[0].mixed == 2 && decks[1].mixed == 1);
    __atomic_store_n(&decks[1].published, NULL, __ATOMIC_SEQ_CST);

    ready_publication(&p, memory);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x69) = 0;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x9c) = 0;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader + 0x84, 1); /* only part of requested source is buffered */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader + 4, p.reader_impl + 4); /* wrong track owner */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader, PCM_READER_VPTR + 4); /* unexpected native reader class */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    __atomic_add_fetch(&decks[0].generation, 1, __ATOMIC_SEQ_CST);
    exercise_enabled_stream(memory, 0, 0, h, v); /* stale before original read */

    ready_publication(&p, memory); stream_action = CHANGE_RING;
    exercise_enabled_stream(memory, 0, 0, h, v); /* coverage still sufficient, state changed */
    ready_publication(&p, memory); stream_action = CHANGE_READY;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory); stream_action = CHANGE_GENERATION;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory); stream_action = DISABLE_DURING_READ;
    exercise_enabled_stream(memory, 0, 0, h, v);
    assert(!enabled && decks[0].mixed == 2);

    __atomic_store_n(&decks[0].published, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    stream_fill = 0;
    assert(munmap(memory, 4096) == 0);
    memset(decks, 0, sizeof(decks));
    puts("real enabled stream hook mixed expected samples; preserved return and zero fill; readiness, identity, generation and disable races bypassed");
}

int main(void) {
 disabled_start_test();
 enabled_stream_test();
 race_test();
 struct publication p={0};struct publication *held;uint32_t output_rate,output_generation;
 assert(xz_audio_lifecycle_self_test()==0);
 setup();decks[0].generation=p.generation=7;decks[0].published=&p;
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==-1);
 original_load=fake_load;original_unload=fake_unload;running=1;enabled=1;
 held=acquire(&decks[0]);assert(held==&p);
 expect_generation=8;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==0);
 assert(decks[0].pending_kind==1 && decks[0].pending_generation==8);
 assert(!strcmp(decks[0].pending.path,"/media/usb/new.flac"));
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==1);
 assert(held->generation!=decks[0].generation);release(&decks[0]);
 expect_generation=9;load_result=-5;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==-5);
 assert(decks[0].pending_kind==2 && decks[0].status.state==XZ_AUDIO_WAITING_TRACK);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==-1);
 expect_generation=10;unload_hook((void*)(uintptr_t)0x11200);
 assert(decks[0].pending_kind==2 && decks[0].status.state==XZ_AUDIO_WAITING_TRACK);
 assert(retire(&decks[0])==&p && decks[0].users==0);
 assert(acquire(&decks[0])==NULL);
 xz_audio_set_enabled(0);load_result=0;
 expect_generation=12;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==0);
 assert(decks[0].history_valid && decks[0].pending_kind==2 && !decks[0].published);
 assert(decks[0].status.state==XZ_AUDIO_DISABLED);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==4);
 xz_audio_set_enabled(1);assert(decks[0].pending_kind==1 && decks[0].pending_generation==12);
 xz_audio_set_levels(0,(struct xz_stem_levels){0,0.5f,2});
 xz_audio_set_enabled(0);assert(decks[0].pending_kind==2 && decks[0].pending_generation==13);
 assert(decks[0].history_valid && get_levels(&decks[0]).drums==1);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==4);
 puts("disabled loads retain history without decode; enable queues known history; disable cancels and restores unity");
 puts("mocked native load/unload cancellation precedes original; captured new path; failed load never publishes");
 return 0;
}
