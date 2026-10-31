// Definitions shared by the program and the translated game code.
//
// The translated code is either linked into the program (developer builds,
// `make recomp`) or built on the player's machine into a separate module
// (.so/.dll, or in memory with TinyCC) that the program loads. Modules are
// compiled with REC_MODULE defined and without the C library's headers, so
// this file stands on its own in that case.
#pragma once

#ifdef REC_MODULE
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef __SIZE_TYPE__ size_t;
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define bool _Bool
#define true 1
#define false 0
#endif
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN (-9223372036854775807LL - 1)
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// Guest memory (RAM, ROM, disk sectors) is kept in the board's byte order,
// little-endian, on every host. Multi-byte values in it are read and written
// through these, which are plain loads on a little-endian host and
// byte-reversed ones (lhbrx/lwbrx/ldbrx) on the Xbox 360.
#if defined(_XBOX) || defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define REC_BIG_ENDIAN 1
#endif

#if defined(REC_BIG_ENDIAN) && defined(_XBOX)
#include <ppcintrinsics.h>
#define ld_le16(p) ((uint16_t)__loadshortbytereverse(0, (p)))
#define ld_le32(p) ((uint32_t)__loadwordbytereverse(0, (p)))
#define ld_le64(p) ((uint64_t)__loaddoublewordbytereverse(0, (p)))
#define st_le16(p, v) __storeshortbytereverse((uint16_t)(v), 0, (p))
#define st_le32(p, v) __storewordbytereverse((uint32_t)(v), 0, (p))
#define st_le64(p, v) __storedoublewordbytereverse((uint64_t)(v), 0, (p))
#elif defined(REC_BIG_ENDIAN)
static inline uint16_t ld_le16(const void *q) { const uint8_t *p = (const uint8_t *)q; return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t ld_le32(const void *q) { const uint8_t *p = (const uint8_t *)q; return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint64_t ld_le64(const void *q) { const uint8_t *p = (const uint8_t *)q; return ld_le32(p) | (uint64_t)ld_le32(p + 4) << 32; }
static inline void st_le16(void *q, uint16_t v) { uint8_t *p = (uint8_t *)q; p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void st_le32(void *q, uint32_t v) { uint8_t *p = (uint8_t *)q; st_le16(p, (uint16_t)v); st_le16(p + 2, (uint16_t)(v >> 16)); }
static inline void st_le64(void *q, uint64_t v) { uint8_t *p = (uint8_t *)q; st_le32(p, (uint32_t)v); st_le32(p + 4, (uint32_t)(v >> 32)); }
#else
// Little-endian host: unaligned-safe plain loads.
#if defined(__GNUC__) && !defined(__TINYC__)
#define REC_MEMCPY_ __builtin_memcpy
#define REC_LE_LD_(T, p) T v; REC_MEMCPY_(&v, (p), sizeof v); return v;
#define REC_LE_ST_(p, v) REC_MEMCPY_((p), &(v), sizeof (v));
#else
#define REC_LE_LD_(T, p) return *(const T *)(p);
#define REC_LE_ST_(p, v) *(__typeof__(v) *)(p) = (v);
#endif
static inline uint16_t ld_le16(const void *p) { REC_LE_LD_(uint16_t, p) }
static inline uint32_t ld_le32(const void *p) { REC_LE_LD_(uint32_t, p) }
static inline uint64_t ld_le64(const void *p) { REC_LE_LD_(uint64_t, p) }
static inline void st_le16(void *p, uint16_t v) { REC_LE_ST_(p, v) }
static inline void st_le32(void *p, uint32_t v) { REC_LE_ST_(p, v) }
static inline void st_le64(void *p, uint64_t v) { REC_LE_ST_(p, v) }
#endif
// Host-order value <-> little-endian 16-bit word (disk data held in uint16_t arrays).
// A function, not a macro: callers pass expressions with side effects
// (ide.c's d.buf[d.buf_pos++]), which a macro would evaluate twice.
static inline uint16_t le16_host(uint16_t v)
{
#ifdef REC_BIG_ENDIAN
    return (uint16_t)(v >> 8 | v << 8);
#else
    return v;
#endif
}

// Bumped whenever anything below (or what modules expect of the program) changes.
#define REC_ABI_VERSION 3

#define RAM0_SIZE 0x80000u     // phys 0x00000000: 512 KB (holds the framebuffers)
#define RAM2_SIZE 0x800000u    // phys 0x08000000: 8 MB
#define ROM_SIZE  0x80000u     // phys 0x1fc00000: boot EPROM U98
#define RAM_SIZE  (RAM0_SIZE + RAM2_SIZE)
#define RAM_PAGES (RAM_SIZE >> 12)

typedef struct {
    uint64_t mask, hi, lo0, lo1;
} tlb_entry_t;

typedef struct {
    uint64_t r[32];
    uint64_t hi, lo;
    uint32_t pc;          // address of the instruction about to execute
    uint32_t npc;         // address of the one after it
    uint32_t cur_pc;      // address of the instruction currently executing
    bool     in_delay;    // instruction at pc is a branch delay slot
    bool     next_delay;  // set by a branch: instruction at npc is a delay slot

    uint64_t cp0[32];
    uint64_t fpr[32];
    uint32_t fcr31;
    bool     llbit;

    tlb_entry_t tlb[48];

    uint64_t cycles;      // total instructions executed
    uint32_t count_base;  // Count value at count_cyc; Count ticks at half the pipeline clock
    uint64_t count_cyc;
    uint64_t next_compare; // cycle at which Count reaches Compare
    uint32_t ext_irq;     // external interrupt lines (bit n = Int n)
} cpu_t;

#ifdef REC_MODULE
#define CP0_STATUS 12
#endif
#define SR_IE   0x00000001u
#define SR_EXL  0x00000002u
#define SR_ERL  0x00000004u
#define SR_KSU  0x00000018u
#define SR_BEV  0x00400000u
#define SR_FR   0x04000000u
#define SR_CU1  0x20000000u

// Cache of TLB-mapped 4 KB pages (cpu.c)
#define TLBC_SIZE 1024
typedef struct {
    uint32_t tag;       // virtual page number + 1, 0 = empty
    uint32_t pa;        // physical page
    bool     dirty;     // writes allowed
    uint8_t *rd, *wr;   // host page for loads / stores, NULL for I/O, ROM or code
} tlbc_t;

typedef void (*rec_fn_t)(void);

typedef struct {
    uint32_t pc;        // labelled entry address
    uint16_t fn;        // index into the module's functions
} rec_label_t;

typedef struct {
    rec_fn_t fn;
    uint32_t lo, hi;    // MIPS address range the C code was generated from
} rec_func_t;

// Translated sound DSP code (adsprec.c): a function per piece of a program
// image. Entered with the DSP's pc at one of its words, it runs until the
// cycles are used up or the pc leaves what it can run, and returns the
// cycles left. ok[k]: the word at base + k still holds image[k]. *irq_done
// is set when it stops just after taking an interrupt, before running the
// instruction there.
typedef int (*rec_dsp_fn_t)(int cycles, const uint8_t *ok, int *irq_done);

typedef struct rec_dsp_image_s {
    rec_dsp_fn_t fn;
    uint16_t base, words;               // program memory address and length
    const uint32_t *image;              // the 24-bit words it was translated from
} rec_dsp_image_t;

// What the program gives a module: where its state lives.
typedef struct {
    uint32_t abi, sizeof_cpu, sizeof_tlbc;
    cpu_t   *cpu;
    uint8_t *ram, *ram_dirty;
    tlbc_t  *tlbc;
    uint32_t *code_lo, *code_size;      // physical range of the translated code
    bool    *irq_dirty, *cpu_exc;
    uint64_t *deadline;                 // return to the dispatcher at a back-edge past this cycle
    uint64_t (*load)(uint32_t va, int size, uint32_t pc, bool delay);
    void     (*store)(uint32_t va, uint64_t v, int size, uint32_t pc, bool delay);
    bool     (*op)(uint32_t op, uint32_t pc, bool delay);
    void    *adsp;                      // the sound DSP's adsp_t (adsp_core.h)
    uint32_t sizeof_adsp;
    void     (*dsp_pm_written)(uint16_t addr);   // the DSP wrote its internal program RAM
} rec_host_t;

// What a module gives back.
typedef struct {
    uint32_t abi, sizeof_cpu, sizeof_tlbc;
    const rec_func_t  *funcs;
    const rec_label_t *labels;
    uint32_t nfuncs, nlabels;
    uint32_t image_base, image_words;
    const uint32_t *image;              // the game code it was translated from
    uint64_t image_hash;
    char game[8];                       // which game's code ("ki1", "ki2")
    const rec_dsp_image_t *dsp;         // translated sound code (may be none)
    uint32_t ndsp, sizeof_adsp;
} rec_module_t;

// Every module (and the program, for linked-in code) defines this.
#if defined(REC_MODULE) && defined(_WIN32)
#define REC_EXPORT __declspec(dllexport)
#elif defined(REC_MODULE) && defined(__GNUC__)
#define REC_EXPORT __attribute__((visibility("default")))
#else
#define REC_EXPORT
#endif
typedef const rec_module_t *(*rec_module_init_fn)(const rec_host_t *host);
#define REC_MODULE_INIT "rec_module_init"
