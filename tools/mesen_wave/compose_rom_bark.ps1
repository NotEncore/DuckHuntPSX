param(
 [Parameter(Mandatory=$true)][string]$DecodedDir,
 [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference='Stop'
# Compose three independently triggered DMC segments at their measured NES frame positions.
# This is an isolated ROM-derived DMC-only reconstruction, not the complete NES APU mix.
$inputPath=Join-Path $DecodedDir 'dmc_FB00_rate14.wav'
$b=[IO.File]::ReadAllBytes($inputPath)
if($b.Length -lt 46 -or [Text.Encoding]::ASCII.GetString($b,0,4) -ne 'RIFF'){throw 'Missing decoded DMC WAV'}
$hz=[BitConverter]::ToInt32($b,24)
$sourceCount=[int](($b.Length-44)/2)
$source=New-Object 'int16[]' $sourceCount
[Buffer]::BlockCopy($b,44,$source,0,$sourceCount*2)
$frames=@(470,486,502)
$ntsc=60.0988
$outHz=44100
$duration=[int][Math]::Ceiling((($frames[-1]-$frames[0])/$ntsc+($sourceCount/$hz)+0.10)*$outHz)
$samples=New-Object 'int16[]' $duration
$positions=@()
foreach($frame in $frames){
 $start=[int][Math]::Round((($frame-$frames[0])/$ntsc)*$outHz)
 $positions+=$start
 # Output-driven linear interpolation fills every destination sample.
 # The old input-driven loop left zero-valued holes at 44.1kHz, adding
 # artificial high-frequency buzzing and invalidating waveform comparisons.
 $outCount=[int][Math]::Ceiling($sourceCount*$outHz/$hz)
 for($j=0;$j -lt $outCount;$j++){
  $at=$start+$j
  if($at -ge $duration){break}
  $srcPos=$j*$hz/$outHz
  $lo=[int][Math]::Floor($srcPos)
  $hi=[Math]::Min($lo+1,$sourceCount-1)
  if($lo -ge $sourceCount){break}
  $fraction=$srcPos-$lo
  $v=$source[$lo]*(1-$fraction)+$source[$hi]*$fraction
  $samples[$at]=[int16][Math]::Round($v)
 }
}
New-Item -ItemType Directory -Force -Path $OutputDir|Out-Null
$path=Join-Path $OutputDir 'dog_bark_rom_dmc_sequence.wav'
$fs=[IO.File]::Create($path)
try{
 $w=New-Object IO.BinaryWriter($fs)
 $n=$samples.Length*2
 $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$w.Write([int](36+$n))
 $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$w.Write([int]16)
 $w.Write([int16]1);$w.Write([int16]1);$w.Write([int]$outHz);$w.Write([int]($outHz*2));$w.Write([int16]2);$w.Write([int16]16)
 $w.Write([Text.Encoding]::ASCII.GetBytes('data'));$w.Write([int]$n)
 $data=New-Object 'byte[]' $n
 [Buffer]::BlockCopy($samples,0,$data,0,$n)
 $w.Write($data);$w.Flush()
}finally{$fs.Dispose()}
$summary=[pscustomobject]@{source='original user-supplied NES ROM via DMC decoder';sample='0xFB00';frames=$frames;spacing_frames=16;spacing_seconds=[Math]::Round(16/$ntsc,6);sample_rate=$outHz;duration_seconds=[Math]::Round($duration/$outHz,4);output=$path;note='DMC-only, linear resampling; approximate DAC initial level and no APU analog filtering; must be listened to and compared against Mesen reference'}
$summary|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 (Join-Path $OutputDir 'dog_bark_sequence_manifest.json')
$summary|ConvertTo-Json -Depth 4
