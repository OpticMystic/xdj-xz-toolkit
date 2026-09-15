# DirectFB 1.4 headers — provenance

The headers under `include/` (plus `dfb-generated/`) come from DirectFB 1.4
(directfb.org, Convergence GmbH), licensed LGPL-2.1-only. Each header carries
its own LGPL notice; no additional restriction is imposed.

Only headers (`.h`) and build templates (`.in`) needed for the ARM hook
compile (`-I` paths in `vendor/tools/xz_runtime/build_hook.ps1`) are vendored
here. The DirectFB implementation sources (`.c`/`.S`) are intentionally not
included — fetch upstream DirectFB 1.4 if you need them.

This package does not claim live XDJ hardware validation for the resulting
ARM binary (a device payload artifact).
