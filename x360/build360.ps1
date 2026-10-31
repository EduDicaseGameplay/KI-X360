# Builds KI-X360 (this UltraCombo port) for the Xbox 360 (build\kix360.xex) with the Xbox 360
# SDK's command-line tools (VS2010 compiler for PowerPC). Run from anywhere:
#   powershell -ExecutionPolicy Bypass -File x360\build360.ps1 [-Debug] [-Rec DIR]
# -Rec links the game's translated code from DIR (made by the PC harness from
# your own files: see README.md) for full speed; without it the game runs on
# the interpreter. A build with -Rec contains game code: never share it.
param([switch]$Debug, [string]$Rec, [switch]$Diag, [string]$Main = 'main360.cpp', [switch]$NoDcsThread, [switch]$LogExc)   # -Main: another frontend file (test variants); -NoDcsThread: sound DSP on the machine thread (default: its own core)
$ErrorActionPreference = 'Stop'
$X = $env:XEDK
if (-not $X) { throw 'XEDK is not set: install the Xbox 360 SDK' }
$env:PATH = "$X\bin\win32;$env:PATH"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = (Resolve-Path (Join-Path $here '..\src')).Path
$out = Join-Path $here 'build'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null
$env:INCLUDE = "$here\shim;$X\include\xbox"
$env:LIB = "$X\lib\xbox"

# The shared emulator (C11 sources compiled as C++ with x360_compat.h), the
# console frontend, and libchdr (plain C).
$core = 'cpu','mem','ide','dcs','adsp','adsprec','rec','game','game_ki1','game_ki2',
        'state','disasm','machine','launch','osd'
$opt = if ($Debug) { '/Od','/Zi','/D_DEBUG' } else { '/Ox','/Zi','/DNDEBUG' }
# C4800 (int to bool "performance" note) fires on idiomatic C; C4996 is MSVC's secure-CRT nag.
$common = @('/nologo','/c','/W3','/wd4800','/wd4996','/D_XBOX','/DUC_PROFILE','/DREC_NO_DIRTY','/GF','/Gy') + $opt
if ($Diag) { $common += '/DUC_DIAG' }               # checkpoints, disk log on screen (see main360.cpp)
if (-not $NoDcsThread) { $common += '/DDCS_THREAD' }   # dcs.c: the sound DSP runs on its own core (same result)
if ($LogExc) { $common += '/DUC_LOGEXC' }           # every MIPS exception into the log (diagnostics)
$crt = if ($Debug) { '/MTd' } else { '/MT' }

function Compile([string]$file, [string[]]$flags, [string]$name) {
    if (-not $name) { $name = [IO.Path]::GetFileNameWithoutExtension($file) }
    $o = & cl @common $crt @flags /Fo"$obj\$name.obj" /Fd"$obj\vc.pdb" $file 2>&1 | Out-String
    $bad = $o -split "`n" | Where-Object { $_ -match ': (fatal )?error |: warning ' }
    if ($bad) { $bad | ForEach-Object { Write-Host $_.Trim() } }
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $file" }
    "$obj\$name.obj"
}

$objs = @()
foreach ($c in $core) { $objs += Compile "$src\$c.c" @('/TP','/FIx360_compat.h',"/I$src") }
$objs += Compile (Join-Path $here $Main) @('/TP','/FIx360_compat.h',"/I$src","/I$here") 'main360'
$objs += Compile (Join-Path $here 'boot.cpp') @('/TP','/FIx360_compat.h',"/I$src")
$objs += Compile "$src\third_party\libchdr_all.c" @('/TC','/wd4244',"/I$src\third_party\libchdr\include")   # C4244: libchdr's own narrowing, upstream code
if ($Rec) {
    # generated code: unused labels and narrowing are expected there
    Get-ChildItem $Rec -Filter *.c | ForEach-Object {
        $objs += Compile $_.FullName @('/TP','/bigobj','/wd4102','/wd4244','/FIx360_compat.h',"/I$src") ("rec_" + $_.BaseName)
    }
} else {
    $objs += Compile "$src\rec_none.c" @('/TP','/FIx360_compat.h',"/I$src")
}

$libs = if ($Debug) { 'xapilibd.lib','d3d9d.lib','d3dx9d.lib','xgraphicsd.lib','xaudiod2.lib','xmcored.lib','xinput2d.lib','xboxkrnl.lib' }
        else { 'xapilib.lib','d3d9.lib','d3dx9.lib','xgraphics.lib','xaudio2.lib','xmcore.lib','xinput2.lib','xboxkrnl.lib' }
& link /nologo /DEBUG /OPT:REF /OPT:ICF /OUT:"$out\kix360.exe" /PDB:"$out\kix360.pdb" @objs @libs 2>&1 |
    ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
# Every build: a stale .xex beside a new .exe would go to the console unnoticed.
& imagexex /nologo /IN:"$out\kix360.exe" /OUT:"$out\kix360.xex"
if ($LASTEXITCODE -ne 0) { throw 'imagexex failed' }
Get-Item "$out\kix360.xex" | Select-Object FullName, Length
