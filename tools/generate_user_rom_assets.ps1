[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)

function Invoke-Dotnet([string[]]$Arguments) {
    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Asset generator exited with code $LASTEXITCODE." }
}

Invoke-Dotnet @('run', '--project', "$PSScriptRoot/romconv", '--',
    '--rom', $romPath, '--ps1pack-only', '--out', $output)

# Reproduce the hardware-approved RB5 source sequence from the local DMC
# sample at CPU $FB00. These are timing and register settings, not samples.
$work = Join-Path $output 'bark-work'
New-Item -ItemType Directory -Path $work | Out-Null
$trace = Join-Path $work 'dmc-registers.csv'
$records = foreach ($frame in 470, 486, 502) {
    foreach ($register in @(@('4010','0E'), @('4012','EC'), @('4013','20'), @('4015','10'))) {
        [pscustomobject]@{frame=$frame; address=$register[0]; value=$register[1]}
    }
}
$records | Export-Csv -LiteralPath $trace -NoTypeInformation

$barkTools = Join-Path $PSScriptRoot 'mesen_wave'
& "$barkTools/decode_rom_dmc.ps1" -Rom $romPath -Apu $trace -OutputDir $work
& "$barkTools/compose_rom_bark.ps1" -DecodedDir $work -OutputDir $work
$spu = Join-Path $work 'bark.spu'
# BarkSpu and the Mesen recorder share a source directory. Separate build
# intermediates so a previous recorder build cannot be selected here.
$encoder = Join-Path $work 'encoder'
$obj = (Join-Path $work 'encoder-obj') + '/'
Invoke-Dotnet @('build', "$barkTools/BarkSpu.csproj", '-o', $encoder,
    "-p:BaseIntermediateOutputPath=$obj", "-p:MSBuildProjectExtensionsPath=$obj")
Invoke-Dotnet @("$encoder/BarkSpu.dll", "$work/dog_bark_rom_dmc_sequence.wav",
    $spu, "$work/spu.json")
$expected = '3E4F6F308F11A26B325A2B39EB535C9347EBD02DAF73F291B827987FE0F087EF'
if ((Get-FileHash -LiteralPath $spu -Algorithm SHA256).Hash -ne $expected) {
    throw 'Locally generated bark differs from the isolated diagnostic payload.'
}
& "$barkTools/prepare_rom_bark_ps1.ps1" -Spu $spu -Output "$output/rom_bark_spu.c"

