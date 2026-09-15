# xdj-xz-toolkit

Public XDJ-XZ USB builder inputs and ARM display-hook sources for firmware
1.26. Developer preview — no live-hardware validation is claimed.

## What this is not

This repository never ships firmware, boot keys, decrypted images, extracted
`rbp` binaries, loader data, model weights, or built device artifacts. Those
are always user-supplied from their original publishers:

- Official XDJ-XZ v1.26 ZIP (extract `XDJXZ.UPD` locally):
  https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip
- Your local boot key (`--key <aes256.key>`, or `vendor/keys/aes256.key`,
  which is git-ignored — see `vendor/keys/README.md`)
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

## Test

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
