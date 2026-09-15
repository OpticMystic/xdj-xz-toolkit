# Device build artifacts — rebuilt locally, never shipped

The following are local-only outputs and are not part of this repository:

- `imagedata-vjtools-loader.dat`
- `libxz-directfb-hook-*-abi14.so` (ARM hook build output)
- `xz-gui-ip-patch` (GUI/IP patch build output)

Rebuild them from the checked-in source:

```powershell
npm test
powershell -ExecutionPolicy Bypass -File vendor/tools/xz_runtime/build_hook.ps1 -Output build/libxz-directfb-hook-repo-built-abi14.so
powershell -ExecutionPolicy Bypass -File vendor/tools/xz_runtime/build_gui_ip_patch.ps1 -Output build/xz-gui-ip-patch-repo-built
```

The ARM hook build uses Docker (`vendor/tools/xz_runtime/Dockerfile.cross`)
and fails closed if the hook would require a glibc symbol newer than the
deck's GLIBC_2.4 exports.
