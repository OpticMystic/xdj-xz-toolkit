# XZ gate and smart cue port

`cue.c` implements independent per-deck gate sessions and smart-cue following. Both settings start off. It passes stock pad presses/releases through, updates smart CUE immediately after a successful stock press recalls a pre-existing cue, waits until stock release bookkeeping completes before gate return, ignores newly recorded/deleted/moved cue slots, supports PLAY latching, and clears sessions when settings or pad mode change. Native adapter is ARM32-only; it does not install hooks or modify device files.

Integration: create `xz_cue_native` with zero initialization, validate the exact executable and all target function bytes against `../abi/`, then set its `verified` field. Initialize the cue engine using `xz_cue_native_api`. At the verified physical-key vtable wrapper call decode, then before; only a return of1 consumes PLAY. Otherwise invoke stock and then `xz_cue_after(c, event, stock_result)`. Do not unload the module while its hook is installed. Serialize settings with the physical-key thread.

The native methods use firmware-specific addresses. `getCueTime` requires a hidden structure-return pointer and writes0x338 bytes; it is not a scalar getter. IN milliseconds are at0x318; INVALID_TIME is-1. `getPlayingTime`/`setPlayingTime` use milliseconds, with native ceil(samples/44.1)/floor(ms*44.1) rounding. Main memory-cue writes use native `setMemoryCueTime` kind0, preserving its current metadata while replacing IN and clearing loop OUT/flag. `pause` receives the same1,1,0 boolean tuple as the physical PLAY playing branch.

Proof: host test exercises disabled stock flow, smart-memory update while a pad remains held, overlapping release order, exact post-stock release ordering, latch, delete versus recall, newly recorded cue, settings disabled during a hold independent decks and native setter/pause/seek failures. Each deck records the latest native operation error in `last_error`; failed memory setters are not retried on release, and a failed pause prevents seeking. Native adapter compiles with Zig for ARMv7 cortex_a9 gnueabi. These are source/build checks only. Vtable reachability, native return values, exact sample positioning, smart-cue reservation/slip side effects and short-hold transport ordering still need device acceptance; `verified` must not be interpreted as that acceptance.

```powershell
.tmp/cdj-zig/ziglang/zig.exe cc -std=c11 -Wall -Wextra -Werror packages/xdj-xz-toolkit/mods/cue/cue.c packages/xdj-xz-toolkit/mods/cue/test_cue.c -o .tmp/xz-cue-abi/test_cue.exe
.tmp/xz-cue-abi/test_cue.exe
.tmp/cdj-zig/ziglang/zig.exe cc -target arm-linux-gnueabi -mcpu=cortex_a9 -std=c11 -Wall -Wextra -Werror -c packages/xdj-xz-toolkit/mods/cue/native.c -o .tmp/xz-cue-abi/native.o
```

Run the supported acceptance command with assertions explicitly enabled:

```powershell
python packages/xdj-xz-toolkit/mods/cue/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
```

This first checks that compiling with `-DNDEBUG` fails at the acceptance guard, then builds and runs the optimized suite with `-UNDEBUG`. A build that silently removes assertions cannot print a passing result.
