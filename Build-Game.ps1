[CmdletBinding()]
param(
    [ValidateSet('EXE','BIN/CUE')][string]$Format,
    [string]$Rom,
    [string]$Sdk = $env:DUCKHUNT_SDK,
    [string]$MesenCore = $env:DUCKHUNT_MESEN_CORE,
    [string]$Ffmpeg = 'ffmpeg'
)
$ErrorActionPreference = 'Stop'
try {
    if (!$Rom) {
        $roms = @(Get-ChildItem -LiteralPath "$PSScriptRoot/rom" -File -Filter '*.nes')
        if ($roms.Count -ne 1) { throw 'Put exactly one supported Duck Hunt .nes file in the rom folder.' }
        $Rom = $roms[0].FullName
    }
    $Rom = (Resolve-Path -LiteralPath $Rom).Path
    foreach ($tool in 'dotnet','node','cmake',$Ffmpeg) {
        if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Build tool unavailable: $tool." }
    }
    if (!$Sdk -or !(Test-Path -LiteralPath "$Sdk/bin/mipsel-none-elf-gcc.exe")) { throw 'Set DUCKHUNT_SDK to the installed PSn00bSDK folder.' }
    if (!$MesenCore -or !(Test-Path -LiteralPath $MesenCore -PathType Leaf)) { throw 'Set DUCKHUNT_MESEN_CORE to the supported MesenCore.dll.' }
    if (!$Format) {
        switch (Read-Host 'Choose 1 for BIN/CUE disc, or 2 for PS-X EXE') {
            '1' { $Format = 'BIN/CUE' }
            '2' { $Format = 'EXE' }
            default { throw 'Choose 1 or 2.' }
        }
    }
    if ($Format -eq 'BIN/CUE' -and !(Test-Path -LiteralPath "$Sdk/bin/mkpsxiso.exe")) {
        throw 'The installed SDK does not contain mkpsxiso.exe.'
    }
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $work = "$PSScriptRoot/private-build/$id"
    Write-Host 'Verifying your ROM, generating game data locally, and compiling.'
    & "$PSScriptRoot/tools/build_from_user_rom.ps1" -Rom $Rom -MesenCore $MesenCore -SdkDirectory $Sdk -Ffmpeg $Ffmpeg -OutputDirectory $work
    $exe = "$work/candidate/build/duckhunt.exe"
    $sha = (Get-FileHash -LiteralPath $exe).Hash
    if ($sha -ne '5BB5BA113A247FAAB6F798342D359FA63EF52502A14507CE2E841B3D2FCCC3A6') {
        throw "Build does not match the verified executable. Keep the private build for investigation. SHA-256: $sha"
    }
    $out = "$PSScriptRoot/output/$id"
    New-Item -ItemType Directory -Path $out | Out-Null
    if ($Format -eq 'EXE') {
        Copy-Item -LiteralPath $exe -Destination "$out/DuckHuntPS1.exe"
    } else {
        Write-Host 'Creating a local BIN/CUE disc image.'
        Push-Location $out
        try {
            Copy-Item -LiteralPath $exe -Destination "$work/HUNTPS1.EXE"
            @('BOOT=cdrom:\HUNTPS1.EXE;1','TCB=4','EVENT=10','STACK=801FFFF0') |
                Set-Content -LiteralPath "$work/SYSTEM.CNF" -Encoding ascii
            $boot = [Security.SecurityElement]::Escape("$work/SYSTEM.CNF")
            $game = [Security.SecurityElement]::Escape("$work/HUNTPS1.EXE")
            @"
<iso_project><track type="data">
<identifiers system="PLAYSTATION" volume="DUCKHUNTPS1" />
<directory_tree><file name="SYSTEM.CNF" type="data" source="$boot" />
<file name="HUNTPS1.EXE" type="data" source="$game" />
<dummy sectors="1024" /></directory_tree></track></iso_project>
"@ | Set-Content -LiteralPath "$work/disc.xml" -Encoding utf8
            & "$Sdk/bin/mkpsxiso.exe" -o DuckHuntPS1.bin -c DuckHuntPS1.cue "$work/disc.xml"
            if ($LASTEXITCODE -ne 0 -or !(Test-Path DuckHuntPS1.cue) -or !(Test-Path DuckHuntPS1.bin)) {
                throw 'Disc image creation failed. See the tool output above.'
            }
        } finally { Pop-Location }
    }
    Write-Host "Finished. Your local game is in $out"
} catch {
    Write-Host "Build stopped. $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

