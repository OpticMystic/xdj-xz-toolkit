param([string]$Output = "build/xz-gui-ip-patch")
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolved = [IO.Path]::GetFullPath((Join-Path $repo $Output))
if (-not $resolved.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must remain inside repository' }
$relative = $resolved.Substring($repo.Length).TrimStart('\','/').Replace('\','/')
docker build -q -t xdj-xz-gui-ip-patch -f (Join-Path $PSScriptRoot 'Dockerfile.gui-ip-patch') $repo | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'GUI IP patcher toolchain build failed' }
docker run --rm -v "${repo}:/src" xdj-xz-gui-ip-patch sh -lc "set -eu; /opt/musl/bin/musl-gcc -static -O2 -std=gnu99 -Wall -Wextra -o '/src/$relative' /src/tools/xz_runtime/xz_gui_ip_patch.c; arm-linux-gnueabihf-strip '/src/$relative'; arm-linux-gnueabihf-readelf -h '/src/$relative' | grep -E 'Class|Type|Machine'; if arm-linux-gnueabihf-readelf -l '/src/$relative' | grep -E 'INTERP|DYNAMIC'; then exit 1; fi"
if ($LASTEXITCODE -ne 0) { throw 'Static ARM GUI IP patcher build failed' }
Get-Item -LiteralPath $resolved
Get-FileHash -Algorithm MD5 -LiteralPath $resolved
