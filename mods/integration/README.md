# VJ.Tools support for XDJ-XZ mods

This reusable support package bundles the existing v49 DirectFB receiver unchanged. Another XZ mod can include this folder and call the sourced loader before its normal application launch. It adds the existing VJ.Tools connection and discovery protocol; it does not implement standalone stems or install firmware.

## Integrate

Copy this complete bundle into your own RAM-boot payload or USB mod folder, then source its loader from your existing startup script:

```sh
. /path/to/vjtools-xz-support/vjtools-xz-loader.sh
vjtools_xz_enable /path/to/vjtools-xz-support || exit 1
# Continue your mod's existing application launch here.
```

Sourcing defines functions only. The explicit enable call checks the pinned receiver MD5 using the native utility, stages an owned read-only copy at `/dev/shm/vjtools-xz-support/<sha256>/libvjtools-xz-receiver.so`, and appends that exact path to `LD_PRELOAD` without replacing other mods or adding it twice. It neither launches nor stops the application, changes network settings, nor flashes anything. Remove the enable call to omit support on the next boot. RAM staging disappears on reboot.

Paths passed to the loader must be absolute and must not contain spaces, colons, newlines or `..`. The package manifest also records the host-verified SHA-256. Required target commands are POSIX shell, md5sum, mkdir, mktemp, cp, chmod, mv, rm and rmdir (BusyBox provides them). The loader's external staging commands run with empty LD_PRELOAD to avoid injecting the caller's libraries into file utilities; the caller's preload environment is preserved for the final application. Other mods that interpose DirectFB or input must chain the original/next implementation. Combining independently working hooks still requires testing together.

## Existing protocol

Receiver TCP50005 and discovery UDP50006 are unchanged. VJ.Tools sends `VJDISCOVER`; this receiver replies `VJXZ 1 <ip> 50005`. The same VJFS frame, VJVP preview, VJFA filmstrip asset, VJFT tick and VJTE touch messages remain available. Connect using VJ.Tools' existing XDJ-XZ discovery/output controls on the same network. No new cloud service or external account is required.

The prebuilt binary targets the existing XDJ-XZ1.26 DirectFB environment. Its ARM32 hard-float ELF flag is preserved exactly; it is not replaced with the new standalone mod runtime. Manifest hash/ELF checks establish the packaged binary identity. This packaging task does not constitute a new combined-mod hardware acceptance run.

## Contents and provenance

`manifest.json` lists exact hashes, protocol identifiers and source provenance. `source/xz_directfb_hook.c` is the pinned repository receiver source carrying `SPDX-License-Identifier: MPL-2.0`; see `NOTICE-MPL-2.0.txt` and the [Mozilla license](https://mozilla.org/MPL/2.0/). Source-to-prebuilt reproducibility has not been established here. New loader/package code follows `LICENSE-MIT`.

Only the allowlisted receiver, source, loader, manifest and notices are packaged. Firmware, rbp executables, keys, firmware archives, credentials, catalogs and user media are excluded. Building creates a local folder and does not publish it.

From the VJ.Tools repository:

```powershell
python packages/xdj-xz-toolkit/mods/integration/build.py --output .tmp/vjtools-xz-support
python packages/xdj-xz-toolkit/mods/integration/verify.py --shell 'C:/Program Files/Git/bin/bash.exe'
```

Host tests use a temporary staging root permitted only with `VJTOOLS_XZ_TESTING=1`; production integration should never set that variable or pass the optional second argument.
