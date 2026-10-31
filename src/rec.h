// Statically recompiled MIPS code: runtime interface (program side).
//
// The recompiler (recomp.c) translates the game's RAM-resident code, from a
// snapshot, into C functions, one per MIPS function, with a switch on cpu.pc
// at the top so any labelled address inside can be entered. cpu_run calls
// into them whenever cpu.pc hits a label and falls back to the interpreter
// otherwise. A translated function is used only while the bytes in RAM still
// match the snapshot it was built from.
//
// The code is either linked in (developer builds) or a module built on the
// player's machine (rec_build.c); either way it is reached through a
// rec_module_t.
#pragma once
#include "cpu.h"
#include "mem.h"
#include <string.h>

extern const rec_module_t *rec_mod;          // NULL: interpreter only
extern uint32_t rec_image_base, rec_image_words;

// Physical range covered by the image; any store into it bumps rec_code_gen.
extern uint32_t rec_code_lo, rec_code_size, rec_code_gen;
extern bool     rec_enabled;

// Linked-in code (build/rec/rec_tab.c, or src/rec_none.c which returns NULL).
const rec_module_t *rec_module_init(const rec_host_t *host);

void     rec_init(void);                              // use the linked-in code, if any
bool     rec_use_module(rec_module_init_fn init);     // use a module built at run time
const rec_host_t *rec_host_info(void);
rec_fn_t rec_lookup_slow(uint32_t pc);
void     rec_note_write(uint32_t pa);                 // a store into the image (pa inside it): recheck its functions
size_t   rec_state(uint8_t *buf, bool load);
void     rec_forget(void);
void     rec_unload(void);     // stop using the translated code (the game's code changed)
void     rec_reset(void);
extern rec_fn_t *rec_map;          // per-word entry, valid for the current rec_code_gen
extern uint32_t rec_map_gen;

static inline rec_fn_t rec_lookup(uint32_t pc)
{
    uint32_t i = (pc - rec_image_base) >> 2;
    if (i >= rec_image_words || (pc & 3)) return 0;
    if (rec_map_gen != rec_code_gen) return rec_lookup_slow(pc);
    return rec_map[i];
}

// Used by translated code (through rec_host_t when it is a module)
extern bool     cpu_exc;           // set by a helper when it raised an exception
extern bool     irq_dirty;
extern uint64_t rec_deadline;      // return to the dispatcher at a back-edge past this cycle
extern uint64_t rec_slice_end;     // the end of the current slice (rec_gen.h CALL recomputes rec_deadline from it)
uint64_t rec_load(uint32_t va, int size, uint32_t pc, bool delay);
void     rec_store(uint32_t va, uint64_t v, int size, uint32_t pc, bool delay);
bool     rec_op(uint32_t op, uint32_t pc, bool delay);   // interpret one non-branch op

extern tlbc_t tlbc[TLBC_SIZE];
