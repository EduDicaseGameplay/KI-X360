// Tests x360\padmap.h on the PC, without the SDK: with the default buttons the
// pad must give exactly what the frontend gave before buttons could be moved
// (old_bits below is that code), for every combination of buttons.
//   cl /nologo /EHsc padmap_test.cpp && padmap_test
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned short WORD;
typedef short SHORT;
typedef unsigned char BYTE;
struct XINPUT_GAMEPAD { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
enum {
    XINPUT_GAMEPAD_DPAD_UP = 0x0001, XINPUT_GAMEPAD_DPAD_DOWN = 0x0002, XINPUT_GAMEPAD_DPAD_LEFT = 0x0004,
    XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008, XINPUT_GAMEPAD_START = 0x0010, XINPUT_GAMEPAD_BACK = 0x0020,
    XINPUT_GAMEPAD_LEFT_THUMB = 0x0040, XINPUT_GAMEPAD_RIGHT_THUMB = 0x0080, XINPUT_GAMEPAD_LEFT_SHOULDER = 0x0100,
    XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200, XINPUT_GAMEPAD_A = 0x1000, XINPUT_GAMEPAD_B = 0x2000,
    XINPUT_GAMEPAD_X = 0x4000, XINPUT_GAMEPAD_Y = 0x8000,
    XINPUT_GAMEPAD_TRIGGER_THRESHOLD = 30, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE = 7849,
};
enum { BTN_HQ, BTN_HM, BTN_HF, BTN_LQ, BTN_LM, BTN_LF, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START, BTN_COIN };
#define snprintf _snprintf      // VS2010; x360_compat.h has its own on the console
#include "../padmap.h"

static uint32_t old_bits(const XINPUT_GAMEPAD &g, bool *test, bool *quit, bool *speed)
{
    WORD b = g.wButtons;
    if (b & XINPUT_GAMEPAD_LEFT_THUMB) *speed = true;
    uint32_t h = 0;
    const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    if ((b & XINPUT_GAMEPAD_DPAD_UP) || g.sThumbLY > dz) h |= 1u << BTN_UP;
    if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || g.sThumbLY < -dz) h |= 1u << BTN_DOWN;
    if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || g.sThumbLX < -dz) h |= 1u << BTN_LEFT;
    if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || g.sThumbLX > dz) h |= 1u << BTN_RIGHT;
    if (b & XINPUT_GAMEPAD_X) h |= 1u << BTN_HQ;
    if (b & XINPUT_GAMEPAD_Y) h |= 1u << BTN_HM;
    if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) h |= 1u << BTN_HF;
    if (b & XINPUT_GAMEPAD_A) h |= 1u << BTN_LQ;
    if (b & XINPUT_GAMEPAD_B) h |= 1u << BTN_LM;
    if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) h |= 1u << BTN_LF;
    bool back = (b & XINPUT_GAMEPAD_BACK) != 0;
    if (back && (b & XINPUT_GAMEPAD_LEFT_SHOULDER) && (b & XINPUT_GAMEPAD_RIGHT_SHOULDER)) { *test = true; return 0; }
    if (back && (b & XINPUT_GAMEPAD_START)) { *quit = true; return 0; }
    if (b & XINPUT_GAMEPAD_START) h |= 1u << BTN_START;
    if (back) h |= 1u << BTN_COIN;
    if ((h >> BTN_UP & 1) && (h >> BTN_DOWN & 1)) h &= ~(3u << BTN_UP);
    if ((h >> BTN_LEFT & 1) && (h >> BTN_RIGHT & 1)) h &= ~(3u << BTN_LEFT);
    return h;
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool all_different(int p)
{
    for (int a = 0; a < PAD_ATTACKS; a++)
        for (int b = a + 1; b < PAD_ATTACKS; b++)
            if (pad_map[p][a] == pad_map[p][b]) return false;
    return true;
}

static uint32_t press(int p, WORD buttons, BYTE lt, BYTE rt)
{
    XINPUT_GAMEPAD g;
    memset(&g, 0, sizeof g);
    g.wButtons = buttons; g.bLeftTrigger = lt; g.bRightTrigger = rt;
    bool t = false, q = false, s = false;
    return pad_translate(p, g, &t, &q, &s);
}

int main(void)
{
    // defaults: every button combination, triggers around the threshold, sticks in and out of the dead zone
    static const BYTE trig[] = { 0, 30, 31, 255 };
    static const SHORT stick[] = { 0, 7849, 7850, -7849, -7850 };
    unsigned long long cases = 0;
    for (int p = 0; p < PAD_PLAYERS; p++)
        for (int b = 0; b < 65536; b++)
            for (int l = 0; l < 4; l++)
                for (int r = 0; r < 4; r++)
                    for (int x = 0; x < 5; x += (b & 1023) ? 5 : 1)
                        for (int y = 0; y < 5; y += (b & 1023) ? 5 : 1) {
                            XINPUT_GAMEPAD g;
                            memset(&g, 0, sizeof g);
                            g.wButtons = (WORD)b; g.bLeftTrigger = trig[l]; g.bRightTrigger = trig[r];
                            g.sThumbLX = stick[x]; g.sThumbLY = stick[y];
                            bool t1 = false, q1 = false, s1 = false, t2 = false, q2 = false, s2 = false;
                            uint32_t n = pad_translate(p, g, &t1, &q1, &s1), o = old_bits(g, &t2, &q2, &s2);
                            if (n != o || t1 != t2 || q1 != q2 || s1 != s2) { if (fails++ < 5) printf("differs: buttons %04x\n", b); }
                            cases++;
                        }

    // moving a button: LB to quick punch; the old X is free, the others stay
    padmap_assign(0, 0, 4);
    CHECK(press(0, XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 0) == 1u << BTN_HQ);
    CHECK(press(0, XINPUT_GAMEPAD_X, 0, 0) == 0);
    CHECK(press(0, XINPUT_GAMEPAD_Y, 0, 0) == 1u << BTN_HM);
    CHECK(press(1, XINPUT_GAMEPAD_X, 0, 0) == 1u << BTN_HQ);      // player 2 keeps theirs
    CHECK(all_different(0));
    // onto a button in use: the two swap
    padmap_assign(0, 1, 7);                                      // medium punch to RT (fierce kick's)
    CHECK(press(0, 0, 0, 255) == 1u << BTN_HM);
    CHECK(press(0, XINPUT_GAMEPAD_Y, 0, 0) == 1u << BTN_LF);
    CHECK(all_different(0));
    // the same button again changes nothing
    padmap_assign(0, 1, 7);
    CHECK(press(0, 0, 0, 255) == 1u << BTN_HM);
    CHECK(all_different(0));
    // LT works as a button, and the menu's combos don't depend on the map
    padmap_assign(0, 2, 6);
    CHECK(press(0, 0, 255, 0) == 1u << BTN_HF);
    CHECK(press(0, XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START, 0, 0) == 0);
    CHECK(press(0, XINPUT_GAMEPAD_START, 0, 0) == 1u << BTN_START);
    // every assignment from every state keeps six different buttons
    for (int a = 0; a < PAD_ATTACKS; a++)
        for (int i = 0; i < PAD_INPUTS; i++) { padmap_assign(1, a, i); CHECK(pad_map[1][a] == i); CHECK(all_different(1)); }

    // the settings file: saved, read back, and bad lines
    signed char saved[PAD_PLAYERS][PAD_ATTACKS];
    memcpy(saved, pad_map, sizeof saved);
    FILE *f = tmpfile();
    padmap_save(f);
    padmap_reset(0); padmap_reset(1);
    rewind(f);
    char line[128];
    int loaded = 0;
    while (fgets(line, sizeof line, f)) {
        char key[32];
        int v;
        if (sscanf(line, "%31[^=]=%d", key, &v) == 2 && padmap_load_key(key, v)) loaded++;
    }
    fclose(f);
    padmap_check();
    CHECK(loaded == PAD_PLAYERS * PAD_ATTACKS);
    CHECK(!memcmp(saved, pad_map, sizeof saved));
    padmap_reset(0); padmap_reset(1);
    CHECK(!padmap_load_key("view", 1));
    CHECK(!padmap_load_key("p3_quick_punch", 1));
    CHECK(padmap_load_key("p1_quick_punch", 99));                 // out of range: ignored
    CHECK(pad_map[0][0] == pad_defaults[0]);
    CHECK(padmap_load_key("p1_quick_punch", 3));                  // same button as medium punch: defaults
    padmap_check();
    CHECK(!memcmp(pad_map[0], pad_defaults, PAD_ATTACKS));

    // a whole settings file as the console writes it, read the way the console reads it
    padmap_reset(0); padmap_reset(1);
    padmap_assign(0, 0, 0);                                       // A X RB Y B RT, as in the test on the console
    padmap_assign(0, 1, 2);
    padmap_assign(0, 3, 3);
    memcpy(saved, pad_map, sizeof saved);
    f = tmpfile();
    fprintf(f, "; KI-X360 settings (the menu writes this file: right stick click in game)\n");
    fprintf(f, "; view: 0 = 4:3, 1 = 4:3 with blurred sides, 2 = 16:9 full (stretched, nothing cut), 3 = 16:9 zoom (crops top and bottom)\n");
    fprintf(f, "view=2\n; filter: 0 = sharp pixels, 1 = smooth, 2 = CRT\nfilter=1\n");
    fprintf(f, "; scanlines and glow: 0-10; brightness 50-150, saturation 0-200 (percent)\nscanlines=6\nglow=0\nbrightness=100\nsaturation=100\n");
    fprintf(f, "; gamma in hundredths, 70-180: above 100 lifts the mid tones (TVs show the picture darker)\ngamma=130\nshow_speed=1\n");
    fprintf(f, "; sound volume in percent, 50-300: 100 is the arcade board's output as it is\nsound_volume=200\n");
    fprintf(f, "; language: 0 = English, 1 = Portugues (Brasil)\nlanguage=1\n");
    padmap_save(f);
    padmap_reset(0); padmap_reset(1);
    rewind(f);
    loaded = 0;
    while (fgets(line, sizeof line, f)) {
        char key[32];
        int v;
        if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
        if (padmap_load_key(key, v)) loaded++;
    }
    fclose(f);
    CHECK(!padmap_check());
    CHECK(loaded == PAD_PLAYERS * PAD_ATTACKS);
    CHECK(!memcmp(saved, pad_map, sizeof saved));

    // the guard: quiet while the map is what was chosen, puts it back otherwise
    padmap_chosen();
    CHECK(padmap_guard());
    pad_map[0][2] = 7;
    CHECK(!padmap_guard());
    CHECK(!memcmp(saved, pad_map, sizeof saved));
    CHECK(padmap_guard());
    char text[96];
    padmap_text(text, sizeof text);
    CHECK(!strcmp(text, "p1: A X RB Y B RT  p2: X Y RB A B RT"));

    printf("%llu pad states compared; %s\n", cases, fails ? "FAILED" : "all passed");
    return fails != 0;
}
