// Static translator for the sound DSP's code (ADSP-2105 to C). Runs on the
// player's machine at first launch, beside recomp.c, on program images taken
// from their own sound ROMs. The output runs exactly like the interpreter.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t base, words;       // program memory address and length
    const uint32_t *image;      // the 24-bit words loaded there
} adsprec_image_t;

#define ADSPREC_FILES 6         // rec_dsp_0.c ... rec_dsp_5.c

// Writes rec_dsp.c (the table) and the ADSPREC_FILES code files into outdir.
// No images: a table with none, so the module still links.
bool adsprec_generate(const adsprec_image_t *img, int n, const char *outdir, char *err, size_t errlen);
