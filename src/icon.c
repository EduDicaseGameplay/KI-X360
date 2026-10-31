// Program icon: the profile's text ("UC2", "UC") on a dark red rounded tile
// with three claw slashes behind it, built from the OSD font (no game art).
#include "icon.h"
#include "osd.h"
#include "png.h"
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t argb(float a, float r, float g, float b)
{
    return (uint32_t)(a * 255) << 24 | (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
}

void icon_pixels(uint32_t *px, int size) { icon_pixels_text(px, size, game.icon_text ? game.icon_text : "UC"); }

void icon_pixels_text(uint32_t *px, int size, const char *text)
{
    // tile: vertical gradient, rounded corners
    float rad = size * 0.18f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float fx = (x + 0.5f) / size, fy = (y + 0.5f) / size;
            float cx = x + 0.5f < rad ? rad : x + 0.5f > size - rad ? size - rad : x + 0.5f;
            float cy = y + 0.5f < rad ? rad : y + 0.5f > size - rad ? size - rad : y + 0.5f;
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            float a = d > rad ? 0 : d > rad - 1 ? rad - d : 1;
            float r = 0.42f - 0.32f * fy, g = 0.035f + 0.01f * fy, b = 0.025f + 0.015f * fy;
            // claw slashes from top right to bottom left, tapered at both ends
            if (size > 16)
                for (int i = 0; i < 3; i++) {
                    float along = fy, across = fx + 0.62f * fy - (0.70f + 0.17f * i);
                    float w = 0.048f * sinf(3.14159f * along);
                    float k = w > 0 ? 1 - fabsf(across) / w : 0;
                    if (k > 0) {
                        float m = k > 0.5f ? 1 : k * 2;          // soft edge
                        m *= 0.6f;
                        r += (0.95f - r) * m; g += (0.30f - g) * m; b += (0.10f - b) * m;
                    }
                }
            px[y * size + x] = argb(a, r, g, b);
        }
    // 5x7 glyphs, scaled to fit with a dark outline; a final digit glows
    int n = (int)strlen(text);
    int units = 6 * n - 1;                                           // glyph columns plus gaps
    int scale = (int)(size * 0.82f / units);                         // text ~80% wide...
    if (7 * scale > size * 0.55f) scale = (int)(size * 0.55f / 7);   // ...and not too tall
    if (scale < 1) scale = 1;
    int gw = 5 * scale, gap = size <= 16 ? 0 : scale, tw = n * gw + (n - 1) * gap, th = 7 * scale;
    int x0 = (size - tw) / 2, y0 = (size - th) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < n; c++) {
            const uint8_t *gl = osd_glyph(text[c]);
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++) {
                    if (!(gl[col] >> row & 1)) continue;
                    for (int dy = -1; dy <= scale; dy++)
                        for (int dx = -1; dx <= scale; dx++) {
                            bool inside = dx >= 0 && dy >= 0 && dx < scale && dy < scale;
                            if (pass == 0 && inside) continue;        // outline first
                            if (pass == 1 && !inside) continue;
                            int x = x0 + c * (gw + gap) + col * scale + dx, y = y0 + row * scale + dy;
                            if (x < 0 || y < 0 || x >= size || y >= size) continue;
                            float t = (float)(row * scale + dy) / (float)th;
                            bool hot = c == n - 1 && text[c] >= '0' && text[c] <= '9';
                            uint32_t fill = !hot ? argb(1, 1, 0.88f - 0.38f * t, 0.38f - 0.32f * t)     // gold
                                                  : argb(1, 1, 0.97f - 0.62f * t, 0.85f - 0.80f * t);    // white-hot to orange
                            px[y * size + x] = pass ? fill : 0xff1a0000u;
                        }
                }
        }
}

void set_window_icon(SDL_Window *win)
{
    static uint32_t px[64 * 64];
    icon_pixels(px, 64);
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(px, 64, 64, 32, 64 * 4, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); }
}

static void le16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc(v >> 8 & 0xff, f); }
static void le32(FILE *f, unsigned v) { le16(f, v & 0xffff); le16(f, v >> 16); }

bool icon_write_ico(const char *path)
{
    static const int sizes[] = { 16, 32, 48, 64 };
    const int n = 4;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    le16(f, 0); le16(f, 1); le16(f, n);
    unsigned offset = 6 + 16 * n;
    for (int i = 0; i < n; i++) {
        int s = sizes[i];
        unsigned mask = (unsigned)((s + 31) / 32 * 4 * s), bytes = 40 + (unsigned)(s * s * 4) + mask;
        fputc(s, f); fputc(s, f); fputc(0, f); fputc(0, f);
        le16(f, 1); le16(f, 32); le32(f, bytes); le32(f, offset);
        offset += bytes;
    }
    static uint32_t px[64 * 64];
    for (int i = 0; i < n; i++) {
        int s = sizes[i];
        icon_pixels(px, s);
        le32(f, 40); le32(f, (unsigned)s); le32(f, (unsigned)(2 * s)); le16(f, 1); le16(f, 32);
        le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0);
        for (int y = s - 1; y >= 0; y--)                 // bottom-up BGRA
            for (int x = 0; x < s; x++) le32(f, px[y * s + x]);
        for (int k = 0; k < (s + 31) / 32 * 4 * s; k++) fputc(0, f);   // AND mask: alpha does the work
    }
    return fclose(f) == 0;
}

bool icon_write_png(const char *path, int size)
{
    static uint32_t px[256 * 256];
    if (size > 256) return false;
    icon_pixels(px, size);
    return png_write_argb(path, px, size, size);
}
