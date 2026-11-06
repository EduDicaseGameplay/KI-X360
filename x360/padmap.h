// Which pad button plays which arcade attack, per player, and the pad's state
// turned into the cabinet's buttons. Kept apart from the frontend so the PC
// can test it (pc/padmap_test.cpp). The includer provides XINPUT_GAMEPAD, the
// XINPUT_GAMEPAD_* masks, the BTN_* bits and <stdio.h>/<string.h>.
//
// Only attacks can be moved. Directions, Start, Back and the stick clicks stay
// where they are, so nobody can lose the way to the menu or out of the game.
#pragma once

// The triggers as two more button bits: wButtons has these two free.
enum { PAD_BIT_LT = 0x0400, PAD_BIT_RT = 0x0800 };

enum { PAD_INPUTS = 8, PAD_ATTACKS = 6, PAD_PLAYERS = 2 };

static const struct { WORD mask; const char *name; } pad_inputs[PAD_INPUTS] = {
    { XINPUT_GAMEPAD_A, "A" }, { XINPUT_GAMEPAD_B, "B" }, { XINPUT_GAMEPAD_X, "X" }, { XINPUT_GAMEPAD_Y, "Y" },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, "LB" }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB" },
    { PAD_BIT_LT, "LT" }, { PAD_BIT_RT, "RT" },
};

// In the order of the BTN_* bits: quick, medium, fierce punch, then kick.
static const char *const pad_attack_keys[PAD_ATTACKS] = {
    "quick_punch", "medium_punch", "fierce_punch", "quick_kick", "medium_kick", "fierce_kick",
};
static const signed char pad_defaults[PAD_ATTACKS] = { 2, 3, 5, 0, 1, 7 };   // X Y RB, A B RT

// Index into pad_inputs for each attack. Only the menu changes it, with the
// game paused; the emulation thread reads it.
static signed char pad_map[PAD_PLAYERS][PAD_ATTACKS] = { { 2, 3, 5, 0, 1, 7 }, { 2, 3, 5, 0, 1, 7 } };

static void padmap_reset(int player) { memcpy(pad_map[player], pad_defaults, PAD_ATTACKS); }

// Give an attack this input. An attack already on it takes the one being
// left, so all six always stay on a button of their own.
static void padmap_assign(int player, int attack, int input)
{
    signed char *m = pad_map[player];
    for (int a = 0; a < PAD_ATTACKS; a++)
        if (a != attack && m[a] == input) m[a] = m[attack];
    m[attack] = (signed char)input;
}

// One line of the settings file. True if it was ours.
static bool padmap_load_key(const char *key, int v)
{
    if (key[0] != 'p' || (key[1] != '1' && key[1] != '2') || key[2] != '_') return false;
    for (int a = 0; a < PAD_ATTACKS; a++)
        if (!strcmp(key + 3, pad_attack_keys[a])) {
            if (v >= 0 && v < PAD_INPUTS) pad_map[key[1] - '1'][a] = (signed char)v;
            return true;
        }
    return false;
}

// After loading: a file edited by hand may repeat a button. That player gets
// the defaults. True if any did.
static bool padmap_check(void)
{
    bool reset = false;
    for (int p = 0; p < PAD_PLAYERS; p++)
        for (int a = 0; a < PAD_ATTACKS; a++)
            for (int b = a + 1; b < PAD_ATTACKS; b++)
                if (pad_map[p][a] == pad_map[p][b]) { padmap_reset(p); reset = true; a = b = PAD_ATTACKS; }
    return reset;
}

// What the player chose, kept apart. Only the menu and the settings file may
// change the map; padmap_guard finds and undoes anything else that did.
static signed char pad_chosen[PAD_PLAYERS][PAD_ATTACKS] = { { 2, 3, 5, 0, 1, 7 }, { 2, 3, 5, 0, 1, 7 } };

static void padmap_chosen(void) { memcpy(pad_chosen, pad_map, sizeof pad_chosen); }

static bool padmap_guard(void)
{
    if (!memcmp(pad_chosen, pad_map, sizeof pad_chosen)) return true;
    memcpy(pad_map, pad_chosen, sizeof pad_chosen);
    return false;
}

// "p1: X Y RB A B RT  p2: ..." for the log.
static void padmap_text(char *out, size_t n)
{
    size_t len = 0;
    for (int p = 0; p < PAD_PLAYERS; p++) {
        len += (size_t)snprintf(out + len, n - len, "%sp%d:", p ? "  " : "", p + 1);
        for (int a = 0; a < PAD_ATTACKS; a++) {
            int i = pad_map[p][a];
            len += (size_t)snprintf(out + len, n - len, " %s", i >= 0 && i < PAD_INPUTS ? pad_inputs[i].name : "?");
        }
    }
}

static void padmap_save(FILE *f)
{
    fprintf(f, "; attack buttons: 0 = A, 1 = B, 2 = X, 3 = Y, 4 = LB, 5 = RB, 6 = LT, 7 = RT (each on a button of its own)\n");
    for (int p = 0; p < PAD_PLAYERS; p++)
        for (int a = 0; a < PAD_ATTACKS; a++)
            fprintf(f, "p%d_%s=%d\n", p + 1, pad_attack_keys[a], pad_map[p][a]);
}

// wButtons with the triggers as buttons.
static WORD pad_held(const XINPUT_GAMEPAD &g)
{
    WORD b = (WORD)(g.wButtons & ~(PAD_BIT_LT | PAD_BIT_RT));
    if (g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) b |= PAD_BIT_LT;
    if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) b |= PAD_BIT_RT;
    return b;
}

// The cabinet's buttons for this pad state (bits per BTN_*).
static uint32_t pad_translate(int player, const XINPUT_GAMEPAD &g, bool *test, bool *quit, bool *speed)
{
    WORD b = g.wButtons;
    if (b & XINPUT_GAMEPAD_LEFT_THUMB) *speed = true;
    uint32_t h = 0;
    const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    if ((b & XINPUT_GAMEPAD_DPAD_UP) || g.sThumbLY > dz) h |= 1u << BTN_UP;
    if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || g.sThumbLY < -dz) h |= 1u << BTN_DOWN;
    if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || g.sThumbLX < -dz) h |= 1u << BTN_LEFT;
    if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || g.sThumbLX > dz) h |= 1u << BTN_RIGHT;
    const WORD held = pad_held(g);
    for (int a = 0; a < PAD_ATTACKS; a++)
        if (held & pad_inputs[pad_map[player][a]].mask) h |= 1u << (BTN_HQ + a);
    bool back = (b & XINPUT_GAMEPAD_BACK) != 0;
    if (back && (b & XINPUT_GAMEPAD_LEFT_SHOULDER) && (b & XINPUT_GAMEPAD_RIGHT_SHOULDER)) { *test = true; return 0; }
    if (back && (b & XINPUT_GAMEPAD_START)) { *quit = true; return 0; }
    if (b & XINPUT_GAMEPAD_START) h |= 1u << BTN_START;
    if (back) h |= 1u << BTN_COIN;
    // the cabinet can't hold opposite directions at once
    if ((h >> BTN_UP & 1) && (h >> BTN_DOWN & 1)) h &= ~(3u << BTN_UP);
    if ((h >> BTN_LEFT & 1) && (h >> BTN_RIGHT & 1)) h &= ~(3u << BTN_LEFT);
    return h;
}
