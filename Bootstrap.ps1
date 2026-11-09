param([ValidateSet('BIN/CUE','EXE')][string]$Format, [string]$OfflinePackages, [string]$ToolCache)
$ErrorActionPreference = 'Stop'
$logStarted = $false
try {
    $log = Join-Path $PSScriptRoot ('builder-' + (Get-Date -Format yyyyMMdd-HHmmss-fff) + '.log')
    Start-Transcript -Path $log | Out-Null
    $logStarted = $true
    $romDir = Join-Path $PSScriptRoot 'rom'
    [IO.Directory]::CreateDirectory($romDir) | Out-Null
    $roms = @(Get-ChildItem -LiteralPath $romDir -File -Filter '*.nes')
    if ($roms.Count -ne 1) { throw 'Put exactly one supported Duck Hunt .nes file in the rom folder.' }
    $rom = $roms[0].FullName
    $image = [IO.File]::ReadAllBytes($rom)
    if ($image.Length -lt 16 -or [BitConverter]::ToString($image,0,4) -ne '4E-45-53-1A' -or
        $image[4] -ne 1 -or $image[5] -ne 1 -or ($image[6] -band 240) -or ($image[7] -band 240) -or
        ((($image[7] -band 12) -eq 8) -and (($image[8] -band 15) -or $image[9]))) {
        throw 'Unsupported ROM header. Use the supported mapper-0 Duck Hunt cartridge dump.'
    }
    $start = 16
    if ($image[6] -band 4) { $start += 512 }
    if ($image.Length -lt $start + 24576) { throw 'The ROM is incomplete.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash($image,$start,24576)).Replace('-','') }
    finally { $sha.Dispose() }
    if ($digest -ne '7026334A7E8742B61B450F4B3B182922C6A69FC723D7CD19C83DB365F15E45BA') {
        throw 'This ROM does not match the supported Duck Hunt revision.'
    }
    Write-Host 'ROM verified.'
    if (!$Format) {
        switch (Read-Host 'Press Enter for BIN/CUE (recommended), or enter 2 for PS-X EXE') {
            '' { $Format = 'BIN/CUE' }
            '1' { $Format = 'BIN/CUE' }
            '2' { $Format = 'EXE' }
            default { throw 'Choose BIN/CUE or PS-X EXE.' }
        }
    }
    $lock = Join-Path $PSScriptRoot 'tools.lock.json'
    if (!(Test-Path -LiteralPath $lock -PathType Leaf)) {
        throw 'Portable tool setup is pending a verified tools.lock.json. No tool was downloaded.'
    }
    . "$PSScriptRoot/PortableTools.ps1"
    $manifest = Get-Content -LiteralPath $lock -Raw | ConvertFrom-Json
    $cache = if ($ToolCache) { $ToolCache } else { Join-Path $env:LOCALAPPDATA 'DuckHuntPS1\tools' }
    $roles = @('pwsh','dotnet','node','cmake','ffmpeg','sdk','mesen')
    foreach ($role in $roles) {
        if (@($manifest.packages | Where-Object role -eq $role).Count -ne 1) {
            throw "Tool lock must define exactly one $role package."
        }
    }
    $tools = @{}
    foreach ($role in $roles) {
        $package = $manifest.packages | Where-Object role -eq $role
        $tools[$role] = Get-PortableTool $package $cache $OfflinePackages
    }
    $core = Get-PinnedMesenCore $tools.mesen
    $sdk = Split-Path (Split-Path $tools.sdk -Parent) -Parent
    $dirs = foreach ($role in 'dotnet','node','cmake','ffmpeg') { Split-Path $tools[$role] -Parent }
    $env:Path = ($dirs -join ';') + ';' + "$env:SystemRoot\System32;$env:SystemRoot"
    & $tools.pwsh -NoProfile -File "$PSScriptRoot/Build-Game.ps1" -Format $Format -Rom $rom -Sdk $sdk -MesenCore $core -Ffmpeg $tools.ffmpeg
    if ($LASTEXITCODE) { throw 'The local build failed. Review the messages above.' }
} catch {
    Write-Host "Build stopped. $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($logStarted) { Stop-Transcript | Out-Null }
}



