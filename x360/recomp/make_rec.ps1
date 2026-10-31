# Makes the game's translated code (rec_*.c, the sound DSP's rec_dsp*.c and
# rec_tab.c) from YOUR game files with the PC harness: a RAM snapshot after
# 900 frames, then the MIPS->C translation with the options the released
# builds use. The output is the game's code in another form: never share it.
#   powershell -ExecutionPolicy Bypass -File x360\recomp\make_rec.ps1 -Root C:\path\game -Out C:\path\rec
# Root holds kinst.zip and kinst\kinst.chd; Out is created if needed.
param([Parameter(Mandatory = $true)][string]$Root, [Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here '..\pc\build\harness.exe'
if (-not (Test-Path $exe)) { throw 'build the PC harness first: x360\pc\build_pc.ps1' }
$exe = (Resolve-Path $exe).Path
$Root = (Resolve-Path $Root).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$work = Join-Path $env:TEMP 'kix360_snapshot'
New-Item -ItemType Directory -Force $work | Out-Null
Remove-Item "$work\ram0.bin", "$work\ram2.bin" -ErrorAction SilentlyContinue   # this script's own, from a run before

Push-Location $work
# the harness reports on stderr, which Windows PowerShell would take as errors
$ErrorActionPreference = 'Continue'
try {
    # --pristine: the disk's own settings, so your saved settings don't matter
    & $exe $Root 900 --pristine --dump-at 900 2>&1 | ForEach-Object { "$_" } | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path "$work\ram2.bin")) { throw 'the harness did not run the game: check the files in Root' }
    # Functions that talk to the hardware or are the disk driver stay unoptimised
    # (lista-protegida.txt); the rest gets fast memory access, small callees
    # copied in and floating point translated.
    $env:KI2_RECOMP_PLAIN = Join-Path $here 'lista-protegida.txt'
    $env:KI2_RECOMP_FAST = '1'
    $env:KI2_RECOMP_INLINE = '1'
    $env:KI2_RECOMP_FPU = '1'
    & $exe $Root 0 --recomp "$work\ram0.bin" "$work\ram2.bin" $Out 2>&1 | ForEach-Object { "$_" } | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { throw 'translation failed' }
} finally {
    Pop-Location
}
Write-Host "translated code in $Out"
