// Analog Devices ADSP-2105 DSP core (as used on the Midway DCS sound board).
// Behaviour cross-checked against MAME's ADSP-21xx core (BSD-3-Clause, Aaron Giles).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "adsp_core.h"     // adsp_hooks_t, interrupt lines

void adsp_init(const adsp_hooks_t *hooks);
void adsp_reset(void);
void adsp_run(int cycles);
void adsp_set_irq(int line, bool state);
void adsp_pulse_irq(int line);
uint32_t *adsp_internal_pm(void);   // 1K x 24-bit, filled by the boot loader
uint16_t adsp_pc(void);
size_t adsp_state(uint8_t *buf, bool load);    // NULL buf: size only

// Translated code (rec_abi.h's rec_dsp_image_t), from the game's module; NULL to stop using it.
struct rec_dsp_image_s;
void adsp_use_code(const struct rec_dsp_image_s *img, uint32_t n);
void adsp_pm_changed(uint16_t addr);   // the board changed the program word at addr
void adsp_code_reload(void);           // program memory was reloaded (boot, state load)
void *adsp_core(void);                 // the adsp_t, for translated code
size_t adsp_core_size(void);
