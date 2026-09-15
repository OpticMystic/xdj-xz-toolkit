param(
    [string]$Output = "build/libxz-directfb-hook-next-abi14.so"
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolvedOutput = [IO.Path]::GetFullPath((Join-Path $repo $Output))
if (-not $resolvedOutput.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output must remain inside $repo"
}
$relativeOutput = $resolvedOutput.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')

# The XZ glibc only exports up to GLIBC_2.4. A hook referencing a newer symbol
# version uploads and hashes fine, then makes rbp refuse to start and leaves the
# deck with no UI at all. Fail the build here rather than on the hardware.
docker build -q -t xdj-xz-arm-cross -f (Join-Path $PSScriptRoot 'Dockerfile.cross') $repo | Out-Null
docker run --rm -v "${repo}:/src" xdj-xz-arm-cross sh -lc @"
arm-linux-gnueabihf-gcc -shared -fPIC -O3 -std=gnu99 -Wall -Wextra   -I/src/build/dfb-generated   -I/src/build/directfb-1.4-src/include   -I/src/build/directfb-1.4-src/lib   -o /src/$relativeOutput   /src/tools/xz_runtime/xz_directfb_hook.c -ldl -pthread
arm-linux-gnueabihf-readelf -h /src/$relativeOutput | grep -E 'Class|Type|Machine'
arm-linux-gnueabihf-readelf -Ws /src/$relativeOutput | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 > /src/build/.hook-abi
cat /src/build/.hook-abi
"@
if ($LASTEXITCODE -ne 0) { throw "ARM cross-compile failed with exit code $LASTEXITCODE" }

# Read the ABI result from a file: capturing native stdout in Windows PowerShell
# turns the compiler's stderr into terminating NativeCommandError records.
$abiPath = Join-Path $repo 'build/.hook-abi'
$symbol = if (Test-Path -LiteralPath $abiPath) { (Get-Content -LiteralPath $abiPath -TotalCount 1).Trim() } else { '' }
$required = [version]'2.4'
if ($symbol -match '^GLIBC_([0-9.]+)$' -and ([version]$Matches[1]) -gt $required) {
    Remove-Item -LiteralPath $resolvedOutput -Force -ErrorAction SilentlyContinue
    throw "ABI gate failed: hook requires $symbol but the XZ glibc only exports up to GLIBC_$required"
}

Get-Item -LiteralPath $resolvedOutput
Get-FileHash -Algorithm MD5 -LiteralPath $resolvedOutput
