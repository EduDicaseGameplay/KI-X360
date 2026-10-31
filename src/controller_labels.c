#include "controller_labels.h"
#include "config.h"
#include <stdio.h>

int pad_family_for_type(SDL_GameControllerType type)
{
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360: case SDL_CONTROLLER_TYPE_XBOXONE:
        return PAD_FAMILY_XBOX;
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5:
        return PAD_FAMILY_PLAYSTATION;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return PAD_FAMILY_SWITCH;
    default: return PAD_FAMILY_XBOX;
    }
}

int pad_label_family(SDL_GameController *pad, int override)
{
    if (override > PAD_FAMILY_AUTO && override < PAD_FAMILY_COUNT) return override;
    return pad_family_for_type(pad ? SDL_GameControllerGetType(pad) : SDL_CONTROLLER_TYPE_UNKNOWN);
}

const char *pad_family_name(int family)
{
    static const char *const names[] = {"Auto", "Xbox", "PlayStation", "Switch", "Positions"};
    return family >= 0 && family < PAD_FAMILY_COUNT ? names[family] : names[0];
}

const char *pad_button_name(SDL_GameController *pad, int override, int button)
{
    int family = pad_label_family(pad, override);
    SDL_GameControllerType type = pad ? SDL_GameControllerGetType(pad) : SDL_CONTROLLER_TYPE_UNKNOWN;
    if (button == PAD_NONE) return "-";
    if (button >= SDL_CONTROLLER_BUTTON_A && button <= SDL_CONTROLLER_BUTTON_Y) {
        static const char *const xbox[] = {"A", "B", "X", "Y"};
        static const char *const ps[] = {"Cross", "Circle", "Square", "Triangle"};
        static const char *const sw[] = {"B", "A", "Y", "X"};
        static const char *const generic[] = {"South", "East", "West", "North"};
        // Normalise the source mapping to a physical position before applying
        // any label override, including Positions. Do not change the binding.
        bool by_label = pad_family_for_type(type) == PAD_FAMILY_SWITCH &&
            SDL_GetHintBoolean(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, SDL_TRUE);
        if (by_label) button ^= 1;   // A/B and X/Y swap on Nintendo layouts
        if (family == PAD_FAMILY_PLAYSTATION) return ps[button];
        if (family == PAD_FAMILY_XBOX) return xbox[button];
        if (family == PAD_FAMILY_SWITCH) return sw[button];
        return generic[button];
    }
    switch (button) {
    case PAD_LT: return family == PAD_FAMILY_PLAYSTATION ? "L2" : family == PAD_FAMILY_SWITCH ? "ZL" : "LT";
    case PAD_RT: return family == PAD_FAMILY_PLAYSTATION ? "R2" : family == PAD_FAMILY_SWITCH ? "ZR" : "RT";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return family == PAD_FAMILY_PLAYSTATION ? "L1" : family == PAD_FAMILY_SWITCH ? "L" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return family == PAD_FAMILY_PLAYSTATION ? "R1" : family == PAD_FAMILY_SWITCH ? "R" : "RB";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return family == PAD_FAMILY_PLAYSTATION ? "L3" : "LS";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return family == PAD_FAMILY_PLAYSTATION ? "R3" : "RS";
    case SDL_CONTROLLER_BUTTON_BACK:
        if (family == PAD_FAMILY_SWITCH) return "-";
        if (family == PAD_FAMILY_PLAYSTATION)
            return type == SDL_CONTROLLER_TYPE_PS5 ? "Create" : type == SDL_CONTROLLER_TYPE_PS4 ? "Share" : "Select";
        return type == SDL_CONTROLLER_TYPE_XBOXONE && family == PAD_FAMILY_XBOX ? "View" : "Back";
    case SDL_CONTROLLER_BUTTON_START:
        if (family == PAD_FAMILY_SWITCH) return "+";
        if (family == PAD_FAMILY_PLAYSTATION) return type == SDL_CONTROLLER_TYPE_PS3 ? "Start" : "Options";
        return type == SDL_CONTROLLER_TYPE_XBOXONE && family == PAD_FAMILY_XBOX ? "Menu" : "Start";
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return family == PAD_FAMILY_PLAYSTATION ? "PS" : family == PAD_FAMILY_SWITCH ? "Home" : "Guide";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "Up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "Down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "Left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "Right";
    default: {
        const char *name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)button);
        return name ? name : "Button";
    }
    }
}

void pad_prompt_init(pad_prompt_t *prompt)
{
    prompt->last_pad = -1;
    prompt->using_pad = false;
}

void pad_prompt_event(pad_prompt_t *prompt, const SDL_Event *event)
{
    SDL_JoystickID id;
    switch (event->type) {
    case SDL_KEYDOWN: case SDL_MOUSEBUTTONDOWN:
        prompt->using_pad = false;
        return;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (prompt->last_pad == event->cdevice.which) pad_prompt_init(prompt);
        return;
    case SDL_CONTROLLERBUTTONDOWN: id = event->cbutton.which; break;
    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            if (event->caxis.value <= 12000) return;
        } else if (event->caxis.value >= -16000 && event->caxis.value <= 16000) return;
        id = event->caxis.which;
        break;
    default: return;
    }
    if (SDL_GameControllerFromInstanceID(id)) {
        prompt->last_pad = id;
        prompt->using_pad = true;
    }
}

void pad_prompt_footer(const pad_prompt_t *prompt, int override,
                       const char *select, const char *back, char *out, size_t n)
{
    SDL_GameController *pad = SDL_GameControllerFromInstanceID(prompt->last_pad);
    const char *a = "ENTER", *b = "ESC";
    if (prompt->using_pad && pad) {
        a = pad_button_name(pad, override, SDL_CONTROLLER_BUTTON_A);
        b = pad_button_name(pad, override, SDL_CONTROLLER_BUTTON_B);
    }
    snprintf(out, n, "%s %s   %s %s", a, select, b, back);
}

void pad_prompt_adjust_footer(const pad_prompt_t *prompt, int override, char *out, size_t n)
{
    SDL_GameController *pad = SDL_GameControllerFromInstanceID(prompt->last_pad);
    const char *back = prompt->using_pad && pad ?
        pad_button_name(pad, override, SDL_CONTROLLER_BUTTON_B) : "ESC";
    snprintf(out, n, "LEFT/RIGHT CHANGE   %s BACK", back);
}
