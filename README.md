# xdj-xz-toolkit

Public XDJ-XZ USB builder inputs and ARM display-hook sources for firmware
1.26. Version 0.1.5 is a developer preview; final native-player and cold-boot qualification remains pending.

## What this is not

This repository never ships firmware, boot keys, decrypted images, extracted
`rbp` binaries, loader data, model weights, or built device artifacts. The
standalone app downloads the pinned official firmware and boot support from
manufacturer sources when preparing a USB; local inputs remain available:

- Official XDJ-XZ v1.26 ZIP (extract `XDJXZ.UPD` locally):
  https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip
- A local boot key (`--key <aes256.key>`, or `vendor/keys/aes256.key`) remains
  an advanced input. Automatic preparation extracts verified boot support from
  Pioneer's published source archive into the app-local cache.
- Separation model checkpoints listed in `builder/models.json` (downloaded
  from their original sources when requested)

Only the exact 1.26 archive identity pinned in `builder/firmware.py` is
accepted; unknown versions fail closed.

## Layout

- `builder/` — Python image builder (`firmware.py`, `usb.py`, `cache.py`,
  `inference.py`, `models.json`, `entry.py`, `service.py`)
- `mods/` — device mod/receiver C source, ABI maps, UI, sync helpers
- `vendor/tools/` — `xz_cli.py`, `xz_firmware/`, `xz_patcher/`,
  `xz_runtime/` (hook source, `Dockerfile.cross`, `orchestrator.sh`),
  `xz_stems/`, `xz_waveform/`, `xz_gui/`
- `vendor/build/` — `dfb-generated/` + DirectFB 1.4 headers only (see
  `vendor/build/directfb-1.4-src/PROVENANCE.md`); built `.so`/patch/loader
  outputs are local-only (see `vendor/build/README.md`)
- `tests/` — `test_toolkit.py` (public closure: tooling present, private
  inputs absent), `test_mod_payload.py` (payload/launcher contract)

## Current development source

The September 24 source adds two 48-pixel stem rows, horizontal volume gestures,
streamlined MODS navigation, native interface themes and corrected physical pad
brightness across both banks. Split rows, clean playback and bright pads were
confirmed in RAM trials, including the final primary pad colors. Theme coverage and
cold-boot/two-deck qualification remain pending. The standalone 0.1.4 download
includes this runtime and the automatic loading artwork. Rebuild older USB loaders to update them.

See the [user guide and actual hardware screenshots](https://github.com/OpticMystic/XZ-Mods/blob/main/docs/USER-GUIDE.md)
and [website](https://vj.tools/xz-mods) for setup and controls.

Related projects: [CDJ3K-Mods](https://cdj3k-mods.com/),
[OverCue](https://overcue.gg/), [XDJ-RX3 Toolkit](https://github.com/Tratosca/rx3-toolkit)
and [XDJ-AZ Mods](https://github.com/Kyle-Hosman/xdj-az-mods).
Each project has separate hardware and installation requirements.

## Run tests

```powershell
python -m unittest tests.test_toolkit tests.test_mod_payload -v
```

Requires `cryptography` + `pycdlib` (see `requirements.txt`).

## Rebuild the ARM hook

```powershell
powershell -ExecutionPolicy Bypass -File vendor/tools/xz_runtime/build_hook.ps1 -Output build/libxz-directfb-hook-repo-built-abi14.so
powershell -ExecutionPolicy Bypass -File vendor/tools/xz_runtime/build_gui_ip_patch.ps1 -Output build/xz-gui-ip-patch-repo-built
```

Docker cross-compile fails closed above GLIBC_2.4 (the deck's ceiling).

## Companion app

Pair with [XZ-Mods](https://github.com/OpticMystic/XZ-Mods) (standalone
Tauri builder app). Clone both side by side, or point the app at this
checkout with `--toolkit <path>` / `$XZ_TOOLKIT_DIR`.

## License

MIT (`LICENSE`). Mixed third-party notices in `THIRD-PARTY-NOTICES.md` —
retain MPL-2.0/OFL/LGPL texts with the covered files.

## OverCue v4 and the current fixes

The runtime reads existing `CDJMODS/index.json` and `overcue-stems/4` bundles.
It verifies original track identity and every decoded page, then mixes prepared
96 kHz stereo audio using bounded background windows. The native screen keeps
both waveforms and adds stem controls. Each deck has independent pad state;
the STEMS overlay toggle does not disable audio or pads. Loop-start caching
retains current and pending selections without restoring muted stems.

See [the format and runtime contract](mods/audio/OVERCUE.md) and
[the companion changelog](https://github.com/OpticMystic/XZ-Mods/blob/main/CHANGELOG.md).
`builder/build_overcue_check.py` builds the read-only desktop compatibility
checker from the same decoder. The separation workflow still writes legacy
stemd caches; it does not export OverCue bundles.

The 0.1.2 preview adds native spare-channel EQ, connection-gated VJ.Tools access
and selectable stem banks. See [the controls contract](mods/ui/README.md) for
USB-source lockouts, pickup and the hardware acceptance boundary. Both the
standalone builder and Library loader validate the same paired runtime and its
native sources.
