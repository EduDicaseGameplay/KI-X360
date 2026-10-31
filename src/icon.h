// The program's icon: drawn in code (no game art), for the window and the Windows .exe.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

void icon_pixels(uint32_t *argb, int size);           // size x size, 0xAARRGGBB: this game's icon
void icon_pixels_text(uint32_t *argb, int size, const char *text);   // with this text
void set_window_icon(SDL_Window *win);
bool icon_write_ico(const char *path);               // 16, 32, 48 and 64 pixel images
bool icon_write_png(const char *path, int size);     // for Linux desktop files
