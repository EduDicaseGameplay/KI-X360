// Operator adjustments on disk (see adjust.h; the layout is in game.h). The
// difficulty is stored as 0-4.
#include "adjust.h"
#include "game.h"
#include "ide.h"
#include "rec_abi.h"     // ld_le32, st_le32
#include <stdint.h>
#include <string.h>

// The game's checksum: a 33-bit rotate/xor shift register fed with each byte.
static uint32_t ki2_checksum(const uint8_t *p, int n)
{
    uint64_t t = 1;
    for (int i = 0; i < n; i++) {
        uint64_t x = ((t & 1) << 32) | ((t >> 1) & 0xffffffffu);
        x ^= (t << 12) & 0xffffffffu;
        t = x ^ ((x >> 20) & 0xfff) ^ p[i];
    }
    return (uint32_t)t;
}

#define HEADER_SUM 0x1f8

static uint32_t get32(const uint8_t *p) { return ld_le32(p); }     // disk data is little-endian
static void put32(uint8_t *p, uint32_t v) { st_le32(p, v); ide_dirty[(p - ide_tracked()) >> 12] = 1; }

static bool block_valid(const uint8_t *d)
{
    uint32_t sum = ki2_checksum(d + game.adj_data, (int)game.adj_len);
    if (!game.adj_headers[0]) return get32(d + game.adj_sum) == sum;
    uint32_t h = game.adj_headers[0];
    return get32(d + h + 4) == sum && get32(d + h + HEADER_SUM) == ki2_checksum(d + h, HEADER_SUM);
}

int adjust_difficulty(void)
{
    const uint8_t *d = ide_tracked();
    if (!game.adj_len || !d || !block_valid(d) || d[game.adj_difficulty] > 4) return -1;
    return d[game.adj_difficulty] + 1;
}

// As the operator menu does it: the value, the edited flag, the checksums
// (and on KI1 the headers' generation counts).
bool adjust_set_difficulty(int level)
{
    uint8_t *d = ide_tracked();
    if (!game.adj_len || !d || level < 1 || level > 5 || !block_valid(d)) return false;
    if (d[game.adj_difficulty] == level - 1) return true;
    d[game.adj_difficulty] = (uint8_t)(level - 1);
    d[game.adj_data] = 1;
    ide_dirty[game.adj_difficulty >> 12] = ide_dirty[game.adj_data >> 12] = 1;   // states and rewind see it
    uint32_t sum = ki2_checksum(d + game.adj_data, (int)game.adj_len);
    if (!game.adj_headers[0]) put32(d + game.adj_sum, sum);
    for (int i = 0; i < 2 && game.adj_headers[i]; i++) {
        uint8_t *h = d + game.adj_headers[i];
        put32(h + 4, sum);
        put32(h, get32(h) + 1);
        put32(h + 0x1fc, get32(h + 0x1fc) + 1);
        put32(h + HEADER_SUM, ki2_checksum(h, HEADER_SUM));
    }
    ide_tracked_changed();               // and it gets saved with the settings
    return true;
}
