[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CaptureDirectory,
    [Parameter(Mandatory)][string]$Output,
    [string]$Ffmpeg = 'ffmpeg'
)

$ErrorActionPreference = 'Stop'
$capture = (Resolve-Path -LiteralPath $CaptureDirectory).Path
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if (Test-Path -LiteralPath $outputPath) {
    throw 'Choose a new output file so the approved audio bank remains intact.'
}
$cues = @(
    @{Name='clay_go'; Latch='F1_01'; Duration='1.2'},
    @{Name='clay_launch'; Latch='F0_32'; Duration='1.2'},
    @{Name='clay_break'; Latch='F0_04'; Duration='0.56'}
)
$header = [Text.StringBuilder]::new("/* Locally captured from isolated NES APU requests. */`n")

foreach ($cue in $cues) {
    $wav = Join-Path $capture "sfxscan_$($cue.Latch)_session.wav"
    $pcm = Join-Path $capture "$($cue.Name).pcm"
    & $Ffmpeg -v error -ss 8.3 -i $wav -t $cue.Duration -ac 1 -ar 22050 -f s16le -y $pcm
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg failed for $($cue.Name)." }
    $bytes = [IO.File]::ReadAllBytes($pcm)
    $samples = [Collections.Generic.List[int]]::new()
    for ($i = 0; $i -lt $bytes.Length; $i += 2) {
        $samples.Add([Math]::Clamp(2 * [BitConverter]::ToInt16($bytes, $i), -32768, 32767))
    }
    $first = $samples.FindIndex([Predicate[int]]{ param($sample) [Math]::Abs($sample) -gt 12 })
    if ($first -lt 0) { throw "No audio found for $($cue.Name)." }
    $samples.RemoveRange(0, [Math]::Max(0, $first - 22))
    while ($samples.Count % 28) { $samples.Add(0) }

    $blocks = [Collections.Generic.List[byte]]::new()
    for ($start = 0; $start -lt $samples.Count; $start += 28) {
        for ($shift = 12; $shift -gt 0; $shift--) {
            $scale = 4096 -shr $shift
            $fits = $true
            for ($j = 0; $j -lt 28; $j++) {
                $q = [Math]::Round($samples[$start + $j] / $scale)
                if ($q -lt -8 -or $q -gt 7) { $fits = $false; break }
            }
            if ($fits) { break }
        }
        $scale = 4096 -shr $shift
        $blocks.Add([byte]$shift)
        $blocks.Add([byte]($start + 28 -eq $samples.Count))
        for ($j = 0; $j -lt 28; $j += 2) {
            $lo = [Math]::Clamp([int][Math]::Round($samples[$start + $j] / $scale), -8, 7) -band 15
            $hi = [Math]::Clamp([int][Math]::Round($samples[$start + $j + 1] / $scale), -8, 7) -band 15
            $blocks.Add([byte]($lo -bor ($hi -shl 4)))
        }
    }
    $length = $blocks.Count
    while ($blocks.Count % 64) { $blocks.Add(0) }
    [void]$header.Append("static const uint8_t $($cue.Name)[] __attribute__((aligned(64))) = {`n")
    for ($i = 0; $i -lt $blocks.Count; $i += 16) {
        [void]$header.Append(($blocks.GetRange($i, 16) -join ',') + ",`n")
    }
    [void]$header.Append("};`n#define $($cue.Name.ToUpperInvariant())_BYTES $length`n")
}
[IO.File]::WriteAllText($outputPath, $header.ToString())
