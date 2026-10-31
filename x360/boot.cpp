// Start-up for the Xbox 360 frontend and the PC harness (boot.h). Mirrors
// main.c's file search: a launched MAME set first, then the root folder and
// run\ inside it.
#include "boot.h"
#include "machine.h"
#include "cpu.h"
#include "game.h"
#include "ide.h"
#include "launch.h"
#include "mem.h"
#include "adsp.h"
#include "rec.h"
#include "sound.h"
#include "state.h"
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>      // _mkdir
#include <string.h>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

static char root[512] = "game:\\";
static int version = -1;

void boot_set_root(const char *dir) { snprintf(root, sizeof root, "%s", dir); }
static int lang = LANG_EN;
void ui_set_lang(int l) { lang = l >= 0 && l < LANG_N ? l : LANG_EN; }
int ui_lang(void) { return lang; }
const char *tr(const char *en, const char *pt) { return lang == LANG_PT ? pt : en; }

static DWORD WINAPI unpack_thread(LPVOID unused)
{
    (void)unused;
    DWORD t0 = GetTickCount();
    while (ide_unpack_step()) {}
    fprintf(stderr, "disk decompressed in memory in %.1f s\n", (GetTickCount() - t0) / 1000.0);
    return 0;
}

static bool pristine;
void boot_set_pristine(bool on) { pristine = on; }

const char *base_dir(void) { return root; }
const char *data_dir(void) { return root; }
int machine_version(void) { return version; }

static bool readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void backslashes(char *p) { for (; *p; p++) if (*p == '/') *p = '\\'; }

const char *find_data(const char *name, char *buf, size_t n)
{
    for (int i = 0; launch_dir(i); i++) {
        snprintf(buf, n, "%s%s", launch_dir(i), name); backslashes(buf);
        if (readable(buf)) return buf;
    }
    snprintf(buf, n, "%s%s", root, name); backslashes(buf);
    if (readable(buf)) return buf;
    snprintf(buf, n, "%srun\\%s", root, name); backslashes(buf);
    return buf;
}

static bool load_file(const char *path, void *dst, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(dst, 1, size, f);
    fclose(f);
    return got == size;
}

// The disk: a raw image, else the CHD in the root or in MAME's <set>\ folder.
static const char *find_disk(char *buf, size_t n)
{
    const char *p = find_data(game.disk, buf, n);
    if (readable(p)) return p;
    p = find_data(game.chd, buf, n);
    if (readable(p)) return p;
    char sub[256];
    snprintf(sub, sizeof sub, "%s\\%s", game.set, game.chd);
    return find_data(sub, buf, n);
}

bool boot_machine(char *err, size_t n)
{
    // A MAME set in the root: which game it is, its ROMs unpacked into cache\.
    const game_t *g = NULL;
    char msg[512], path[600];
    for (int i = 0; game_profiles[i] && !g; i++) {
        snprintf(path, sizeof path, "%s%s.zip", root, game_profiles[i]->set);
        if (readable(path)) g = launch_open(path, msg, sizeof msg);
    }
    game_select(g ? g : &game_ki1);

    char rom_path[600];
    for (int v = 0; v < 4 && game.versions[v].label && version < 0; v++)
        if (readable(find_data(game.versions[v].rom, rom_path, sizeof rom_path))) version = v;
    if (version < 0) {
        snprintf(err, n, tr("No boot ROM found.\nPut %s.zip (the MAME set)\nbeside the program.",
                            "ROM de boot nao encontrada.\nColoque %s.zip (o set do MAME)\nao lado do programa."), game.set);
        return false;
    }
    find_data(game.versions[version].rom, rom_path, sizeof rom_path);
    if (!load_file(rom_path, rom, ROM_SIZE)) {
        snprintf(err, n, tr("Can't read the boot ROM\n%s", "Nao foi possivel ler a ROM de boot\n%s"), rom_path);
        return false;
    }

    char disk[600], settings[700];
    find_disk(disk, sizeof disk);
    snprintf(path, sizeof path, "%ssaves", root);
    _mkdir(path);
    const char *base = strrchr(rom_path, '\\');
    base = base ? base + 1 : rom_path;
    const char *dot = strchr(base, '.');
    snprintf(settings, sizeof settings, "%ssaves\\%.*s.settings", root, (int)(dot ? dot - base : (int)strlen(base)), base);
    if (!ide_open(disk, pristine ? NULL : settings, game.cyls, game.heads, game.secs, game.serial_offset)) {
        snprintf(err, n, tr("Can't open the disk image.\nPut %s\\%s (or %s)\nbeside the program.",
                            "Nao foi possivel abrir o disco.\nColoque %s\\%s (ou %s)\nao lado do programa."),
                 game.set, game.chd, game.disk);
        return false;
    }
    // Every seek on the console's USB stick or HDD is slow, and the game reads
    // its code and graphics in small pieces while it boots: take the whole
    // file (about 90 MB for KI's CHD) into memory once instead.
    fprintf(stderr, "disk image %s into memory\n", ide_preload() ? "read" : "could not be read");
    // Samples of the disk's contents: equal on every host when the CHD
    // decompresses correctly (compare with the PC harness's log).
    fprintf(stderr, "disk id %016llx\n", (unsigned long long)ide_image_id());
    // Then the whole CHD decompressed in the background, on a hardware thread
    // nothing else uses, so loads during the game no longer decompress.
    if (!getenv("KI2_NO_UNPACK") && ide_unpack_begin()) {
        HANDLE h = CreateThread(NULL, 256 * 1024, unpack_thread, NULL, CREATE_SUSPENDED, NULL);
#ifdef _XBOX
        XSetThreadProcessor(h, 1);              // core 0 beside the video thread (2: machine, 4/5: sound)
#endif
        ResumeThread(h);
    }

    char u10[600];
    find_data(game.sound_roms[0], u10, sizeof u10);
    char *slash = strrchr(u10, '\\');
    if (slash) *slash = 0;
    if (!sound_init(u10)) fprintf(stderr, "sound ROMs missing: running without sound\n");

    // DIP switches as the PC's defaults: high red blood, demo sounds,
    // finishing moves, free play.
    hw.dsw = (0xffffffffu & ~0x3e00u) | 0x3000u;
    cpu_reset();
    rec_init();             // linked-in translated code if this build has it, else the interpreter
    state_init();
    fprintf(stderr, "%s %s from %s, disk %s, %s\n", game.title, game.versions[version].label, rom_path, disk,
            rec_enabled ? "translated code" : "interpreter only");
    return true;
}

bool checkpoint_frame(unsigned long long f)
{
    // dense through the boot ROM and the first disk loads, then sparse
    return (f <= 120 && f % 5 == 0) || f == 1 || f == 180 || f == 240 || f == 300 || f == 450 || f == 600 ||
           (f % 900 == 0 && f <= 9000);
}

static uint64_t fnv(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; }
static uint64_t fnv_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) h = fnv(h, p[i]);
    return h;
}

void checkpoint_hashes(unsigned long long out[CK_PARTS])
{
    const uint64_t basis = 0xcbf29ce484222325ull;
    out[CK_RAM0] = fnv_bytes(basis, ram, RAM0_SIZE);
    out[CK_RAM2] = fnv_bytes(basis, ram + RAM0_SIZE, RAM2_SIZE);
    uint64_t h = basis;
    for (int r = 0; r < 32; r++) h = fnv(h, cpu.r[r]);
    h = fnv(h, cpu.hi); h = fnv(h, cpu.lo); h = fnv(h, cpu.pc); h = fnv(h, cpu.cycles);
    for (int r = 0; r < 32; r++) h = fnv(h, cpu.cp0[r]);
    for (int r = 0; r < 32; r++) h = fnv(h, cpu.fpr[r]);
    h = fnv(h, cpu.ext_irq);
    out[CK_CPU] = h;
    const adsp_t *a = (const adsp_t *)adsp_core();
    h = basis;
    for (int i = 0; i < 0x400; i++) h = fnv(h, a->ipm[i]);
    for (int i = 0; i < 0x200; i++) h = fnv(h, a->idm[i]);
    for (int i = 0; i < 8; i++) { h = fnv(h, a->i[i]); h = fnv(h, a->l[i]); h = fnv(h, (uint16_t)a->m[i]); }
    const regset_t *r = &a->r;
    uint64_t regs[] = { r->ax0, r->ax1, r->ay0, r->ay1, r->ar, r->af, r->mx0, r->mx1, r->my0, r->my1, r->mf,
                        r->si, (uint8_t)r->se, (uint8_t)r->sb, r->sr, (uint64_t)r->mr };
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) h = fnv(h, regs[i]);
    h = fnv(h, a->pc); h = fnv(h, a->cntr); h = fnv(h, a->astat); h = fnv(h, a->mstat); h = fnv(h, a->imask);
    h = fnv_bytes(h, a->irq_all, sizeof a->irq_all);
    out[CK_DSP] = h;
}

#include "reference.h"      // REFERENCE: { frame, ram0, ram2, cpu, dsp } from the PC harness

int checkpoint_check(unsigned long long f, const unsigned long long h[CK_PARTS])
{
    for (size_t i = 0; i < sizeof reference / sizeof reference[0]; i++) {
        if (reference[i][0] != f) continue;
        int diff = 0;
        for (int k = 0; k < CK_PARTS; k++) if (reference[i][1 + k] != h[k]) diff |= 1 << k;
        return diff;
    }
    return -1;
}
