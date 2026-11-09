param(
 [Parameter(Mandatory=$true)][string]$Rom,
 [Parameter(Mandatory=$true)][string]$Apu,
 [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference='Stop'
# NTSC NES DMC timer periods, CPU clock 1789773 Hz.
$periods=@(428,380,340,320,286,254,226,214,190,160,142,128,106,85,72,54)
$clock=1789773.0
$bytes=[IO.File]::ReadAllBytes($Rom)
if($bytes.Length -lt 16400 -or $bytes[0] -ne 78 -or $bytes[1] -ne 69 -or $bytes[2] -ne 83 -or $bytes[3] -ne 26){throw 'Not an iNES ROM'}
$mapper=(($bytes[6] -shr 4) -bor ($bytes[7] -band 0xF0))
if($mapper -ne 0 -or $bytes[4] -ne 1){throw 'Expected 16KB NROM mapper 0'}
$start=16
if(($bytes[6] -band 4) -ne 0){$start+=512}
$rows=@(Import-Csv $Apu)
$events=@()
for($i=0;$i -lt $rows.Count;$i++){
 $r=$rows[$i]
 if($r.address -ne '4015' -or (([Convert]::ToInt32($r.value,16) -band 16) -eq 0)){continue}
 $regs=@{}
 for($j=[Math]::Max(0,$i-8);$j -lt $i;$j++){
  if($rows[$j].frame -eq $r.frame){$regs[$rows[$j].address]=[Convert]::ToInt32($rows[$j].value,16)}
 }
 if(-not ($regs.ContainsKey('4012') -and $regs.ContainsKey('4013') -and $regs.ContainsKey('4010'))){continue}
 $events+= [pscustomobject]@{frame=[int]$r.frame;addr=$regs['4012'];len=$regs['4013'];rate=($regs['4010'] -band 15)}
}
New-Item -ItemType Directory -Force -Path $OutputDir|Out-Null
$manifest=@()
foreach($event in $events|Group-Object addr,len,rate){
 $e=$event.Group[0]
 $cpu=0xC000+64*$e.addr
 $offset=$start+(($cpu-0x8000)%0x4000)
 $length=16*$e.len+1
 if($offset+$length -gt $start+16384){throw 'DMC data outside PRG'}
 $rate=$clock/$periods[$e.rate]
 $pcm=New-Object 'System.Collections.Generic.List[byte]'
 # This is a standalone DMC bitstream approximation, not the full APU mixer.
 # DMC DAC is seven-bit; each LSB-first bit changes output by +/-2.
 $dac=64
 for($i=0;$i -lt $length;$i++){
  $b=[int]$bytes[$offset+$i]
  for($bit=0;$bit -lt 8;$bit++){
   if((($b -shr $bit) -band 1) -ne 0){if($dac -le 125){$dac+=2}}
   else {if($dac -ge 2){$dac-=2}}
   $s=[int16](($dac-64)*480)
   $pcm.Add([byte]($s -band 255))
   $pcm.Add([byte](($s -shr 8) -band 255))
  }
 }
 $name=('dmc_{0:X4}_rate{1}.wav' -f $cpu,$e.rate)
 $path=Join-Path $OutputDir $name
 $fs=[IO.File]::Create($path)
 try{
  $w=New-Object IO.BinaryWriter($fs)
  $n=$pcm.Count
  $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$w.Write([int](36+$n))
  $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$w.Write([int]16)
  $w.Write([int16]1);$w.Write([int16]1)
  $hz=[int][Math]::Round($rate)
  $w.Write($hz);$w.Write([int]($hz*2));$w.Write([int16]2);$w.Write([int16]16)
  $w.Write([Text.Encoding]::ASCII.GetBytes('data'));$w.Write([int]$n)
  $w.Write($pcm.ToArray());$w.Flush()
 }finally{$fs.Dispose()}
 $manifest+=[pscustomobject]@{cpu_address=('0x{0:X4}' -f $cpu);rom_offset=('0x{0:X4}' -f $offset);bytes=$length;dmc_rate_hz=[Math]::Round($rate,2);duration_seconds=[Math]::Round(($length*8)/$rate,4);trigger_frames=@($event.Group|ForEach-Object {$_.frame});output=$name;note='Standalone DMC approximation; other channels and DAC initialization not included'}
}
$manifest|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $OutputDir 'rom_dmc_decode_manifest.json')
$manifest|ConvertTo-Json -Depth 5
if($manifest.Count -eq 0){throw 'No DMC samples decoded'}
