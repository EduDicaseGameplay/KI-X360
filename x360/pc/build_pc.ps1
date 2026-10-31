# Builds the PC test harness (x360\pc\build\harness.exe) with VS2010 for x86:
# the Xbox 360 port's emulator core and start-up, headless.
#   powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1 [-Rec DIR]
# -Rec links the translated game code in DIR (rec_*.c, rec_tab.c, dsp_*.c)
# instead of rec_none.c.
param([string]$Rec)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = (Resolve-Path (Join-Path $here '..\..\src')).Path
$out = Join-Path $here 'build'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null

# VS2010's x86 tools, in this process
$vcvars = (Resolve-Path "$env:VS100COMNTOOLS..\..\VC\vcvarsall.bat").Path
cmd /c "`"$vcvars`" x86 >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] } }
$env:INCLUDE = "$here\shim;$env:INCLUDE"

$core = 'cpu','mem','ide','dcs','adsp','adsprec','rec','game','game_ki1','game_ki2',
        'state','disasm','machine','launch','osd','recomp'
$common = @('/nologo','/c','/O2','/W3','/wd4800','/wd4996','/MT','/Zi','/DNDEBUG','/DUC_PROFILE')

function Compile([string]$file, [string[]]$flags, [string]$name) {
    if (-not $name) { $name = [IO.Path]::GetFileNameWithoutExtension($file) }
    $o = & cl @common @flags /Fo"$obj\$name.obj" /Fd"$obj\vc.pdb" $file 2>&1 | Out-String
    $bad = $o -split "`n" | Where-Object { $_ -match ': (fatal )?error |: warning ' }
    if ($bad) { $bad | Select-Object -First 30 | ForEach-Object { Write-Host $_.Trim() } }
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $file" }
    "$obj\$name.obj"
}

$cxx = @('/TP','/EHsc','/FIx360_compat.h',"/I$src")
$objs = @()
foreach ($c in $core) { $objs += Compile "$src\$c.c" $cxx }
$objs += Compile "$here\..\boot.cpp" $cxx
$objs += Compile "$here\harness.cpp" $cxx
$objs += Compile "$src\third_party\libchdr_all.c" @('/TC','/wd4244',"/I$src\third_party\libchdr\include")
if ($Rec) {
    Get-ChildItem $Rec -Filter *.c | ForEach-Object { $objs += Compile $_.FullName ($cxx + @('/bigobj','/wd4102','/wd4244')) ("rec_" + $_.BaseName) }
} else {
    $objs += Compile "$src\rec_none.c" $cxx
}
& link /nologo /DEBUG /OUT:"$out\harness.exe" @objs 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Get-Item "$out\harness.exe" | Select-Object FullName, Length
