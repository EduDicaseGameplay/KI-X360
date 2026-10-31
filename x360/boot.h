// Finding the game's files and powering the machine on, shared by the Xbox
// 360 frontend (main360.cpp) and the PC test harness (pc/harness.cpp), so
// both run exactly the same start-up.
#pragma once
#include <stddef.h>

// Folder holding the game files (MAME layout: kinst.zip, kinst\kinst.chd),
// with a trailing separator; also where cache\, saves\ and the log go.
void boot_set_root(const char *dir);

// Language of every text the port itself shows (the game's own text is in
// its ROMs and stays English): 0 English (the default), 1 Brazilian Portuguese.
enum { LANG_EN, LANG_PT, LANG_N };
void ui_set_lang(int lang);
int ui_lang(void);
const char *tr(const char *en, const char *pt);

// Use the disk image's own settings sectors and never save them (checkpoint
// comparisons between machines: a saved settings file changes the boot).
void boot_set_pristine(bool on);

// Load ROMs, open the disk and reset the machine. False, with a message for
// the player in err, when files are missing.
bool boot_machine(char *err, size_t n);

// Checkpoints for comparing a console run with the PC harness: at fixed game
// frames both compute hashes of RAM0, RAM2, the CPU and the sound DSP. Guest
// memory is little-endian on every host and registers are hashed as values,
// so the same emulation gives the same hashes on the PC and the 360.
enum { CK_RAM0, CK_RAM2, CK_CPU, CK_DSP, CK_PARTS };
bool checkpoint_frame(unsigned long long frame);
void checkpoint_hashes(unsigned long long h[CK_PARTS]);

// Compare with the PC reference built in (reference.h): -1 when this frame has
// none, else a mask of the parts that differ (0 = all equal).
int checkpoint_check(unsigned long long frame, const unsigned long long h[CK_PARTS]);
