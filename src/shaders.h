// libretro GLSL shader presets (.glslp): multi-pass chains, lookup textures,
// frame history and adjustable #pragma parameters, as used by RetroArch.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char name[64], desc[96];
    float def, min, max, step, value;
} shader_param_t;

// Load a preset, replacing the current one. On failure the old one stays
// loaded and `err` says why.
bool shader_load(const char *path, char *err, size_t errlen);
void shader_unload(void);
bool shader_active(void);
const char *shader_path(void);

// Run the chain on this 320x240 0x00RRGGBB frame into `viewport` of the
// window's default framebuffer (`drawable_h` high).
void shader_render(const uint32_t *px, SDL_Rect viewport, int drawable_h);

int shader_param_count(void);
shader_param_t *shader_param(int i);
// "name=value;..." for the values that differ from the defaults, and back.
void shader_params_save(char *buf, size_t n);
void shader_params_apply(const char *s);

// All .glslp files under `dir`, as paths relative to it (sorted).
int shader_list(const char *dir, char names[][192], int max);
