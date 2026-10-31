# Runs the PC harness (built with the same translated code as the .xex) and
# writes x360\reference.h: the checkpoint hashes the console compares itself
# against. Hashes only, no game data.
#   powershell -File x360\pc\make_reference.ps1 -Root C:\path\testrun
param([Parameter(Mandatory = $true)][string]$Root, [int]$Frames = 1800)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'build\harness.exe'
$out = Join-Path (Split-Path -Parent $here) 'reference.h'
$work = Join-Path $env:TEMP 'uc360_reference'
New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
$log = & $exe $Root $Frames --pristine 2>&1 | ForEach-Object { "$_" }
Pop-Location

$rows = foreach ($l in $log) {
    if ($l -match '^checkpoint (\d+): ([0-9a-f]{16}) ([0-9a-f]{16}) ([0-9a-f]{16}) ([0-9a-f]{16})') {
        "    { $($matches[1])ull, 0x$($matches[2])ull, 0x$($matches[3])ull, 0x$($matches[4])ull, 0x$($matches[5])ull },"
    }
}
if (-not $rows) { $log | Select-Object -Last 20; throw 'no checkpoints in the harness output' }
$disk = ($log | Where-Object { $_ -match '^disk id' }) -join ''
$text = @(
    '// Written by x360\pc\make_reference.ps1 from a PC harness run: checkpoint',
    "// hashes { frame, RAM0, RAM2, CPU, DSP }. $disk",
    'static const unsigned long long reference[][5] = {'
) + $rows + @('};')
$text | Set-Content -Encoding ascii $out
Write-Host "$($rows.Count) checkpoints -> $out"
