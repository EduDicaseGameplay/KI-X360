# KI-X360

*English | [Português](LEIAME.md)*

Arcade Killer Instinct (1994) running **natively** on a modded Xbox 360: full speed, the arcade's own sound, two controllers, 720p, picture filters and a settings menu in English and Portuguese.

This isn't a generic emulator stepping through the game one instruction at a time. It starts from [UltraCombo](https://github.com/thefixinhixon/UltraCombo), a PC port that translates the game's own MIPS code to C, and I adapted UltraCombo to the Xbox 360. The game's code is turned into C on your PC, from **your** files, and compiled into the `.xex` with everything else. The hardware around it (video, IDE disk, DCS sound board, controls) is still emulated.

> **There is no game in here.** No ROMs, no disk image (CHD), no translated code, no prebuilt `.xex`, and no Microsoft SDK. You need your own game files and the SDK installed to build it. Please don't ask me for ROMs.

---

## What works

- Full speed in fights and during loading (tested on a real console, playing whole matches)
- The original DCS board's sound, with the sound processor on a thread of its own
- Controller 1 and controller 2
- 720p output (3x integer scale, 4:3) or 480 lines; the console scales it to 1080
- Settings menu: aspect (4:3, 4:3 with blurred sides, full 16:9, 16:9 zoom), filters (sharp, smooth, CRT with scanlines), brightness, gamma, saturation, sound volume and language
- Attack buttons you can change, separately for each player, saved until you change them again
- High scores and the arcade's settings are really saved (name entry, operator test mode)
- FPS / speed display

## What you need

### On the console
- A modded Xbox 360 (RGH/JTAG) with Aurora, FreeStyle Dash or XeXMenu
- A USB stick or HDD with about 120 MB free

### On the PC (Windows)
Install in this order:

1. **Visual Studio 2010** with C++. The PC harness uses its x86 compiler, and the 360 SDK hooks into it, so it goes first.
2. **Xbox 360 SDK (XDK)**. The installer creates the `XEDK` environment variable, which the build script uses. I used an XDK whose compiler is `cl` 16.00.11886.
3. **Git for Windows** (it comes with Git Bash), to download the project.

Python isn't needed. Only the optional `ppm2png.py`, for viewing the pictures the harness saves, uses it.

### The game files
MAME's **kinst** set, as it is:

```
kinst.zip          boot and sound ROMs
kinst\kinst.chd    the arcade's hard disk
```

I tested with MAME's current set (boot ROM version 1.5d) and the standard CHD. At start-up the program checks the CRCs and the CHD's SHA1.

## Building

Everything is done in PowerShell, **always from the project's root** (the `KI-X360` folder, where this file is): command paths such as `x360\pc\build_pc.ps1` start from there. If you get "The argument ... to the -File parameter does not exist", PowerShell is in another folder; go back to the root with `cd`. In the examples the game files are in `C:\ki\game`; use your own path.

```
C:\ki\game\kinst.zip
C:\ki\game\kinst\kinst.chd
```

**1. Download the project** (Git Bash):

```bash
git clone https://github.com/EduDicaseGameplay/KI-X360.git
cd KI-X360
```

**2. Build the PC harness.** It's the emulator core without a screen, running on Windows. It's used to make the translated code and for testing.

```
powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1
```

**3. Make the translated code from your files.** The script runs the game for 900 frames on the PC, takes a snapshot of the RAM and translates the MIPS code in it to C. It takes a minute or two.

```
powershell -ExecutionPolicy Bypass -File x360\recomp\make_rec.ps1 -Root C:\ki\game -Out C:\ki\rec
```

**4. Build the `.xex` with that code:**

```
powershell -ExecutionPolicy Bypass -File x360\build360.ps1 -Rec C:\ki\rec -LogExc
```

The result is `x360\build\kix360.xex`. `-LogExc` only writes the game CPU's exceptions to the log, which helps if something goes wrong.

It also builds without `-Rec`, but then the game runs on the interpreter alone, far too slow on the 360. That's only useful to check that the SDK is installed properly.

**(Optional) Test on the PC first**, with the same translated code:

```
powershell -ExecutionPolicy Bypass -File x360\pc\build_pc.ps1 -Rec C:\ki\rec
x360\pc\build\harness.exe C:\ki\game 3000 --ppm-every 1500
```

The harness prints the milliseconds per frame (the arcade's frame is 16.96 ms) and saves pictures of frames 1500 and 3000.

> **Careful:** the translated code folder and any `.xex` built with `-Rec` contain the game's code. They're for you only. Don't upload them to GitHub or share them. The `.gitignore` already keeps all of that out.

## Installing on the console

Make a folder on the USB stick or HDD like this:

```
KI-X360\
  default.xex        (x360\build\kix360.xex, renamed)
  kinst.zip
  kinst\kinst.chd
```

Start `default.xex` from Aurora, FreeStyle or XeXMenu.

- The first time, it asks for the language (English or Portuguese).
- Every time it starts, "LOADING..." shows for a few seconds while the whole disk is read into memory. Reading it piece by piece from a USB stick during the game is very slow.
- Right after that, the disk is decompressed in the background. On my console that took about 7 seconds while the intro screens were running. From then on loading is smooth.

The program creates, next to the `.xex`:

| File | What it is |
|---|---|
| `cache\roms\kinst\` | the ROMs extracted from the zip (first time only) |
| `saves\kix360.ini` | your menu settings and buttons |
| `saves\ki-l15d.settings` | the arcade's settings and high scores |
| `kix360.log` | log of the last session: speed, loading, errors |

## Controls

| Button | Arcade |
|---|---|
| D-pad / left stick | directions |
| X, Y, RB | quick, medium, fierce punch |
| A, B, RT | quick, medium, fierce kick |
| Start | start |
| Back | coin |
| Back + LB + RB | operator test mode (difficulty, etc.) |
| **Right** stick click | settings menu (pauses the game) |
| Left stick click | show/hide the speed |
| Back + Start (hold 2 s) | quit |

Those are the default attack buttons. To change them, open the menu and go to **Controller buttons**: pick the player and the attack, press A, then press the new button. Any of A, B, X, Y, LB, RB, LT and RT will do. If the button is already taken, the two attacks swap, so all six always have a button. Your choice is saved right away and stays until you change it, and **Restore defaults** puts that player back. Directions, Start, Back and the stick clicks can't be moved, so you can always reach the menu and quit.

Free play is on. The game's own text is in the ROM and stays in English.

## Picture and sound

The arcade's picture is 4:3, so a 16:9 TV needs a choice, and the menu has four:

| Screen | What you get |
|---|---|
| 4:3 original | The real proportions, with black bars on the sides |
| 4:3 + side fill | The real proportions, with the picture blurred on the sides |
| 16:9 full | Fills the TV and shows everything, but the fighters are a third wider |
| 16:9 zoom | Keeps the proportions and fills the TV, but cuts off the life bars and the feet |

**Sound volume** goes from 50% to 300%, and it starts at 200%. The arcade board on its own is quiet: a cabinet had an amplifier after it. 100% is the board's output as it is. At 200% my recording showed no distortion; if you go higher and it does distort, turn it down a step.

## Common problems

- **`XEDK is not set`**: the 360 SDK isn't installed, or PowerShell was opened before it was. Close it and open it again.
- **Error looking for `vcvarsall.bat`**: Visual Studio 2010 is missing (the harness uses its `VS100COMNTOOLS` variable).
- **Internal compiler error (C1001) in a function `f_XXXXXXXX`**: the XDK's compiler sometimes can't handle an optimised function. Put its address (the `XXXXXXXX`) on a new line of `x360\recomp\lista-protegida.txt` and make the code again (step 3).
- **`the harness did not run the game`**: check that `kinst.zip` and `kinst\kinst.chd` are in the folder given as `-Root`.
- **The game froze or slowed down somewhere**: `kix360.log` shows the speed every few seconds and what the game was doing.

## How it works inside (in short)

- **CPU** (MIPS R4600): the game's code is translated to C beforehand, on the PC (`src/recomp.c`). At run time `src/rec.c` checks that the code in RAM is still what was translated and calls the matching C function. Whatever wasn't translated falls back to the interpreter (`src/cpu.c`).
- **Protected functions**: the functions that talk to the hardware and the game's disk driver are translated without optimisation (`x360/recomp/lista-protegida.txt`). Optimised, they broke the disk protocol on the 360.
- **Sound**: the DCS board's ADSP-2105 DSP is also translated to C and runs on its own thread, on a separate core.
- **Disk**: the CHD is read whole into memory and decompressed in the background (`src/ide.c`).
- **Controller** (`x360/padmap.h`, tested on the PC by `x360/pc/padmap_test.cpp`): the button map sits outside the emulation. The game only ever sees the arcade's buttons.
- **360 frontend** (`x360/main360.cpp`): Direct3D 9, XAudio2 and XInput. Emulation runs on a thread on core 1; picture and sound stay on the main thread.
- **PC harness** (`x360/pc/harness.cpp`): runs the same core without a screen. It computes hashes of RAM and CPU at fixed frames, to compare versions, and has a sampling profiler (`--sample`).
- The 360's PowerPC is big-endian and the game is little-endian: the game's memory stays little-endian and every access swaps the bytes.

The full development history, with the problems that showed up on the console and how they were solved, is in [docs/historico-tecnico.md](docs/historico-tecnico.md) (in Portuguese).

## Credits and licence

- **UltraCombo**, by thefixinhixon: the emulator and code translator this port is based on. MIT licence.
- **libchdr** (BSD), with the **LZMA SDK** (public domain), **zstd** (BSD) and **miniz** (MIT), to read the CHD. **stb_image** (public domain / MIT).
- **MAME**: the files the program reads are those of MAME's `kinst` set (`kinst.zip` and `kinst.chd`).

The code in this repository is under the MIT licence (see [LICENSE](LICENSE)). The libraries in `src/third_party` keep their own licences.

Killer Instinct is a trademark of its owners. This project has no connection with them, contains nothing from the game and only works with files you already have.
