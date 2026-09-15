param([string]$Output = "build/xz-gui-ip-patch")
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolved = [IO.Path]::GetFullPath((Join-Path $repo $Output))
if (-not $resolved.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must remain inside repository' }
$relative = $resolved.Substring($repo.Length).TrimStart('\','/').Replace('\','/')
docker build -q -t xdj-xz-arm-cross -f (Join-Path $PSScriptRoot 'Dockerfile.cross') $repo | Out-Null
docker run --rm -v "${repo}:/src" xdj-xz-arm-cross sh -lc "arm-linux-gnueabihf-gcc -O2 -std=gnu99 -Wall -Wextra -o /src/$relative /src/tools/xz_runtime/xz_gui_ip_patch.c && arm-linux-gnueabihf-readelf -h /src/$relative | grep -E 'Class|Type|Machine'"
Get-Item -LiteralPath $resolved
Get-FileHash -Algorithm MD5 -LiteralPath $resolved
