[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$GeneratedAudioDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$SdkDirectory,
    [string]$Cmake = 'cmake',
    [string]$Ninja
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$audio = (Resolve-Path -LiteralPath $GeneratedAudioDirectory).Path
$sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Choose a new private build directory.' }
if (!$Ninja) { $Ninja = Join-Path $sdk 'bin/ninja.exe' }

$requiredAudio = @(
    'hunt_sfx_m2_title.c', 'hunt_sfx_m2_title.h', 'hunt_sfx_m3_intro.c', 'hunt_sfx_m3_intro.h',
    'game_a_sfx.h', 'game_a_tally.h', 'clay_sfx.h'
)
foreach ($name in $requiredAudio) {
    if (!(Test-Path -LiteralPath (Join-Path $audio $name) -PathType Leaf)) {
        throw "Missing locally generated audio: $name. No fallback is used."
    }
}

$source = Join-Path $out 'source'
$hunt = Join-Path $source 'huntps1'
$gun = Join-Path $source 'guncal'
New-Item -ItemType Directory -Path $hunt,$gun -Force | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/rom_release/CMakeLists.txt" -Destination $source
$huntSources = @(
    'main.c', 'cal_save.c', 'cal_save.h', 'cal_save_codec.h', 'gfx.c', 'gfx.h',
    'logic.c', 'logic.h', 'nes_fly.c', 'nes_fly.h', 'nes_fly_rom.h',
    'audio.c', 'audio.h', 'game_b.c', 'game_b.h', 'clay_motion.c', 'clay_motion.h',
    'clay_flight.c', 'clay_flight.h', 'clay_round.c', 'clay_round.h',
    'clay_render.c', 'clay_render.h', 'deluxe_ducks.c', 'deluxe_ducks.h',
    'woods3d.c', 'woods3d_render.c', 'woods3d_render.h'

)
foreach ($name in $huntSources) { Copy-Item -LiteralPath "$root/huntps1/$name" -Destination $hunt }
Copy-Item -LiteralPath "$root/guncal/guncal.c","$root/guncal/guncal.h" -Destination $gun

& node "$PSScriptRoot/game_f/preview-meshes.cjs" --export-ps1 "$hunt/woods3d_approved_assets.h"
if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath "$hunt/woods3d_approved_assets.h").Hash -ne '6949DAA6F9A6B1A10537468A15EC90F58A934A434A144C12A4307C4444006AB7') {
    throw 'Game F procedural geometry differs from the approved asset header.'
}
& node "$PSScriptRoot/game_f/footsteps.cjs" "$hunt/woods_footsteps.h"
if ($LASTEXITCODE -ne 0) { throw 'Game F footstep generation failed.' }

$generated = Join-Path $hunt 'generated'
& "$PSScriptRoot/generate_user_rom_assets.ps1" -Rom $romPath -OutputDirectory $generated
foreach ($name in $requiredAudio) {
    Copy-Item -LiteralPath (Join-Path $audio $name) -Destination $generated
}

$build = Join-Path $out 'build'
$configure = @('-S', $source, '-B', $build, '-G', 'Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$sdk/lib/libpsn00b/cmake/sdk.cmake",
    "-DPSN00BSDK_TC=$sdk", "-DCMAKE_MAKE_PROGRAM=$Ninja")
& $Cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'PS1 CMake configure failed.' }
& $Cmake --build $build --target duckhunt
if ($LASTEXITCODE -ne 0) { throw 'PS1 candidate build failed.' }
$exe = Join-Path $build 'duckhunt.exe'
$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 2048 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'PS-X EXE' -or
    [BitConverter]::ToUInt32($bytes, 28) -ne $bytes.Length - 2048) {
    throw 'Candidate failed the PS-X EXE header check.'
}
Get-FileHash -LiteralPath $exe -Algorithm SHA256
