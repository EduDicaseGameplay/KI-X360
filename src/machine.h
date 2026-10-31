// The emulated machine as the frontends see it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_HZ 58.9634              // KI2 video refresh

void run_frame(void);                 // one video frame of CPU, disk and sound
size_t machine_state(uint8_t *buf, bool load);   // frame-loop and I/O register state
uint64_t machine_frame(void);         // frames run since power-on
void machine_reset(void);             // power-cycle the cabinet
// Netplay: emulate one frame with these inputs (BTN_* bits) and checkpoint it,
// and power-on + boot frames identically on both sides.
void machine_run_inputs(const uint16_t in[2], bool resim);
void machine_netplay_reset(void);
void to_rgb(uint32_t *dst);           // current 320x240 frame as 0x00RRGGBB
const char *base_dir(void);           // directory of the executable, with trailing separator
const char *data_dir(void);           // writable folder for settings, cache, states (trailing separator)
const char *find_data(const char *name, char *buf, size_t n);
int  frontend_main(void);             // windowed mode (frontend.c)

// Game versions (game.versions): the boot ROM and its settings file.
int  machine_version(void);           // index of the running one, -1 = a ROM given by path
bool machine_version_available(int v);   // its boot ROM is there
// Load version v's boot ROM and settings; then reset the machine and get its
// translated code (the frontend does both).
bool machine_switch_version(int v, char *err, size_t n);
bool machine_both_installed(void);    // both games' files are there
void machine_restart_to_picker(void); // save, and start again at the game picker
