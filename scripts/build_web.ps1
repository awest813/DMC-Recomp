# Windows counterpart of build_web.sh: builds the web version of a recompiled game into build/web-<game>.
# usage: scripts/build_web.ps1 <game folder under games/> [emsdk dir]
param([string]$Game = 'gt4', [string]$Emsdk = '')
$ErrorActionPreference = 'Stop'
$here = (Resolve-Path "$PSScriptRoot/..").Path -replace '\\', '/'
if (-not $Emsdk) { $Emsdk = if ($env:EMSDK) { $env:EMSDK } else { "$here/tools/emsdk" } }
$out = "$here/build/web-$Game"
$boot = (Select-String -Path "$here/games/$Game/disc/SYSTEM.CNF" -Pattern '^BOOT2\s*=\s*(.*)$').Matches[0].Groups[1].Value.Trim()
$elf = Get-ChildItem "$here/games/$Game/patched/*" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $elf) { $elf = "$here/games/$Game/core.elf" }
if (-not (Test-Path $elf)) {
    $name = ($boot -split '\\')[-1] -replace ';.*$', ''
    $elf = "$here/games/$Game/disc/$name"
}
$elf = $elf -replace '\\', '/'
New-Item -ItemType Directory -Force $out | Out-Null
[System.IO.File]::WriteAllText("$out/boot.txt", "$boot`n")
. "$Emsdk/emsdk_env.ps1" | Out-Null
$patches = if (Test-Path "$here/games/$Game/patches.txt") { "$here/games/$Game/patches.txt" } else { '' }
emcmake cmake -S $here -B $out -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DPS2_GENERATED=$here/generated/$Game" "-DPS2_WEB_ELF=$elf" "-DPS2_WEB_BOOT_FILE=$out/boot.txt" "-DPS2_WEB_PATCHES=$patches"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ninja -C $out ps2web
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Output "built $out/ps2web.html; serve it with: python scripts/serve.py build/web-$Game <disc.iso>"
