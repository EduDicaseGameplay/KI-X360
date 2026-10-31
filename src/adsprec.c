// ADSP-2105 to C. Each program image becomes functions of up to PIECE
// words (compilers slow down sharply on bigger ones) in which every word is
// a label: jumps and calls to known addresses are gotos, anything else
// (returns, loop ends, interrupts, other pieces) goes through a switch on
// the pc or back to adsp_run.
// Every instruction does what one pass of adsp_run's loop does, in the same
// order and with the same functions from adsp_core.h, so translated and
// interpreted sound are bit-identical and either may stop at any instruction.
#include "adsprec.h"
#include "adsp_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIECE 128
#define MAX_PIECES 64

static const char *group_fn[256];

static void init_groups(void)
{
#define G(lo, hi, fn) for (int g = lo; g <= hi; g++) group_fn[g] = #fn;
    ADSP_GROUPS(G)
#undef G
}

static bool in_image(const adsprec_image_t *m, uint32_t pc) { return pc >= m->base && pc < (uint32_t)m->base + m->words; }

static void gen_image(FILE *f, const adsprec_image_t *m, int index)
{
    // DO UNTIL ends: where the loop goes back to (the word after the DO)
    uint16_t *loop_top = (uint16_t *)calloc(m->words, sizeof *loop_top);
    for (uint32_t k = 0; k < m->words; k++) {
        uint32_t op = m->image[k], g = op >> 16, end = (op >> 4) & 0x3fff;
        if (g >= 0x14 && g <= 0x17 && in_image(m, end)) loop_top[end - m->base] = (uint16_t)(m->base + k + 1);
    }

    fprintf(f, "int rec_dsp_%d_(int cyc, const uint8_t *ok, int *irq_done)\n{\n"
               "    adsp_irq_seen_t seen = { ~0ull, 0, 0, 0 };   // nothing yet (the lines are 0 or 1)\n"
               "    goto dispatch;\n", index);
    for (uint32_t k = 0; k < m->words; k++) {
        uint32_t pc = m->base + k, op = m->image[k], g = op >> 16;
        uint32_t next = (pc + 1) & 0x3fff;
        fprintf(f, "L_%04x: AT(0x%04x, %u)\nM_%04x: %s(0x%04x, 0x%04x) %s(0x%06x); TICK\n",
                pc, pc, k, pc, loop_top[k] ? "SEQ_END" : "SEQ", pc, next, group_fn[g], op);
        if (g == 0x02 || (g >= 0x18 && g <= 0x1b)) fprintf(f, "    if (A.idle | A.polling) goto out;\n");
        fprintf(f, "    ");
        if (in_image(m, next)) fprintf(f, "if (A.pc == 0x%04x) goto L_%04x; ", next, next);
        if (g >= 0x18 && g <= 0x1f) {                           // jump, call
            uint32_t t = (op >> 4) & 0x3fff;
            if (in_image(m, t) && t != next) fprintf(f, "if (A.pc == 0x%04x) goto L_%04x; ", t, t);
        }
        if (loop_top[k] && in_image(m, loop_top[k]) && loop_top[k] != next)
            fprintf(f, "if (A.pc == 0x%04x) goto L_%04x; ", loop_top[k], loop_top[k]);
        fprintf(f, "goto dispatch;\n");
    }
    fprintf(f, "dispatch:\n    switch (A.pc) {\n");
    for (uint32_t k = 0; k < m->words; k++) fprintf(f, "    case 0x%04x: goto L_%04x;\n", m->base + k, m->base + k);
    fprintf(f, "    }\n    goto out;\n");
    // just took an interrupt: carry on at the vector without checking again
    fprintf(f, "irq:\n    switch (A.pc) {\n");
    for (uint32_t k = 0; k < m->words; k++)
        fprintf(f, "    case 0x%04x: if (ok[%u]) goto M_%04x; break;\n", m->base + k, k, m->base + k);
    fprintf(f, "    }\n    *irq_done = 1;\nout:\n    return cyc;\n}\n\n");
    free(loop_top);
}

static const char prologue[] =
    "// Translated from the game's sound code by adsprec. Do not edit or share.\n"
    "#define ADSP_TRANSLATED\n"
    "#include \"rec_abi.h\"\n"
    "#include \"adsp_core.h\"\n"
    "#if defined(__GNUC__) && !defined(__TINYC__)\n"
    "#define DSP_HIDDEN __attribute__((visibility(\"hidden\")))\n"
    "#else\n"
    "#define DSP_HIDDEN\n"
    "#endif\n"
    "extern DSP_HIDDEN adsp_t *rec_dsp_s_;\n"
    "extern DSP_HIDDEN void (*rec_dsp_pm_written_)(uint16_t addr);\n"
    "#if defined(__GNUC__) && !defined(__TINYC__)\n"
    "// The pointer never changes while code runs: read through a const function,\n"
    "// the compiler keeps it in a register instead of reloading it after every\n"
    "// store (all the stores to DSP state may alias it otherwise).\n"
    "__attribute__((const, noinline)) static adsp_t *dsp_state_(void) { return rec_dsp_s_; }\n"
    "#define A (*dsp_state_())\n"
    "#else\n"
    "#define A (*rec_dsp_s_)\n"
    "#endif\n"
    "#define ADSP_PM_WRITTEN(addr) rec_dsp_pm_written_(addr)\n"
    "#include \"adsp_core.h\"\n\n";

static FILE *open_out(const char *outdir, const char *name, char *err, size_t errlen)
{
    char path[4200];
    snprintf(path, sizeof path, "%s/%s", outdir, name);
    FILE *f = fopen(path, "w");
    if (!f) snprintf(err, errlen, "cannot write %s", path);
    return f;
}

static bool close_out(FILE *f, const char *name, char *err, size_t errlen)
{
    if (fclose(f) == 0) return true;
    snprintf(err, errlen, "cannot write %s", name);
    return false;
}

bool adsprec_generate(const adsprec_image_t *img, int n, const char *outdir, char *err, size_t errlen)
{
    init_groups();
    // one file per image, so they compile in parallel; unused ones stay empty
    adsprec_image_t piece[MAX_PIECES];
    int np = 0;
    for (int i = 0; i < n; i++)
        for (uint32_t k = 0; k < img[i].words && np < MAX_PIECES; k += PIECE) {
            uint32_t len = img[i].words - k < PIECE ? img[i].words - k : PIECE;
            adsprec_image_t im = { (uint16_t)(img[i].base + k), (uint16_t)len, img[i].image + k };
            piece[np++] = im;
        }
    for (int i = 0; i < ADSPREC_FILES; i++) {
        char name[32];
        snprintf(name, sizeof name, "rec_dsp_%d.c", i);
        FILE *f = open_out(outdir, name, err, errlen);
        if (!f) return false;
        if (i < np) {
            fputs("#ifndef __TINYC__     // see rec_dsp.c\n", f);
            fputs(prologue, f);
            fprintf(f,
                "// the start of one pass of adsp_run's loop, then fetch and sequencing\n"
                "#define AT(at, k) if (cyc <= 0 || !ok[k]) goto out; \\\n"
                "    if (irq_raised() && !irq_seen(&seen)) { if (irq_step()) goto irq; irq_remember(&seen); }\n"
                "#define SEQ(at, next) if (A.loop_end != at) A.pc = next; else sequence_loop_end();\n"
                "#define SEQ_END(at, next) if (A.loop_end != at) A.pc = next; else loop_end_step();   // a DO UNTIL's end\n"
                "#define TICK clock_cycle(); cyc--;\n\n");
            for (int j = i; j < np; j += ADSPREC_FILES) gen_image(f, &piece[j], j);   // spread over the files
            fputs("#endif\n", f);
        } else fprintf(f, "// no sound program image %d\n", i);
        if (!close_out(f, name, err, errlen)) return false;
    }

    FILE *f = open_out(outdir, "rec_dsp.c", err, errlen);
    if (!f) return false;
    fputs(prologue, f);
    fprintf(f, "// TinyCC's code for these runs slower than the program's own interpreter\n"
               "// (every helper stays a call), so a TinyCC module offers none.\n"
               "#ifdef __TINYC__\n#define IMAGES 0\n#else\n#define IMAGES %d\n#endif\n\n", np);
    fprintf(f, "DSP_HIDDEN adsp_t *rec_dsp_s_;\nDSP_HIDDEN void (*rec_dsp_pm_written_)(uint16_t addr);\n\n");
    // declared extern first: a C++ build (the Xbox 360's) makes const objects file-local otherwise
    fprintf(f, "extern const rec_dsp_image_t rec_dsp_images_[];\nextern const uint32_t rec_dsp_nimages_, rec_dsp_sizeof_adsp_;\n\n");
    fprintf(f, "#if IMAGES\n");
    for (int i = 0; i < np; i++) {
        fprintf(f, "int rec_dsp_%d_(int cyc, const uint8_t *ok, int *irq_done);\n", i);
        fprintf(f, "static const uint32_t image_%d[] = {", i);
        for (uint32_t k = 0; k < piece[i].words; k++) fprintf(f, "%s0x%06x,", k % 8 ? " " : "\n    ", piece[i].image[k]);
        fprintf(f, "\n};\n");
    }
    fprintf(f, "\nconst rec_dsp_image_t rec_dsp_images_[] = {\n");
    for (int i = 0; i < np; i++) fprintf(f, "    { rec_dsp_%d_, 0x%04x, %u, image_%d },\n", i, piece[i].base, piece[i].words, i);
    fprintf(f, "};\n#else\nconst rec_dsp_image_t rec_dsp_images_[] = { { 0, 0, 0, 0 } };\n#endif\n");
    fprintf(f, "const uint32_t rec_dsp_nimages_ = IMAGES;\nconst uint32_t rec_dsp_sizeof_adsp_ = sizeof(adsp_t);\n\n"
               "void rec_dsp_bind_(const rec_host_t *h)\n{\n"
               "    rec_dsp_s_ = (adsp_t *)h->adsp;\n    rec_dsp_pm_written_ = h->dsp_pm_written;\n}\n");
    return close_out(f, "rec_dsp.c", err, errlen);
}
