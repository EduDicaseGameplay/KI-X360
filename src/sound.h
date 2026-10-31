// DCS sound board interface.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "adsprec.h"

bool   sound_init(const char *rom_dir);
void   sound_reset_w(bool state);      // bit 0 of the sound reset register
void   sound_data_w(uint16_t data);    // latched on rising edge of sound control bit 1
bool   sound_input_empty(void);        // DCS has consumed the last command word
void   sound_run(int cycles);          // advance the sound DSP (10 MHz cycles)
double sound_rate(void);               // current output sample rate in Hz
int    sound_fetch(int16_t *out, int max);
// For the host's audio only: what the sound DSP has produced so far, without
// waiting for it (its own thread may still be running queued cycles).
int    sound_fetch_async(int16_t *out, int max);
double sound_rate_async(void);
size_t sound_state(uint8_t *buf, bool load);   // board + DSP, for save states
void   sound_power_on(void);
int    sound_code_images(adsprec_image_t *out, int max);   // to translate; 0 without the ROMs
