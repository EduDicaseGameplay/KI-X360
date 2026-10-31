// ADSP-2105 run loop: translated code where there is some for the program
// in memory (adsprec.c, loaded with the game's module), the interpreter for
// the rest. The instruction semantics live in adsp_core.h, shared by both.
#include "adsp.h"
#include "rec_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static adsp_t a;
#define A a
#define ADSP_PM_WRITTEN(addr) adsp_pm_changed(addr)
#include "adsp_core.h"

// Translated images and, per word, whether program memory still holds what
// each was translated from. Not machine state: it follows from memory.
#define MAX_CODE 64
#define PER_WORD 4                            // images that may cover one address
static const rec_dsp_image_t *code;
static uint32_t ncode;
static uint8_t *code_ok[MAX_CODE];
static uint8_t (*code_for)[PER_WORD];         // per address: the images covering it, 0xff = none
static uint64_t ran_native, ran_interp;     // KI2_LOG_SOUND

void adsp_pm_changed(uint16_t addr)
{
    if (!ncode) return;
    addr &= 0x3fff;
    for (int j = 0; j < PER_WORD && code_for[addr][j] != 0xff; j++) {
        uint32_t i = code_for[addr][j], k = (uint32_t)(addr - code[i].base);
        code_ok[i][k] = pm_read(addr) == code[i].image[k];
    }
}

void adsp_code_reload(void)
{
    for (uint32_t i = 0; i < ncode; i++)
        for (uint32_t k = 0; k < code[i].words; k++)
            code_ok[i][k] = code[i].base + k < 0x4000 && pm_read((uint16_t)(code[i].base + k)) == code[i].image[k];
}

static void log_coverage(void)
{
    uint64_t all = ran_native + ran_interp;
    if (all) fprintf(stderr, "dcs: %.2f%% of DSP cycles ran translated\n", 100.0 * ran_native / all);
}

void adsp_use_code(const rec_dsp_image_t *img, uint32_t n)
{
    for (uint32_t i = 0; i < ncode; i++) free(code_ok[i]);
    if (n > MAX_CODE || getenv("KI2_NOADSP")) n = 0;
    if (!code_for) code_for = (decltype(code_for))malloc(0x4000 * sizeof *code_for);
    memset(code_for, 0xff, 0x4000 * sizeof *code_for);
    code = img;
    ncode = n;
    for (uint32_t i = 0; i < n; i++) {
        code_ok[i] = (uint8_t *)calloc(code[i].words, 1);
        for (uint32_t k = 0; k < code[i].words && code[i].base + k < 0x4000; k++) {
            uint8_t *c = code_for[code[i].base + k];
            int j = 0;
            while (j < PER_WORD && c[j] != 0xff) j++;
            if (j < PER_WORD) c[j] = (uint8_t)i;
        }
    }
    adsp_code_reload();
    static bool logging;
    if (ncode && !logging && getenv("KI2_LOG_SOUND")) { logging = true; atexit(log_coverage); }
}

void *adsp_core(void) { return &a; }
size_t adsp_core_size(void) { return sizeof a; }

// The image to run the word at pc from, -1 if none holds it now.
static int code_at(uint16_t pc)
{
    for (int j = 0; j < PER_WORD && code_for[pc][j] != 0xff; j++) {
        uint32_t i = code_for[pc][j];
        if (code_ok[i][pc - code[i].base]) return (int)i;
    }
    return -1;
}

void adsp_set_irq(int line, bool state) { set_irq(line, state); }
void adsp_pulse_irq(int line) { pulse_irq(line); }

void adsp_run(int cycles)
{
    bool irq_done = false;      // translated code took an interrupt and stopped before the vector
    while (cycles > 0) {
        if (ncode && !a.idle && !a.polling && !irq_done) {
            int img = code_at(a.pc);
            if (img >= 0) {
                int done = 0, left = code[img].fn(cycles, code_ok[img], &done);
                ran_native += (uint64_t)(cycles - left);
                cycles = left;
                irq_done = done;
                continue;
            }
        }
        if (!irq_done && irq_raised() && irq_ready()) check_irqs();
        irq_done = false;
        if (a.idle || a.polling) {
            a.polling = false;
            // fast-forward to the next autobuffer sample (or the end of the slice)
            int step = cycles;
            if (a.tx_on) {
                int to_next = a.tx_period - a.tx_cnt;
                if (to_next < step) step = to_next < 1 ? 1 : to_next;
            }
            if ((a.mstat & M_TIMER) && step > 1) step = 1 + (step > 64 ? 63 : step - 1);
            timer_tick(step);
            tx_tick(step);
            cycles -= step;
            continue;
        }

        uint32_t op = pm_read(a.pc);
        sequence();
        exec(op);
        clock_cycle();
        cycles--;
        if (ncode) ran_interp++;
    }
}

void adsp_init(const adsp_hooks_t *hooks)
{
    memset(&a, 0, sizeof(a));
    a.h = *hooks;
}

void adsp_reset(void)
{
    adsp_hooks_t h = a.h;
    uint32_t ipm[0x400];
    memcpy(ipm, a.ipm, sizeof ipm);
    memset(&a, 0, sizeof(a));
    a.h = h;
    memcpy(a.ipm, ipm, sizeof ipm);
    a.pc = 0;
    a.loop_end = 0xffff;
    a.sstat = 0x55;
    for (int n = 0; n < 8; n++) update_l(n);
}

uint32_t *adsp_internal_pm(void) { return a.ipm; }

size_t adsp_state(uint8_t *buf, bool load)
{
    if (buf && load) {
        adsp_hooks_t h = a.h;
        memcpy(&a, buf, sizeof a);
        a.h = h;
    } else if (buf) {
        memcpy(buf, &a, sizeof a);
        memset(buf + offsetof(adsp_t, h), 0, sizeof a.h);        // keep states pointer-free
    }
    return sizeof a;
}
uint16_t adsp_pc(void) { return a.pc; }
