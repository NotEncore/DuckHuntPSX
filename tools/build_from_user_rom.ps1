# SPDX-License-Identifier: GPL-3.0-or-later
# See LICENSE-MESEN-WORKER.md.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$MesenCore,
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Ffmpeg = 'ffmpeg',
    [string]$Cmake = 'cmake',
    [string]$Ninja
)

$ErrorActionPreference = 'Stop'
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$core = (Resolve-Path -LiteralPath $MesenCore).Path
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Choose a new private build directory.' }

function Invoke-Dotnet([string[]]$Arguments) {
    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ROM build tool failed with exit code $LASTEXITCODE." }
}

Invoke-Dotnet @('run', '--project', "$PSScriptRoot/romconv", '--', '--rom', $romPath, '--verify-only')
Get-Command node -ErrorAction Stop | Out-Null
$scratch = Join-Path $out 'audio-work'
$captures = Join-Path $scratch 'captures'
$audio = Join-Path $scratch 'encoded'
New-Item -ItemType Directory -Path $scratch,$captures,$audio -Force | Out-Null
$captureHost = Join-Path $scratch 'capture-host'
$encoder = Join-Path $scratch 'encoder'
Invoke-Dotnet @('build', "$PSScriptRoot/mesen_wave/MesenWave.csproj", '-o', $captureHost)
Invoke-Dotnet @('build', "$PSScriptRoot/rom_audio/RomAudio.csproj", '-o', $encoder)

$profiles = @(
    @('title'), @('round'), @('trace-perfect'), @('trace-mixed'), @('trace-fail'), @('trace-gapped'),
    @('sfxscan','F1','1'), @('sfxscan','F0','32'), @('sfxscan','F0','4')
)
$workers = [Collections.Generic.List[object]]::new()
try {
    foreach ($profile in $profiles) {
        $start = [Diagnostics.ProcessStartInfo]::new((Get-Command dotnet).Source)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($arg in (@("$captureHost/mesen_wave.dll") + $profile +
                @("--rom=$romPath", "--core=$core", "--capture-dir=$captures"))) {
            $start.ArgumentList.Add($arg)
        }
        $process = [Diagnostics.Process]::Start($start)
        $workers.Add(@{
            Name=($profile -join '-'); Process=$process
            Stdout=$process.StandardOutput.ReadToEndAsync()
            Stderr=$process.StandardError.ReadToEndAsync()
        })
    }
    foreach ($worker in $workers) {
        if (!$worker.Process.WaitForExit(240000)) { throw "Timed out capturing $($worker.Name)." }
        $log = $worker.Stdout.Result + $worker.Stderr.Result
        Set-Content -LiteralPath (Join-Path $scratch "$($worker.Name).log") -Value $log
        if ($worker.Process.ExitCode -ne 0) {
            throw "Capture failed for $($worker.Name). See its log in $scratch."
        }
        Write-Host "Captured $($worker.Name)"
    }
}
finally {
    foreach ($worker in $workers) {
        if (!$worker.Process.HasExited) { $worker.Process.Kill() }
        $worker.Process.Dispose()
    }
}

# Pin the timing inputs for the measured cue windows. These are hashes of
# local emulator event logs, not recordings or bundled audio.
$expectedTraces = @{
    perfect='7595BF68C2918878702D1021FCA5C75A020B55C82BF68B198F0E193E58A99BD2'
    mixed='7E93CFD93894038CCEA7F535EEEBBB1136C89581A0116E2A8180637AD82D44E3'
    fail='2AACEBDEDCFAFFF5697F9768DC0BE459B960091FC9C0BFB0367550C22CAECBE3'
    gapped='27EB1D9AB147F1658BD07780F283B8AF0904725DEC435D31579CB0F8B2C3B552'
}
foreach ($profile in $expectedTraces.Keys) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $captures "$profile.trace")).Hash
    if ($hash -ne $expectedTraces[$profile]) {
        throw "Mesen timing differed for $profile. Build stopped before encoding that trace."
    }
}
$windows = Join-Path $scratch 'windows.csv'
@('flap,-1','fall,0,45','land,-10','retrieve,-10','laugh,-6','clear,102',
    'bonus,-110','over,32','tally,26') | Set-Content -LiteralPath $windows
$intro = Join-Path $scratch 'intro.wav'
& $Ffmpeg -v error -ss 4.344 -i "$captures/round_session.wav" -t 5.495 -ac 1 -ar 48000 -c:a pcm_s16le $intro
if ($LASTEXITCODE -ne 0) { throw 'FFmpeg could not cut the local intro recording.' }
$renderer = "$encoder/RomAudio.dll"
Invoke-Dotnet @($renderer, 'title', "$captures/title_session.wav", $audio)
Invoke-Dotnet @($renderer, 'intro', $intro, $audio)
Invoke-Dotnet @($renderer, 'game-a', $romPath, $captures, $audio, $windows)
& "$PSScriptRoot/mesen_wave/encode_clay_audio.ps1" -CaptureDirectory $captures -Output "$audio/clay_sfx.h" -Ffmpeg $Ffmpeg

$candidate = @{
    Rom=$romPath; GeneratedAudioDirectory=$audio; OutputDirectory="$out/candidate"
    SdkDirectory=$SdkDirectory; Cmake=$Cmake; Ninja=$Ninja
}
& "$PSScriptRoot/build_user_rom_candidate.ps1" @candidate
