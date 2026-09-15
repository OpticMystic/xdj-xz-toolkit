# Upstream compatibility

The cache key, cache layout and residual mix derive from nsaintot/cdj3k-mods,
commit `e74e199603e2a25567950ca72997c38d17ada4e8`, under its
`MIT OR Apache-2.0` source license notices.

- `package/deck/mods/stem/cache.c`: `key_of`, `fnv1a`, `meta_write`, `entry_path`.
- `package/deck/mods/stem/audio_mix.c`: residual formula and unity bypass.
- `package/deck/mods/stem/store.c`: normalization gain restoration.
- `package/deck/mods/stem/job_loader.c`: keys use decoded length at 44100 Hz.

The cache remains `mods/stemd-cache/<separation-id>/<first-two-key-digits>/<key>/`
with `meta`, `harmonics.flac` or `.wav`, and `vocals.flac` or `.wav`. The metadata
contains `v=1`, `frames`, `harmonics`, and `vocals`. No conversion or new container
format is introduced. Keys use upstream's actual FNV offset basis, explicit
little-endian 64-bit size and frame count, and the first and last 64 KiB windows.

`tests/verify_upstream.py` extracts and compiles the pinned upstream key and meta
writer and compares their output to this implementation, including a sparse file
larger than 4 GiB. Payload fixture bytes only test path selection. They do not
prove FLAC/WAV decoding. The C mixer tests cover bit-exact unity, restored gains
above full scale, residual drums, seek offsets, and uncovered tail preservation.

`stem_decode.c` uses the unmodified single-header FLAC and WAV decoders from
[mackron/dr_libs](https://github.com/mackron/dr_libs), pinned at
`dfe8377631000664666519fdb83da193fd8037f4`. Its license and original source notices
are retained in `vendor/dr_libs`. The worker decodes stereo files in 4096-frame
chunks directly into a combined resident PCM allocation capped by the caller.
The cap excludes decoder state. Cancellation and failures release both buffers.
`tests/verify_decode.py` generates real FLAC and WAV fixtures with ffmpeg and
checks every decoded sample, the combined PCM budget, rate gate and cancellation.

This decoder requires both cache files to match the measured stock reader rate.
It does not resample or assume a CDJ pool rate. The caller must determine the
original decoder's padded 44100 Hz length for key lookup and establish alignment
between cached frame zero and the stock source reader's timeline. Using compressed
duration or the DAC rate as a substitute changes keys or alignment.

The adapter retains decoded PCM lifetime across audio calls. This slice does not
install hooks, identify decks or render controls. Those are required adapter work,
not functionality established by these tests. No physical playback was tested.

# XZ 1.26 source reader ABI

`native_reader.c` follows the ARM32 disassembly of the local firmware `rbp`,
SHA-256 `6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2`.
Offsets below are byte offsets. They are not scaled CDJ-3000 offsets.

| Evidence function | Address | Observed access |
| --- | --- | --- |
| DjEngineIF::getFilePath | `0x3fce0` | PlayEngine pointer cell `0x010e9688`; ignores `this` |
| PlayEngine::getFilePath | `0x49234` | Player vector start `+0xc`, end `+0x10`, four-byte indexed pointers |
| Player::getFilePath | `0x610cc` | Reader pointer `+0xc8` |
| Player::getTSMLoopInPoint | `0x6489c` | Embedded manager `+0xd0` |
| TimeStretchManager::setReader | `0x76178` | Manager reader `+0x5c`; each subengine reader `+4` |
| ReaderOwner::getFilePath | `0x3ae08` | ReaderImpl `+4`; its first bytes contain the path |
| ReaderOwner::getFileReader | `0x3ae38` | ReaderImpl file reader `+0x694` |
| ReaderOwner::getLength | `0x3ae88` | Converter at impl `+0x69c`; length converter `+8` or file reader `+0x14` |
| ReaderOwner::loadFile | `0x3ab60` | Owner output rate `+0xc`, source rate file reader `+0x18`, converter rate `+0xc` |
| SampleRateConverter::Init | `0x3b14c` | File reader length `+0x14`, source rate `+0x18`, target rate converter `+0xc` |

Player construction stores one PcmReader at `+0xc8` at `0x6f0c4`. Track changes
reuse it. `PcmReader::load` at `0x34ba0` has signature
`int(self, const StTrackInfo*)`; zero means success. Its first eight bytes are
`f04f2de95cd04de2`, a PC-independent push and stack subtraction. A loader hook
must cancel the previous generation before calling the original and capture the
new source after success. `ReaderOwner::loadFile` replaces `ReaderImpl`, but
allocator reuse means pointer comparison alone cannot identify a track generation.
Its first instruction uses a PC-relative literal and must not be copied into a
trampoline without relocation.

The manager may be unbound when the initial `PcmReader::load` returns.
`Player::eventLoadFile` calls `setReader` at `0x6e2b0`. The snapshot explicitly
reports `manager_bound`; it does not mistake an unbound manager for readiness.
The read callback must handle invalid process addresses. These helpers only
compute and read data; the caller owns process lifetime and firmware verification.

`TimeStretch::getStreamAt` at `0x8fd5c` takes a signed 32-bit source position,
stereo float buffer and unsigned 32-bit requested count. Its first bytes are
`f8402de90040a0e1`. It fills the requested buffer and zero-fills samples outside
the track. Its return register is not a reliable count or success flag.
`PcmReader::getStreamAt` at `0x38d28` returns the requested count even when it
fills silence because data is unavailable. `xz126_source_extent` clips only
track boundaries. It does not establish ring-buffer availability after a seek.
The native hook must separately verify source readiness and current generation
before adding residual stems, and retain the original function's return value.

# Runtime verification

Run `python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_runtime.py` on a Linux
host with GCC or Clang. It compiles the actual runtime with mocked firmware
access under ASan and UBSan. A deliberate failing assertion verifies that tests
are active before the runtime checks begin. All executables live in a temporary
directory; the tests never open a device or modify firmware.

The tests cover successful disabled startup, original-call passthrough without
native pointer access when disabled, rejection of internal decks 3 and 4,
load/unload generation cancellation before the original call, captured source
history, opt-in decoding eligibility and unity restoration on disable. Two
reader threads race 1,000 publications against retirement to check PCM lifetime.
An additional low-address private `mmap` fixture executes the real enabled source
hook with ARM32-shaped reader fields and a mocked original read. It verifies
residual sample values, independent deck levels, preserved original return value,
unchanged leading/trailing zero fill and bypass for unavailable or changing ring
coverage, mismatched identity, changed generation or disable during the read.
The fixture asserts the audio path never calls the guarded memory-read bridge.
The mapping never replaces an existing allocation; inability to obtain a low
address fails the test. These are host tests and do not establish physical alignment.

# Portable performance DSP

`performance.c` adapts these functions from the same pinned cdj3k-mods commit:

- `mods/stem/loop.c`: loop phase, wrap and grid beat interpolation.
- `mods/stem/audio_mix.c`: Groove Circuit replacement equations and interpolated loop reads.
- `mods/xpad/audio.c`: two-head pitch reads, window curve, Q32 phase, per-voice pitch
  retention, 49.5 ms glide, 26.67/31.35 ms windows, roll boundary claims and frame placement.
- `mods/xpad/ui.c`: roll lengths of 1/16, 1/8, 1/4, 1/2, 1 and 2 beats.

Groove Circuit has an explicit pre-stretch API. X-PAD has an explicit post-stretch
API and eight audio-thread-owned voices. Clock helpers must use separate state for
input reads and output blocks. Borrowed bank and grid memory must remain alive for
the whole call. These functions do not install firmware hooks or acquire native
objects. The adapter still needs bank loading/publication, sequencer recording and
storage, controls, validated grid data and a post-stretch hook. No standalone
Groove Circuit or X-PAD runtime delivery is claimed by these portable functions.

Run `python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_performance.py /path/to/cdj3k-mods`.
The verifier compiles the pinned upstream pitch and phase functions as an oracle
and compares rendered samples at -12, 0 and +12 semitones, including retained pitch
after releasing the gesture. It also checks all three replacement equations, eight
simultaneous voices, immediate roll/boundary suppression, frame-exact roll placement,
and the paused transport clock. Loop wrapping uses `fmod` and beat differences use
double subtraction to avoid undefined integer conversion/overflow outside native
position ranges; the oracle verifies ordinary source positions retain upstream results.
