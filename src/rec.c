// Recompiled code dispatch: maps entry addresses to generated functions and
// checks that the game's code in RAM still matches the recompiled image.
#include "rec.h"
#include "game.h"
#include "adsp.h"
#include <stdio.h>
#include <stdlib.h>

uint32_t rec_code_lo, rec_code_size, rec_code_gen = 1;
bool     rec_enabled;
rec_fn_t *rec_map;
uint32_t rec_map_gen;
const rec_module_t *rec_mod;
uint32_t rec_image_base, rec_image_words;
static rec_host_t host;

static uint32_t *func_ok_gen;   // rec_code_gen at which each function last matched RAM
static uint32_t *func_bad_gen;  // ... or last failed to match
// Functions written since the map was last brought up to date. A function's
// range holds the data that follows its code too (the game keeps variables
// there, written every frame), so a store only rechecks the functions around it.
static uint8_t  *fn_dirty;
static uint32_t *dirty_list, ndirty;
static uint32_t *fn_lab;        // first label of each function (labels are sorted by pc)

const rec_host_t *rec_host_info(void)
{
    rec_host_t h = {
        REC_ABI_VERSION, sizeof(cpu_t), sizeof(tlbc_t), &cpu, ram, ram_dirty, tlbc,
        &rec_code_lo, &rec_code_size, &irq_dirty, &cpu_exc, &rec_deadline,
        rec_load, rec_store, rec_op,
        adsp_core(), (uint32_t)adsp_core_size(), adsp_pm_changed,
    };
    host = h;
    return &host;
}

static bool use(const rec_module_t *m)
{
    if (!m || !m->nfuncs || getenv("KI2_NOREC") || getenv("KI2_COVER") || cpu_trace) return false;
    if (m->abi != REC_ABI_VERSION || m->sizeof_cpu != sizeof(cpu_t) || m->sizeof_tlbc != sizeof(tlbc_t)) {
        fprintf(stderr, "rec: translated code is from another version, not used\n");
        return false;
    }
    if (strncmp(m->game, game.id, sizeof m->game)) return false;     // the other game's code
    rec_mod = m;
    rec_image_base = m->image_base;
    rec_image_words = m->image_words;
    rec_code_lo = rec_image_base & 0x1fffffffu;
    rec_code_size = rec_image_words * 4;
    free(rec_map); free(func_ok_gen); free(func_bad_gen); free(fn_dirty); free(dirty_list); free(fn_lab);
    rec_map = (rec_fn_t *)calloc(rec_image_words, sizeof *rec_map);
    func_ok_gen = (uint32_t *)calloc(m->nfuncs, sizeof *func_ok_gen);
    func_bad_gen = (uint32_t *)calloc(m->nfuncs, sizeof *func_bad_gen);
    fn_dirty = (uint8_t *)calloc(m->nfuncs, 1);
    dirty_list = (uint32_t *)calloc(m->nfuncs, sizeof *dirty_list);
    fn_lab = (uint32_t *)calloc(m->nfuncs + 1, sizeof *fn_lab);
    for (uint32_t f = 0, i = 0; f <= m->nfuncs; f++) {
        if (f < m->nfuncs) while (i < m->nlabels && m->labels[i].pc < m->funcs[f].lo) i++;
        else i = m->nlabels;
        fn_lab[f] = i;
    }
    ndirty = 0;
    rec_map_gen = 0;
    rec_enabled = true;
    fprintf(stderr, "rec: %u functions, %u entry points, %u KB of code\n",
            m->nfuncs, m->nlabels, rec_image_words * 4 / 1024);
    bool dsp = m->ndsp && m->sizeof_adsp == adsp_core_size();
    adsp_use_code(dsp ? m->dsp : NULL, dsp ? m->ndsp : 0);
    return true;
}

void rec_init(void) { use(rec_module_init(rec_host_info())); }

// The module itself stays loaded: little memory, and it is never called again.
void rec_unload(void) { rec_enabled = false; rec_mod = NULL; adsp_use_code(NULL, 0); }

bool rec_use_module(rec_module_init_fn init) { return init && use(init(rec_host_info())); }

static bool func_matches(uint32_t f)
{
    if (func_ok_gen[f] == rec_code_gen) return true;
    if (func_bad_gen[f] == rec_code_gen) return false;
    const rec_func_t *fn = &rec_mod->funcs[f];
    uint32_t off = fn->lo - rec_image_base, len = fn->hi - fn->lo;
#ifdef REC_BIG_ENDIAN
    // the image holds word values; RAM holds them little-endian
    const uint8_t *r = &ram2[rec_code_lo - 0x08000000u + off];
    const uint32_t *im = rec_mod->image + off / 4;
    bool ok = true;
    for (uint32_t k = 0; k < len / 4 && ok; k++) ok = ld_le32(r + 4 * k) == im[k];
#else
    bool ok = memcmp(&ram2[rec_code_lo - 0x08000000u + off], (const uint8_t *)rec_mod->image + off, len) == 0;
#endif
    (ok ? func_ok_gen : func_bad_gen)[f] = rec_code_gen;
    return ok;
}

static uint64_t last_rebuild;

// As at program start (machine power-on): identical on every machine.
void rec_reset(void)
{
    rec_code_gen = 1;
    last_rebuild = 0;
    rec_forget();
}

// Forget everything learned about RAM contents (after loading a state file).
void rec_forget(void)
{
    if (!rec_enabled) return;
    memset(func_ok_gen, 0, rec_mod->nfuncs * sizeof *func_ok_gen);
    memset(func_bad_gen, 0, rec_mod->nfuncs * sizeof *func_bad_gen);
    memset(fn_dirty, 0, rec_mod->nfuncs);
    ndirty = 0;
    rec_map_gen = 0;           // generations start at 1, so this never matches
}

static void rebuild(void)
{
    memset(rec_map, 0, rec_image_words * sizeof *rec_map);
    for (uint32_t i = 0; i < rec_mod->nlabels; i++) {
        const rec_label_t *l = &rec_mod->labels[i];
        if (func_matches(l->fn)) rec_map[(l->pc - rec_image_base) >> 2] = rec_mod->funcs[l->fn].fn;
    }
    memset(fn_dirty, 0, rec_mod->nfuncs);
    ndirty = 0;
    last_rebuild = cpu.cycles;
    rec_map_gen = rec_code_gen;
}

static void mark_dirty(uint32_t f)
{
    if (!fn_dirty[f]) { fn_dirty[f] = 1; dirty_list[ndirty++] = f; }
}

void rec_note_write(uint32_t pa)
{
    if (!rec_mod) return;
    uint32_t va = pa - rec_code_lo + rec_image_base;
    const rec_func_t *fn = rec_mod->funcs;
    uint32_t a = 0, b = rec_mod->nfuncs;          // the last function starting at or before va
    while (b - a > 1) { uint32_t m = (a + b) / 2; if (fn[m].lo <= va) a = m; else b = m; }
    if (va >= fn[a].lo && va < fn[a].hi) mark_dirty(a);
    if (a && va < fn[a - 1].hi) mark_dirty(a - 1);  // a range ends one word into the next (its delay slot)
}

// Code was written since the map was built: recheck the functions written to
// (every word of each against the image) and update only their entries. The
// rest of the map is unchanged, because nothing else was written.
rec_fn_t rec_lookup_slow(uint32_t pc)
{
    if (!rec_map_gen) { rebuild(); return rec_map[(pc - rec_image_base) >> 2]; }
    for (uint32_t k = 0; k < ndirty; k++) {
        uint32_t f = dirty_list[k];
        fn_dirty[f] = 0;
        rec_fn_t fp = func_matches(f) ? rec_mod->funcs[f].fn : 0;
        for (uint32_t i = fn_lab[f]; i < fn_lab[f + 1]; i++)
            rec_map[(rec_mod->labels[i].pc - rec_image_base) >> 2] = fp;
    }
    ndirty = 0;
    rec_map_gen = rec_code_gen;
    return rec_map[(pc - rec_image_base) >> 2];
}

// Which code runs translated must not change across a state load, or the
// replay would take a different (interpreted vs compiled) path. RAM is
// restored first, so rebuilding from it reproduces the saved map exactly.
size_t rec_state(uint8_t *buf, bool load)
{
    struct { uint32_t gen; bool map_valid; uint64_t last; } s;
    memset(&s, 0, sizeof s);     // padding too: states and netplay hashes compare bytes
    if (buf && load) {
        memcpy(&s, buf, sizeof s);
        if (rec_enabled) {
            rec_code_gen = s.gen;
            last_rebuild = s.last;
            if (s.map_valid) { if (rec_map_gen != rec_code_gen) rebuild(); }
            else rec_map_gen = 0;      // rebuilt whole: same map as rechecking what was written
            memset(fn_dirty, 0, rec_mod->nfuncs);
            ndirty = 0;
        }
    } else if (buf) {
        s.gen = rec_code_gen; s.map_valid = rec_map_gen == rec_code_gen; s.last = last_rebuild;
        memcpy(buf, &s, sizeof s);
    }
    return sizeof s;
}
