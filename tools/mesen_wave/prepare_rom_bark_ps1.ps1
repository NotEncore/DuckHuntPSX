param([Parameter(Mandatory=$true)][string]$Spu,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$b=[IO.File]::ReadAllBytes($Spu)
if($b.Length -ne 10080){throw "Unexpected ROM bark SPU size $($b.Length)"}
if($b.Length%16 -ne 0){throw 'SPU block alignment error'}
for($i=0;$i -lt $b.Length;$i+=16){
 if(($b[$i] -shr 4) -gt 4 -or ($b[$i] -band 15) -gt 12){throw "Invalid SPU header at $i"}
}
$sb=New-Object Text.StringBuilder
[void]$sb.AppendLine('/* Generated locally from user-supplied DuckHunt.nes. Do not commit. */')
[void]$sb.AppendLine('#include <stdint.h>')
[void]$sb.AppendLine('const uint8_t rom_bark_spu[] __attribute__((aligned(64))) = {')
for($i=0;$i -lt $b.Length;$i++){
 if($i%16 -eq 0){[void]$sb.Append('  ')}
 [void]$sb.Append(('0x{0:X2},' -f $b[$i]))
 if($i%16 -eq 15){[void]$sb.AppendLine()}
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine(('const unsigned int rom_bark_spu_size = {0}u;' -f $b.Length))
New-Item -ItemType Directory -Force -Path (Split-Path $Output -Parent)|Out-Null
[IO.File]::WriteAllText($Output,$sb.ToString(),(New-Object Text.UTF8Encoding($false)))
Write-Host ('Generated private C audio array: '+$Output)

