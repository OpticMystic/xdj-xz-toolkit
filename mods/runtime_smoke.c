#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <unistd.h>

static void crash(int signal, siginfo_t *info, void *context) {
    const ucontext_t *state = context;
    uintptr_t words[] = {(uintptr_t)signal, (uintptr_t)info->si_addr,
                        state->uc_mcontext.arm_pc, state->uc_mcontext.arm_lr,
                        state->uc_mcontext.arm_r0, state->uc_mcontext.arm_sp};
    char line[128] = "CRASH signal,address,pc,lr,r0,sp:";
    size_t n = sizeof("CRASH signal,address,pc,lr,r0,sp:") - 1;
    const char *digits = "0123456789abcdef";
    for (unsigned i = 0; i < 6; i++) {
        line[n++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4) line[n++] = digits[(words[i] >> shift) & 15];
    }
    line[n++] = '\n';
    write(2, line, n);
    _exit(128 + signal);
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    struct sigaction handler = {0};
    handler.sa_sigaction = crash;
    handler.sa_flags = SA_SIGINFO;
    sigemptyset(&handler.sa_mask);
    sigaction(SIGSEGV, &handler, NULL);
    sigaction(SIGILL, &handler, NULL);
    sigaction(SIGBUS, &handler, NULL);
    puts("runtime-smoke: started"); fflush(stdout);
    setenv("XZ_MODS_ENABLE", "1", 1);
    setenv("XZ_MODS_OBSERVER", "1", 1);
    setenv("XZ_MODS_GATE_CUE", "1", 1);
    puts("runtime-smoke: loading"); fflush(stdout);
    void *receiver = NULL;
    if (argc == 3) {
        receiver = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
        if (!receiver) { fprintf(stderr, "receiver load failed: %s\n", dlerror()); return 1; }
        if (!dlsym(receiver, "xz_vj_touch_v1")) return 1;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }
    puts("runtime-smoke: loaded"); fflush(stdout);
    dlclose(library);
    if (receiver) dlclose(receiver);
    puts("PASS: ARM runtime loads and stays inert outside the verified application");
    return 0;
}
