# XDJ-XZ mod development

This is an in-progress standalone port for firmware 1.26. It preserves the
CDJ3K Mods stem-cache format and adds a native VJ.Tools connection page. It is
not a complete firmware release.

## Reusable VJ.Tools support

[integration/README.md](integration/README.md) describes the receiver module that
other XZ mod loaders can include. It packages the unchanged v49 receiver, source,
loader and notices. It contains no firmware application or keys.

The loader was tested on the physical XZ in an isolated RAM directory. It staged
the correct receiver, preserved an existing preload value, and added its own
path only once. This did not restart the DJ application. The receiver protocol
remains TCP 50005 and UDP discovery 50006.

## Port status

| Feature | Implemented work | Remaining acceptance or implementation |
| --- | --- | --- |
| Gate and smart cues | Native adapters and state machine; active-assertion tests | Physical press, hold, release, slip and quantize behavior |
| Preview hot cue | Upstream specification and native ABI investigation | Native registration without moving playback |
| Pad lights | Control model | Native lamp ownership and updates |
| Stems | Compatible cache, real FLAC/WAV decoder, residual mixer, guarded two-deck runtime; initial vocal/drum/bypass listening checks passed | Recorded alignment, two-deck stress and other source formats; reported fader issue resolved by user |
| Stem waveform | Display model and upstream contract | Native waveform data and level-dependent update |
| Stem status | Runtime readiness and UI status | Physical display verification |
| Offline cache | Exact upstream identity, paths, metadata and gain restoration | Cross-device decoder-padding compatibility beyond qualified inputs |
| Network stems | Existing upstream protocol documented | Separation-server runtime integration |
| Groove Circuit | Portable replacement DSP and controls | Bank lifetime, native grid binding and pad ownership |
| X-PAD | Portable DSP, eight voices, controls | Native output hook, banks, overdub recording and storage |
| Themes | Seven mod-panel palettes | Stock-screen recoloring |
| Playlist reorder | Feature specification | Native database mutation and persistence |
| Grid adjustment | Exact native beat-array investigation | Editing, persistence and controls |
| Mod settings | Session controls and capability states | Persistence and server address editor |
| VJ.Tools | Reusable receiver package and optional combined display bridge | Combined native UI/input acceptance with active playback |
| Hybrid sync | Selected XZ player to Link bridge with real-daemon test | Physical Serato/rekordbox audio sync, master handoff and reverse direction |
| Key shift and key sync | Tested portable shifter and opt-in native output adapter | Key metadata/sync and physical quality tests; not active in the stems test build |

The initial native stem adapter accepts matching 44.1 kHz WAV/FLAC source and
cache timelines and caps decoded PCM at 128 MiB per deck. These limits are
explicit development constraints, not full upstream parity.

## Build and verify

Install the Python dependencies already used by the toolkit plus `pyelftools`
and `capstone`. Use a Zig compiler with the `arm-linux-gnueabi.2.13` target.

```powershell
python packages/xdj-xz-toolkit/mods/build.py --zig <zig.exe> --output <build-directory>
python -m unittest discover -s packages/xdj-xz-toolkit/tests -v
python packages/xdj-xz-toolkit/mods/cue/verify.py --zig <zig.exe>
```

The build checks ARM32 calling conventions, GLIBC_2.4 imports, retained hook
libraries, and adjacent relocation tables. The last check prevents a reproduced
glibc 2.13 loader failure with modern linker output. Tests reject builds that
disable assertions.

The audio verifiers in `audio/tests/` cover cache compatibility against compiled
upstream code, decoded FLAC/WAV samples, enabled and disabled native-shaped
audio calls, publication races, and portable performance DSP. The sync verifier
can use the existing Link daemon. These checks do not replace physical controls
and recorded audio acceptance.

`device_smoke.py` stages isolated test programs in a fresh RAM directory and
checks that the existing DJ process survives unchanged. It does not activate
native hooks. `private_usb.py` requires exact-artifact smoke evidence and builds
a private observer or experimental `autoexec.bin`, then reads its contents back
to verify the encrypted package. These images contain the firmware application
and are distinct from the reusable support package.

## Evidence

- [WORK.md](WORK.md): full-port work list and current physical gate.
- [decisions.tsv](decisions.tsv): decisions and verification results.
- [abi/README.md](abi/README.md): inspected firmware and input ABI.
- [audio/PROVENANCE.md](audio/PROVENANCE.md): upstream code and runtime contracts.
- [ui/README.md](ui/README.md): native control renderer and action contract.
- [sync/README.md](sync/README.md): hybrid clock direction and limits.

Upstream feature source: [nsaintot/cdj3k-mods](https://github.com/nsaintot/cdj3k-mods/tree/e74e199603e2a25567950ca72997c38d17ada4e8).
Retain the source-specific notices when reusing these components.
