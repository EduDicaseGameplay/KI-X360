// Static recompiler: translates the game's RAM-resident MIPS code into C
// (the C version of what tools/recomp.py did, so it can run on the player's
// machine at first launch).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int functions, labels, files;
    uint32_t code_end;          // end of the translated region
    uint64_t image_hash;        // identifies the translated code
} recomp_info_t;

// snap0/snap2: RAM snapshots (low 512 KB and the 8 MB bank) taken once the game has loaded its code. Writes
// rec_000.c ... and rec_tab.c into outdir.
bool recomp_generate(const uint8_t *snap0, const uint8_t *snap2, const char *outdir,
                     recomp_info_t *info, char *err, size_t errlen);
