// Minimal PNG writer (RGB, uncompressed deflate): no zlib needed.
#include "png.h"
#include <stdio.h>
#include <string.h>

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, n, f);
    uint32_t c = crc(crc(0, (const uint8_t *)type, 4), data, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static bool write(const char *path, const uint32_t *px, int w, int h, bool alpha)
{
    const size_t bpp = alpha ? 4 : 3;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = alpha ? 6 : 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    // raw scanlines (filter byte 0 + RGB or RGBA), wrapped in stored deflate blocks
    size_t raw_n = (size_t)h * (1 + bpp * (size_t)w);
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    static uint8_t buf[2 + 640 * 480 * 4 + 64 * 5 + 4];
    if (z_n > sizeof buf) { fclose(f); return false; }
    uint8_t *raw = buf + sizeof buf - raw_n;     // build raw data at the end, then pack forward
    uint8_t *r = raw;
    for (int y = 0; y < h; y++) {
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t p = px[y * w + x];
            *r++ = p >> 16; *r++ = p >> 8; *r++ = p;
            if (alpha) *r++ = p >> 24;
        }
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint8_t *z = buf;
    *z++ = 0x78; *z++ = 0x01;
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *z++ = off + n == raw_n;
        *z++ = n & 0xff; *z++ = n >> 8; *z++ = ~n & 0xff; *z++ = (~n >> 8) & 0xff;
        memmove(z, raw + off, n);
        z += n;
    }
    be32(z, (b << 16) | a); z += 4;
    chunk(f, "IDAT", buf, (uint32_t)(z - buf));
    chunk(f, "IEND", 0, 0);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool png_write(const char *path, const uint32_t *xrgb, int w, int h) { return write(path, xrgb, w, h, false); }
bool png_write_argb(const char *path, const uint32_t *argb, int w, int h) { return write(path, argb, w, h, true); }
