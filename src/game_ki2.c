// UltraCombo 2: Killer Instinct 2 (MAME kinst2). RAM locations are for the
// L1.4 code and were found by diffing deterministic runs.
#include "game.h"

#ifdef _MSC_VER
// VS2010 (Xbox 360 build) has no designated initializers: the same profile,
// filled in by assignments. Keep it in step with the initializer below.
#include <string.h>
static game_t make_game_ki2(void)
{
    game_t g;
    memset(&g, 0, sizeof g);
    g.id = "ki2";
    g.title = "Killer Instinct 2";
    g.product = "UltraCombo 2";
    g.icon_text = "UC2";
    { static const decltype(g.versions) v = { { "1.4", "ki2-l14.u98", 0x27d0285e } }; memcpy(g.versions, v, sizeof v); }
    { static const decltype(g.sound_roms) v = { "ki2_l1.u10", "ki2_l1.u11", "ki2_l1.u12", "ki2_l1.u13", "ki2_l1.u33", "ki2_l1.u34", "ki2_l1.u35", "ki2_l1.u36" }; memcpy(g.sound_roms, v, sizeof v); }
    { static const decltype(g.sound_crc) v = { 0xfdf6ed51, 0xf9e70024, 0x2994c199, 0x3fe6327b, 0x6f4dcdcf, 0x5db48206, 0x7245ce69, 0x8920acbb }; memcpy(g.sound_crc, v, sizeof v); }
    { static const decltype(g.sound_code) v = { { 0x0000, 0x0000, 0, true }, { 0x1000, 0x0000, 0, true }, { 0x0800, 0x0800, 0x300 }, { 0x2000, 0x0800, 0x400 } }; memcpy(g.sound_code, v, sizeof v); }
    g.set = "kinst2";
    g.disk = "kinst2.raw";
    g.chd = "kinst2.chd";
    g.chd_sha1 = "e7c9291b4648eae0012ea0cc230731ed4987d1d5";
    g.cyls = 1463;
    g.heads = 13;
    g.secs = 47;
    g.serial_offset = 11;
    g.io.p1 = 0x10000098;
    g.io.p2 = 0x10000090;
    g.io.volume = 0x10000080;
    g.io.unused = 0x100000a0;
    g.io.dsw = 0x10000088;
    g.io.coin = 0x100000b8;
    g.io.nop = 0x100000b0;
    g.snapshot_frames = 900;
    g.input_lag = 1;
    g.credits = 0x7ff124;
    g.credits_needed = 2;
    g.health_p1 = 0x7fc105;
    g.health_step = 0x100;
    g.health_max = 120;
    g.timer_tick = 0x7f7a10;
    g.timer_ones = 0x7f7a11;
    g.timer_tens = 0x7f7a12;
    g.vars = 0x7f0000;
    g.vars_len = 0x10000;
    g.adj_sum = 0xa00;
    g.adj_data = 0xa04;
    g.adj_len = 88;
    g.adj_difficulty = 0xa1c;
    return g;
}
const game_t game_ki2 = make_game_ki2();
#else
const game_t game_ki2 = {
    .id = "ki2",
    .title = "Killer Instinct 2",
    .product = "UltraCombo 2",
    .icon_text = "UC2",

    .versions = { { "1.4", "ki2-l14.u98", 0x27d0285e } },
    .sound_roms = { "ki2_l1.u10", "ki2_l1.u11", "ki2_l1.u12", "ki2_l1.u13",
                    "ki2_l1.u33", "ki2_l1.u34", "ki2_l1.u35", "ki2_l1.u36" },
    .sound_crc = { 0xfdf6ed51, 0xf9e70024, 0x2994c199, 0x3fe6327b,
                   0x6f4dcdcf, 0x5db48206, 0x7245ce69, 0x8920acbb },
    // the boot loader page, the sound OS it reboots into (bank 1), and the
    // two programs the OS copies into external RAM
    .sound_code = { { 0x0000, 0x0000, 0, true }, { 0x1000, 0x0000, 0, true },
                    { 0x0800, 0x0800, 0x300 }, { 0x2000, 0x0800, 0x400 } },
    .set = "kinst2",
    .disk = "kinst2.raw",
    .chd = "kinst2.chd",
    .chd_sha1 = "e7c9291b4648eae0012ea0cc230731ed4987d1d5",
    .cyls = 1463, .heads = 13, .secs = 47,
    .serial_offset = 11,
    .io = { .p1 = 0x10000098, .p2 = 0x10000090, .volume = 0x10000080,
            .unused = 0x100000a0, .dsw = 0x10000088, .coin = 0x100000b8, .nop = 0x100000b0 },

    .snapshot_frames = 900,
    .input_lag = 1,

    .credits = 0x7ff124, .credits_needed = 2,
    .health_p1 = 0x7fc105, .health_step = 0x100, .health_max = 120,
    .timer_tick = 0x7f7a10, .timer_ones = 0x7f7a11, .timer_tens = 0x7f7a12,
    .vars = 0x7f0000, .vars_len = 0x10000,

    // checksum of the 88 bytes after it, computed by the game at 0x8802d400;
    // +4 is set once the adjustments were edited
    .adj_sum = 0xa00, .adj_data = 0xa04, .adj_len = 88, .adj_difficulty = 0xa1c,
};
#endif
