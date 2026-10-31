#pragma once
#include <stdint.h>
#include <string.h>

// All drawing goes into a 320x240 0xAARRGGBB overlay.
void osd_clear(uint32_t *ov);
void osd_text(uint32_t *ov, int x, int y, const char *s, uint32_t rgb);   // 5x7 font, 6 px per char
void osd_box(uint32_t *ov, int x, int y, int w, int h, uint32_t argb);
// Show a message at the bottom of the screen for two seconds.
void osd_message(const char *fmt, ...);
// Draw the current message, and `status` (e.g. "PAUSED") top right, into a 320x240 XRGB image.
void osd_draw(uint32_t *px, const char *status);
const uint8_t *osd_glyph(char c);    // 5 column bytes of the 5x7 font, bit 0 at the top
// Draw a menu panel; items are "label\tvalue".
void osd_menu(uint32_t *px, const char *title, const char *const *items, int n, int sel, const char *footer);
