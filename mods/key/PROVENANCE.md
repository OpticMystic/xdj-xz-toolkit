# Key-shift provenance and limits

`keyshift.c` adapts the allocation-free time-domain shifter from
`E:/Github/XDJXZMe/rx3-toolkit/mod/modules/keyshift/1.19/rx3_pitch_shift.h`,
SHA-256 `957e5feb8903e34f9d32a05f797b7447dca2e0f0a2ffb12efb059b984692f288`.
The source carries `SPDX-License-Identifier: MPL-2.0`; this port keeps the same
license. The source snapshot is not a Git checkout, so the file hash is the
reproducible identity available here. `LICENSE-MPL-2.0` contains the source
license text and has SHA-256
`1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5`.

The port retains the two half-grain read heads, Hann overlap, cubic reads,
backward correlation alignment, direction-specific 512/2048-frame grains,
equal-temperament ratios, and per-frame smoothing. It renames the RX3 symbols,
moves history and the Hann table into each deck state, uses a 64-bit lifetime
counter, validates the XZ 512-frame block limit, and removes lazy shared table
initialization.

No code came from `rx3_keyshift.h`. That file combines the portable shifter with
RX3 hook addresses, object layouts, firmware pitch calls, labels, and logging.
None of those details apply to XDJ-XZ.

This engine processes sequential interleaved-stereo float blocks at 44.1 kHz.
It does not identify a deck, install a hook, read track metadata, change tempo,
or implement Key Sync. The custom shifter can add level ripple or doubled
transients on large upward shifts, and downward shifts have weaker tone purity.
Host tone tests measure the port, not device audio quality. XZ support still
requires the verified post-`TimeStretchManager::operate` adapter, two-deck CPU
and memory measurements, and physical listening/recording tests.

Run the supported source checks from the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/key/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
```

The verifier rejects disabled assertions, runs optimized host tone and state
tests, and compiles the engine for ARM32 soft-float. It never opens a device.
