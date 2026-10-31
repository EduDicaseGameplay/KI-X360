// Midway DCS (rev 1, "2K" external RAM) sound board: ADSP-2105 + 8 sound EPROMs.
#include "adsp.h"
#include "adsprec.h"
#include "sound.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_WORDS   0x800000u          // 16 MB 16-bit region, EPROMs on the low byte
#define BANK_WORDS  0x1000u
#define RING        65536

static uint16_t *rom;
static bool roms_ok;

static struct {                        // board state (saved in save states)
    uint32_t ext_pm[0x800];            // 2K x 24-bit, shared program/data RAM
    uint16_t bank;
    uint16_t input_data;
    bool     input_full;
    bool     held;                     // reset line asserted by the host
    double   rate;
} d;

// The DSP's time slices, run later in one go. The machine gives it a few
// cycles every 256 CPU cycles; running thousands of those back to back
// instead keeps its code and state in the caches (about 4% faster overall).
// Nothing sees the board in between except through the functions below, and
// each of them first runs what is queued, with the same slices in the same
// order: the result is exactly as if every slice had run on time.
static struct {
    uint16_t cycles[16384];             // a slice is ~26 cycles
    unsigned n;
} queued;

#ifdef UC_PROFILE
// Profiling builds: host time spent running the DSP (here, not in sound_run,
// which only queues cycles), added to machine.c's counter.
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
extern double machine_dsp_ms;
unsigned dcs_sync_count[4];   // profiling: host syncs by kind (status poll, command, reset, fetch)
#define SYNC_COUNT(k) (dcs_sync_count[k]++)
#else
#define SYNC_COUNT(k) ((void)0)
#endif

static struct {                        // output to the host's audio, not machine state
    int16_t  ring[RING];
    unsigned wpos, rpos;
    bool     log;
} output;

#ifdef DCS_THREAD
// DCS_THREAD builds (the Xbox 360) run the queued slices on a thread of their
// own, on another core, while the machine goes on: the same slices in the
// same order, and every function below still waits until all queued ones
// have run before it looks at or changes the board. Nothing the DSP reads
// changes between those points, so the result is exactly the same as
// running them later in one go, only in parallel with the CPU.
#ifdef _XBOX
#include <xtl.h>
#define DCS_BARRIER() __lwsync()        // PowerPC: order the slices and their count
#else
#include <windows.h>
#define DCS_BARRIER() MemoryBarrier()
#endif
#define DQ_SIZE 65536
static uint16_t dq[DQ_SIZE];
static volatile uint32_t dq_w, dq_r;    // slices queued (machine thread), slices run (DSP thread)
static HANDLE dq_wake;
static uint32_t dq_unannounced;         // queued since the DSP thread was last woken
static volatile unsigned dq_out;        // output samples written by the slices that have run
static volatile double dq_rate;         // the output rate as of those slices

static DWORD WINAPI dsp_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(dq_wake, INFINITE);
        uint32_t r = dq_r;
        for (;;) {
            uint32_t w = dq_w;
            DCS_BARRIER();                  // the slices are read only after their count
            if (r == w) break;
            for (; r != w; r++)
                if (!d.held) adsp_run(dq[r % DQ_SIZE]);
            DCS_BARRIER();                  // the DSP's writes land before the count says they ran
            dq_out = output.wpos;           // samples ready for sound_fetch_async
            dq_rate = d.rate;
            dq_r = r;
        }
    }
}

static void dq_start(void)
{
    if (dq_wake) return;
    dq_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 256 * 1024, dsp_thread, NULL, CREATE_SUSPENDED, NULL);
#ifdef _XBOX
    XSetThreadProcessor(h, 5);              // core 2, beside XAudio2 (pinned to thread 4; core 1: the machine; core 0: video)
#endif
    ResumeThread(h);
}
#endif

static void catch_up(void)
{
#ifdef UC_PROFILE
    LARGE_INTEGER t0, t1;
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
#endif
#ifdef DCS_THREAD
    if (dq_r != dq_w) {
        SetEvent(dq_wake);
        while (dq_r != dq_w) {}
    }
    DCS_BARRIER();                          // see everything the DSP wrote
    dq_unannounced = 0;
#else
    for (unsigned i = 0; i < queued.n; i++)
        if (!d.held) adsp_run(queued.cycles[i]);
    queued.n = 0;
#endif
#ifdef UC_PROFILE
    QueryPerformanceCounter(&t1);
    machine_dsp_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
#endif
}


static void boot(void)
{
    // ADSP-21xx boot format: 4 bytes per instruction (MSB first), page length in byte 3
    const uint16_t *src = &rom[(d.bank * BANK_WORDS) % ROM_WORDS];
    uint32_t *ipm = adsp_internal_pm();
    int words = ((src[3] & 0xff) + 1) * 8;
    for (int i = 0; i < words && i < 0x400; i++)
        ipm[i] = (uint32_t)(src[i * 4] & 0xff) << 16 | (uint32_t)(src[i * 4 + 1] & 0xff) << 8 | (src[i * 4 + 2] & 0xff);
    adsp_code_reload();
}

// The 2K external RAM appears at program addresses 0x0800-0x1fff (three
// times) and, by its upper 16 bits, at data addresses 0x0000-0x1fff.
static void ext_changed(uint16_t i)
{
    for (uint16_t pm = 0x0800; pm < 0x2000; pm += 0x0800) adsp_pm_changed(pm | (i & 0x7ff));
}

static void board_reset(void)
{
    d.bank = 0;
    d.input_full = false;
    adsp_reset();
    adsp_set_irq(ADSP_IRQ0, false);
    adsp_set_irq(ADSP_IRQ1, false);
    adsp_set_irq(ADSP_IRQ2, false);
    boot();
}

static uint32_t pm_read(uint16_t addr)
{
    if (addr >= 0x0800 && addr < 0x2000) return d.ext_pm[addr & 0x7ff];
    return 0;
}

static void pm_write(uint16_t addr, uint32_t v)
{
    if (addr >= 0x0800 && addr < 0x2000) { d.ext_pm[addr & 0x7ff] = v; ext_changed(addr); }
}

static uint16_t dm_read(uint16_t addr)
{
    if (addr < 0x2000) return (uint16_t)(d.ext_pm[addr & 0x7ff] >> 8);
    if (addr < 0x3000) return rom[((uint32_t)d.bank * BANK_WORDS + (addr - 0x2000)) % ROM_WORDS];
    if (addr >= 0x3400 && addr < 0x3800) {
        // reading the input latch acknowledges it
        d.input_full = false;
        adsp_set_irq(ADSP_IRQ2, false);
        return d.input_data;
    }
    return 0xffff;
}

static void dm_write(uint16_t addr, uint16_t v)
{
    if (addr < 0x2000) {
        uint32_t *p = &d.ext_pm[addr & 0x7ff];
        *p = ((uint32_t)v << 8) | (*p & 0xff);
        ext_changed(addr);
        return;
    }
    if (addr >= 0x3000 && addr < 0x3400) { d.bank = v & 0x7ff; return; }
    // 0x3400-0x37ff output latch: KI2 has no read-back path to the host
}

static void sample_out(int16_t s)
{
    output.ring[output.wpos++ % RING] = s;
#ifndef DCS_THREAD      // there the host owns rpos (sound_fetch_async drops what overflowed)
    if (output.wpos - output.rpos > RING) output.rpos = output.wpos - RING;
#endif
}

static void sample_rate(double hz)
{
    if (output.log && hz != d.rate) fprintf(stderr, "dcs: sample rate %.1f Hz\n", hz);
    d.rate = hz;
}

static void reboot(void)
{
    adsp_reset();
    boot();
}

static bool load_rom(const char *dir, const char *name, uint32_t word_offset)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    uint8_t *buf = (uint8_t *)malloc(0x80000);
    size_t n = fread(buf, 1, 0x80000, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) rom[word_offset + i] = 0xff00 | buf[i];
    free(buf);
    return n == 0x80000;
}

bool sound_init(const char *rom_dir)
{
    const char *const *names = game.sound_roms;
    output.log = getenv("KI2_LOG_SOUND") != NULL;
    rom = (uint16_t *)malloc(ROM_WORDS * sizeof(uint16_t));
    for (uint32_t i = 0; i < ROM_WORDS; i++) rom[i] = 0xffff;
    bool ok = true;
    for (int i = 0; i < 8; i++)
        ok &= load_rom(rom_dir, names[i], (uint32_t)i * 0x100000u);
    if (!ok) fprintf(stderr, "dcs: sound ROMs missing, running silent\n");
    roms_ok = ok;

    sound_power_on();
    return ok;
}

// Everything back to power-on, including the DSP's RAM (identical on every machine).
void sound_power_on(void)
{
#ifdef DCS_THREAD
    catch_up();                             // the DSP thread must be idle before the reset
#endif
    queued.n = 0;
    static adsp_hooks_t h;
    memset(&h, 0, sizeof h);
    h.pm_read = pm_read; h.pm_write = pm_write;
    h.dm_read = dm_read; h.dm_write = dm_write;
    h.sample_out = sample_out; h.sample_rate = sample_rate;
    h.sysctl_reboot = reboot;
    adsp_init(&h);
    memset(&d, 0, sizeof d);
    d.rate = 31250;
    board_reset();
}

void sound_reset_w(bool state)
{
    SYNC_COUNT(2);
    catch_up();
    if (!state) {
        if (!d.held && output.log) fprintf(stderr, "dcs: reset asserted\n");
        d.held = true;
        board_reset();
    } else d.held = false;
}

void sound_data_w(uint16_t data)
{
    SYNC_COUNT(1);
    catch_up();
    if (output.log) fprintf(stderr, "dcs: cmd %04x\n", data);
    d.input_data = data;
    d.input_full = true;
    adsp_set_irq(ADSP_IRQ2, true);
}

bool sound_input_empty(void) { SYNC_COUNT(0); catch_up(); return !d.input_full; }

size_t sound_state(uint8_t *buf, bool load)
{
#ifdef DCS_THREAD
    if (buf) catch_up();                // the DSP thread must be idle (a load runs what was queued)
#else
    if (buf && load) queued.n = 0;      // what was queued belongs to the state being left
    else if (buf) catch_up();
#endif
    size_t n = sizeof d;
    if (buf && load) memcpy(&d, buf, sizeof d);
    else if (buf) memcpy(buf, &d, sizeof d);
    n += adsp_state(buf ? buf + n : NULL, load);
    if (buf && load) adsp_code_reload();
    return n;
}

// The program images the profile names, read from U10 as the boot loader and
// the sound OS read them (4 bytes per word). None without the sound ROMs.
int sound_code_images(adsprec_image_t *out, int max)
{
    static uint32_t words[6][0x800];
    int n = 0;
    for (int i = 0; roms_ok && i < 6 && n < max && (game.sound_code[i].words || game.sound_code[i].boot); i++) {
        uint32_t at = game.sound_code[i].rom;
        int len = game.sound_code[i].boot ? ((rom[at + 3] & 0xff) + 1) * 8 : game.sound_code[i].words;
        if (game.sound_code[i].boot && len > 0x400) len = 0x400;
        if (len > 0x800) len = 0x800;
        for (int k = 0; k < len; k++) {
            const uint16_t *w = &rom[(at + 4 * (uint32_t)k) % ROM_WORDS];
            words[n][k] = (uint32_t)(w[0] & 0xff) << 16 | (uint32_t)(w[1] & 0xff) << 8 | (w[2] & 0xff);
        }
        adsprec_image_t im = { game.sound_code[i].pm, (uint16_t)len, words[n] };
        out[n] = im;
        n++;
    }
    return n;
}

void sound_run(int cycles)
{
    if (cycles <= 0) return;
#ifdef DCS_THREAD
    dq_start();
    if (cycles > 0xffff) { catch_up(); if (!d.held) adsp_run(cycles); return; }
    while (dq_w - dq_r >= DQ_SIZE) SetEvent(dq_wake);     // full: let it drain
    dq[dq_w % DQ_SIZE] = (uint16_t)cycles;
    DCS_BARRIER();                          // the slice is in memory before the count
    dq_w = dq_w + 1;
    if (++dq_unannounced >= 16) { dq_unannounced = 0; SetEvent(dq_wake); }   // little left over when the frame ends
#else
    if (cycles > 0xffff || queued.n == sizeof queued.cycles / sizeof *queued.cycles) catch_up();
    if (cycles > 0xffff) { if (!d.held) adsp_run(cycles); return; }
    queued.cycles[queued.n++] = (uint16_t)cycles;
#endif
}

double sound_rate(void) { catch_up(); return d.rate; }

int sound_fetch(int16_t *out, int max)
{
    SYNC_COUNT(3);
    catch_up();
    int n = 0;
    while (n < max && output.rpos != output.wpos) out[n++] = output.ring[output.rpos++ % RING];
    return n;
}

// For the host's audio output only (never machine state): the samples the
// DSP has produced so far, without waiting for it to finish what is queued.
// What it hasn't produced yet comes with the next call. Without the DSP
// thread these are sound_fetch and sound_rate.
int sound_fetch_async(int16_t *out, int max)
{
#ifdef DCS_THREAD
    unsigned w = dq_out;
    DCS_BARRIER();                          // the samples before their count
    if (w - output.rpos > RING) output.rpos = w - RING;     // fell behind by more than the ring: skip
    int n = 0;
    while (n < max && output.rpos != w) out[n++] = output.ring[output.rpos++ % RING];
    return n;
#else
    return sound_fetch(out, max);
#endif
}

double sound_rate_async(void)
{
#ifdef DCS_THREAD
    return dq_rate > 0 ? dq_rate : d.rate;
#else
    return sound_rate();
#endif
}
