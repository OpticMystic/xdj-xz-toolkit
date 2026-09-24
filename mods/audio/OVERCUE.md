# Overcue prepared stems on the XDJ-XZ

The adapter reads the user's `CDJMODS/index.json` and `overcue-stems/4` bundles.
It does not use private Overcue source or CDJ-3000 firmware.

`overcue_file.c` matches the exact exported path, verifies the source SHA-256,
validates each PGZ page table, and checks every decompressed page. The observed
format is 96 kHz stereo signed-16-bit PCM in independently compressed zlib
pages. PWV3 analysis data supplies separate stem waveform amplitudes.

`overcue_stream.c` keeps two bounded PCM windows. File reads, decompression,
hashing, and alignment run on its worker. The audio callback does not allocate,
read files, lock a mutex, or wait. Unchanged full-mix playback retains the native
samples. A pending new selection keeps a covered previous mix or native
audio until its cache is ready. Once a mute is applied, a cache miss cannot
restore that stem. If all matching cache coverage is lost, the remaining
native-source contribution is retained and prepared contributions wait for data.
Binary mutes read one prepared combination; independent levels need at most two.
Only the owned background workers use CPUs 1-2; native audio affinity is unchanged.
Two additional two-second PCM windows retain the current and pending mix
around the native loop start. A mix change does not evict the previous loop audio.
Each cache slot has reader/writer ownership; callbacks only read a pinned slot
and never wait for disk or the worker. This allows immediate loop returns while
the forward windows refill. A loop changed or recalled without preparation time
can still require buffering; the hardware acceptance must check for audible gaps.

Prepared files are not assumed to align from metadata alone. The worker compares
native source samples with the prepared full mix over a bounded search. It
requires correlation above 0.995, an unambiguous peak, and a consistent result in
two separate sections. Fractional sample offsets are retained. Until then,
native audio continues and the UI reports alignment pending.

The native playback strip uses the qualified 536x268 waveform window. Both
scrolling waveforms retain their cue bands; a 64-row strip adds deck selection,
bypass, three toggles, levels, and prepared waveform overviews. Native waveform
touches use the inverse coordinate mapping. Captured strip gestures cannot leak
into stock controls, and a track change resets gains and cancels old gestures.

The STEMS button next to MODS toggles strip visibility without changing audio or
pad ownership. With stems enabled, HOT CUE A/B/C toggle Drums/Harmonics/Vocal,
and D toggles bypass on that physical deck. The configured additional pad page
also works. Either deck remains independent of panel visibility and displayed
deck. Turning the audio STEMS setting off restores normal cue dispatch.
The strip follows the native active deck after prepared stems become available;
manual selection lasts until native focus or the focused track changes.

The native draw hook has an observation mode, `XZ_MODS_INLINE=observe`. Verify
its exact caller, scene, dimensions, pitch, and lifecycle on hardware before
claiming the inline screen works. `xz_inline_proof_v1` and
`xz_prepared_proof_v1` expose bounded read-only diagnostic counters.

## Verification

```powershell
python mods/audio/tests/verify_overcue.py --zig C:\path\to\zig.exe
```

On Linux:

```sh
python3 mods/audio/tests/verify_overcue.py --cc gcc --sanitizer address
python3 mods/audio/tests/verify_overcue.py --cc 'gcc -fno-pie -no-pie' --sanitizer thread
python3 mods/audio/tests/verify_runtime.py
python3 mods/ui/verify_runtime_controls.py
```

The prepared audio is a development adapter. Portable tests and isolated ARM
tests do not establish real-song alignment, audible stem separation, seek
continuity, or physical inline-display acceptance. Record those separately.
