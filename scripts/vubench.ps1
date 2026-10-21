# Times the VU1 code generator: regenerates a game's recorded VU1 programs into build/vubench/gen,
# builds recomp/vubench.cpp with them natively and with Emscripten, and replays a capture made with
# PS2_VU_CAPTURE=<file>:<skip>:<count>. The checksum must not change when the generator changes.
# usage: scripts/vubench.ps1 <capture file> [game] [passes]
param([Parameter(Mandatory)][string]$Capture, [string]$Game = 'dmc', [int]$Passes = 10)
$ErrorActionPreference = 'Stop'
$here = (Resolve-Path "$PSScriptRoot/..").Path
$out = "$here/build/vubench"
$gen = "$out/gen"
New-Item -ItemType Directory -Force $gen | Out-Null
ninja -C "$here/build/tools" vurecomp | Out-Null
& "$here/build/tools/vurecomp.exe" "$here/games/$Game/vu1" $gen
$sources = @("$here/recomp/vubench.cpp") + (Get-ChildItem "$gen/vu1_*.cpp" | ForEach-Object FullName)
$flags = @('-O3', '-std=gnu++20', '-Wno-unused-label', '-Wno-unused-variable', "-I$here/runtime/include")
clang++ @flags @sources -o "$out/vubench.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Output "native:"
& "$out/vubench.exe" $Capture $Passes
$env:EMSDK_QUIET = '1'
. "$here/tools/emsdk/emsdk_env.ps1" | Out-Null
em++ @flags -msimd128 @sources -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=512MB -sENVIRONMENT=node -o "$out/vubench.js"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Output "wasm:"
node "$out/vubench.js" $Capture $Passes
