// I/O space. The two games' boards put the same ports at different
// addresses (game.h).
#include "mem.h"
#include "cpu.h"
#include "game.h"
#include "ide.h"
#include "sound.h"
#include <stdio.h>

// 16-byte aligned so aligned guest accesses are aligned host ones (byte-reversed
// loads on the 360 are slow when they are not).
#ifdef _MSC_VER
__declspec(align(16))
#endif
uint8_t ram[RAM_SIZE];
uint8_t ram_dirty[RAM_PAGES];
uint8_t rom[ROM_SIZE];
hw_t hw = { 0, 0, 0, 0, 0, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };   // p1, p2, volume, dsw active-low idle

const uint16_t *video_base(void)
{
    return (const uint16_t *)&ram0[(hw.vram_control & 4) ? 0x58000 : 0x30000];
}

static uint32_t volume_r(void)
{
    uint32_t v = hw.volume & ~2u;
    if (sound_input_empty()) v |= 2;
    return v;
}

static uint32_t io_read32(uint32_t pa)
{
    // the disk's registers first: read most (its data port, while the game loads)
    if (pa - 0x10000100u < 0x40u) return ide_read((pa - 0x10000100) >> 3);
    if (pa == game.io.p1) return hw.p1;
    if (pa == game.io.p2) return hw.p2;
    if (pa == game.io.volume) return volume_r();
    if (pa == game.io.dsw) return hw.dsw;
    if (pa == game.io.unused) return 0xffffffffu;
    if (pa == 0x10000170) return ide_alt_status();
    if (pa >= 0x10000100 && pa < 0x10000140) return ide_read((pa - 0x10000100) >> 3);
    // Nothing there: reads give 0, as in MAME. KI1 v1.4 reads 32 KB past the end
    // of RAM0 when it copies the screen from the second frame buffer.
    static int logged;
    if (logged < 8) fprintf(stderr, "io: unmapped read %08x (pc %08x)%s\n", pa, cpu.cur_pc, ++logged == 8 ? "; no more shown" : "");
    return 0;
}

uint32_t io_read(uint32_t pa, int size)
{
    uint32_t v = io_read32(pa & ~3u);
    int sh = (pa & 3) * 8;
    if (size == 1) return (v >> sh) & 0xff;
    if (size == 2) return (v >> sh) & 0xffff;
    return v;
}

void io_write(uint32_t pa, uint32_t val, int size)
{
    uint32_t a = pa & ~3u;
    if (size != 4) {
        int sh = (pa & 3) * 8;
        val <<= sh;
    }
    if (a == game.io.volume) {          // sound control: data is sent on a rising bit 1
        uint32_t prev = hw.sound_control;
        hw.sound_control = val;
        if (!(prev & 2) && (val & 2)) sound_data_w((uint16_t)hw.sound_data);
        return;
    }
    if (a == game.io.p2) { hw.sound_reset = val; sound_reset_w(val & 1); return; }
    if (a == game.io.p1) { hw.vram_control = val; return; }
    if (a == game.io.unused) { hw.sound_data = val; return; }
    if (a == game.io.coin) { hw.coin_control = val; return; }
    if (a == game.io.dsw || (game.io.nop && a == game.io.nop)) return;
    switch (a) {
    case 0x10000000: return;   // written 1 at boot (board control/watchdog)
    case 0x10000178: return;   // IDE cs1 reg 7 (drive address), unused
    case 0x10000170: ide_devctl(val); return;
    }
    if (a >= 0x10000100 && a < 0x10000140) { ide_write((a - 0x10000100) >> 3, val); return; }
    static int logged;
    if (logged < 8) fprintf(stderr, "io: unmapped write %08x = %08x (pc %08x)%s\n", pa, val, cpu.cur_pc, ++logged == 8 ? "; no more shown" : "");
}
