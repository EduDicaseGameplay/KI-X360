// Host-only labels. SDL bindings and emulated input bits never depend on them.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>

enum {
    PAD_FAMILY_AUTO, PAD_FAMILY_XBOX, PAD_FAMILY_PLAYSTATION,
    PAD_FAMILY_SWITCH, PAD_FAMILY_GENERIC, PAD_FAMILY_COUNT
};

typedef struct {
    SDL_JoystickID last_pad;
    bool using_pad;
} pad_prompt_t;

int pad_family_for_type(SDL_GameControllerType type);
int pad_label_family(SDL_GameController *pad, int override);
const char *pad_family_name(int family);
const char *pad_button_name(SDL_GameController *pad, int override, int button);
void pad_prompt_init(pad_prompt_t *prompt);
void pad_prompt_event(pad_prompt_t *prompt, const SDL_Event *event);
void pad_prompt_footer(const pad_prompt_t *prompt, int override,
                       const char *select, const char *back, char *out, size_t n);
void pad_prompt_adjust_footer(const pad_prompt_t *prompt, int override, char *out, size_t n);
