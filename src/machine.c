// The emulated machine's frame loop, shared by every frontend (main.c for
// the PC, x360/ for the Xbox 360): one video frame of CPU, disk and sound,
// power-on reset, and the frame as RGB.
#include "machine.h"
#include "cpu.h"
#include "game.h"
#include "ide.h"
#include "mem.h"
#include "rec.h"
#include "sound.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

#define CPU_HZ        100000000
#define LINES         261
#define VISIBLE_LINES 240
#define CYCLES_PER_LINE ((int)(CPU_HZ / FRAME_HZ / LINES))
// CPU cycles run between steps of the disk and the sound DSP. Each step leaves
// translated code and dispatches again, which is costly on the in-order
// Xbox 360 CPU; a few microseconds of latency is far below anything the
// game or the sound board can notice.
#ifndef MACHINE_SLICE
#define MACHINE_SLICE 1024
#endif

static struct {
    int sound_acc;        // CPU cycles not yet passed on to the DCS
    uint64_t frame;       // frames run since power-on
} m;
#define sound_acc m.sound_acc

uint64_t machine_frame(void) { return m.frame; }

size_t machine_state(uint8_t *buf, bool load)
{
    if (buf && load) { memcpy(&m, buf, sizeof m); memcpy(&hw, buf + sizeof m, sizeof hw); }
    else if (buf) { memcpy(buf, &m, sizeof m); memcpy(buf + sizeof m, &hw, sizeof hw); }
    return sizeof m + sizeof hw;
}

// Power-cycle: as at startup, without reloading anything from disk.
void machine_reset(void)
{
    memset(ram, 0, RAM_SIZE);
    memset(&m, 0, sizeof m);
    uint32_t dsw = hw.dsw;     // switches stay as set
    memset(&hw, 0, sizeof hw);
    hw.p1 = hw.p2 = hw.volume = 0xffffffffu;
    hw.dsw = dsw;
    sound_power_on();
    ide_power_on();
    cpu_reset();
    rec_reset();
    state_init();
}

// Profiling (UC_PROFILE builds): host time spent in the sound DSP, read by
// the frontend to see how a frame's time splits between the two processors.
#ifdef UC_PROFILE
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
double machine_sound_ms;
double machine_dsp_ms;          // dcs.c: time running the DSP itself (catch_up)
#define PROF_START() LARGE_INTEGER p0_, p1_; QueryPerformanceCounter(&p0_)
#define PROF_END() do { static LARGE_INTEGER f_; if (!f_.QuadPart) QueryPerformanceFrequency(&f_); \
        QueryPerformanceCounter(&p1_); machine_sound_ms += (double)(p1_.QuadPart - p0_.QuadPart) * 1000.0 / (double)f_.QuadPart; } while (0)
#else
#define PROF_START() ((void)0)
#define PROF_END() ((void)0)
#endif

void run_frame(void)
{
    m.frame++;
    for (int line = 0; line < LINES; line++) {
        if (line == VISIBLE_LINES) cpu_set_irq(0, true);
        if (line == 0) cpu_set_irq(0, false);
        int remaining = CYCLES_PER_LINE;
        while (remaining > 0) {
            int slice = remaining < MACHINE_SLICE ? remaining : MACHINE_SLICE;
            int ran = (int)cpu_run((uint64_t)slice);
            if (cpu_idle) {
                // spinning on an interrupt line: nothing changes until the next scanline
                cpu_skip((uint64_t)(remaining - ran));
                slice = remaining;
            }
            ide_tick(slice);
            // DCS runs at 10 MHz against the 100 MHz R4600
            sound_acc += slice;
            PROF_START();
            sound_run(sound_acc / 10);
            PROF_END();
            sound_acc %= 10;
            remaining -= slice;
        }
    }
}

void to_rgb(uint32_t *dst)
{
    const uint16_t *src = video_base();
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t p = ld_le16(&src[y * 320 + x]);
            uint32_t r = (p & 31), g = (p >> 5) & 31, b = (p >> 10) & 31;
            dst[y * 320 + x] = (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2);
        }
}

/* Netplay callbacks shared with the frontend: emulate with explicit inputs. */
void machine_run_inputs(const uint16_t in[2], bool resim)
{
    hw.p1 = 0xffffffffu & ~(uint32_t)(in[0] & 0x0fff);
    hw.p2 = 0xffffffffu & ~(uint32_t)(in[1] & 0x0fff);
    run_frame();
    static int desync_at = -1;                      // testing: KI2_DESYNC_AT=frame breaks this side
    if (desync_at < 0) desync_at = getenv("KI2_DESYNC_AT") ? atoi(getenv("KI2_DESYNC_AT")) : 0;
    if (desync_at && machine_frame() == (uint64_t)desync_at && !resim) { ram2[game.vars + 0x10] ^= 0x55; ram_touch(&ram2[game.vars + 0x10]); }
    state_checkpoint();
    if (resim) { int16_t junk[4096]; while (sound_fetch(junk, 4096) > 0) {} }
}

