// Killer Instinct 2 physical bus.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "rec_abi.h"     // RAM0_SIZE, RAM2_SIZE, ROM_SIZE, RAM_SIZE, RAM_PAGES

// Both RAMs live in one array so save states can track them page by page.
extern uint8_t ram[RAM_SIZE];
#define ram0 (ram)
#define ram2 (ram + RAM0_SIZE)
extern uint8_t rom[ROM_SIZE];

// Set for each 4 KB page of `ram` written since the last state checkpoint.
// Every store into RAM must mark its page (stores never cross a page).
extern uint8_t ram_dirty[RAM_PAGES];
#ifdef REC_NO_DIRTY       // no save states or rollback on this host (rec_gen.h)
static inline void ram_touch(const uint8_t *p) { (void)p; }
#else
static inline void ram_touch(const uint8_t *p) { ram_dirty[(size_t)(p - ram) >> 12] = 1; }
#endif

// Returns a host pointer for plain memory, or NULL for I/O space.
static inline uint8_t *mem_ptr(uint32_t pa)
{
    if (pa < RAM0_SIZE) return &ram0[pa];
    if (pa - 0x08000000u < RAM2_SIZE) return &ram2[pa - 0x08000000u];
    if (pa - 0x1fc00000u < ROM_SIZE) return &rom[pa - 0x1fc00000u];
    return 0;
}

// A value of `size` bytes at p in guest memory, in either direction
// (little-endian whatever the host; size is a constant at every use).
static inline uint64_t mem_ld(const uint8_t *p, int size)
{
    switch (size) {
    case 1: return *p;
    case 2: return ld_le16(p);
    case 4: return ld_le32(p);
    default: return ld_le64(p);
    }
}
static inline void mem_st(uint8_t *p, uint64_t v, int size)
{
    switch (size) {
    case 1: *p = (uint8_t)v; break;
    case 2: st_le16(p, (uint16_t)v); break;
    case 4: st_le32(p, (uint32_t)v); break;
    default: st_le64(p, v); break;
    }
}

uint32_t io_read(uint32_t pa, int size);
void     io_write(uint32_t pa, uint32_t val, int size);

// Hardware state shared with the frontend
typedef struct {
    uint32_t vram_control, sound_reset, sound_control, sound_data, coin_control;
    uint32_t p1, p2, volume, dsw;   // active-low inputs
} hw_t;
extern hw_t hw;

const uint16_t *video_base(void);   // 320x240, 640-byte stride, BGR555 words in guest (little-endian) order: read with ld_le16
