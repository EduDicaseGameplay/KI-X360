// Helpers included by the C files the recompiler generates.
//
// Linked into the program, the names below (cpu, ram, tlbc, rec_load, ...)
// are the program's own. In a module (REC_MODULE) they reach the program's
// state through the rec_host_t it handed over; REC_LOCALS, at the top of
// every function, copies the hottest pointers into locals so the compiler
// can keep them in registers.
#pragma once

#ifdef REC_MODULE
#include "rec_abi.h"

extern const rec_host_t *rec_host_;
extern cpu_t *rec_cpu_;
extern uint8_t *rec_ram_, *rec_dirty_;

static void rec_bind_(const rec_host_t *h)
{
    rec_host_ = h;
    rec_cpu_ = h->cpu;
    rec_ram_ = h->ram;
    rec_dirty_ = h->ram_dirty;
}

#define REC_LOCALS cpu_t *const cpu_l_ = rec_cpu_; uint8_t *const ram_l_ = rec_ram_; \
                   uint8_t *const dirty_l_ = rec_dirty_; (void)cpu_l_; (void)ram_l_; (void)dirty_l_;
#define cpu (*cpu_l_)
#define ram ram_l_
#define ram_dirty dirty_l_
#define tlbc (rec_host_->tlbc)
#define rec_code_lo (*rec_host_->code_lo)
#define rec_code_size (*rec_host_->code_size)
#define irq_dirty (*rec_host_->irq_dirty)
#define cpu_exc (*rec_host_->cpu_exc)
#define rec_deadline (*rec_host_->deadline)
#define rec_load (*rec_host_->load)
#define rec_store (*rec_host_->store)
#define rec_op (*rec_host_->op)

#else
#include "rec.h"
static void rec_bind_(const rec_host_t *h) { (void)h; }
#define REC_LOCALS
#endif

#if defined(__TINYC__)
// TinyCC (one of the compilers that may build a module on the player's
// machine) has neither gcc's overflow builtins nor 128-bit integers.
static int rec_add_ov(int32_t a, int32_t b, int32_t *r) { int64_t s = (int64_t)a + b; *r = (int32_t)s; return s != *r; }
static int rec_sub_ov(int32_t a, int32_t b, int32_t *r) { int64_t s = (int64_t)a - b; *r = (int32_t)s; return s != *r; }
#define __builtin_add_overflow(a, b, r) rec_add_ov((a), (b), (r))
#define __builtin_sub_overflow(a, b, r) rec_sub_ov((a), (b), (r))
static void rec_mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo = (mid << 32) | (uint32_t)ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
#define REC_LOAD(T, v, p) ((v) = *(const T *)(p))
#define REC_STORE(T, p, v) (*(T *)(p) = (v))
#elif defined(REC_MODULE)
#define REC_LOAD(T, v, p) __builtin_memcpy(&(v), (p), sizeof(T))
#define REC_STORE(T, p, v) __builtin_memcpy((p), &(v), sizeof(T))
#else
#define REC_LOAD(T, v, p) memcpy(&(v), (p), sizeof(T))
#define REC_STORE(T, p, v) memcpy((p), &(v), sizeof(T))
#endif
#if defined(REC_BIG_ENDIAN)
// Guest memory is little-endian: byte-reversed loads and stores (rec_abi.h).
// sizeof(T) is a constant, so each use folds to one access.
#undef REC_LOAD
#undef REC_STORE
#define REC_LOAD(T, v, p) ((v) = (T)(sizeof(T) == 1 ? *(const uint8_t *)(p) :            \
        sizeof(T) == 2 ? (uint64_t)ld_le16(p) : sizeof(T) == 4 ? (uint64_t)ld_le32(p) : ld_le64(p)))
#define REC_STORE(T, p, v) (sizeof(T) == 1 ? (void)(*(uint8_t *)(p) = (uint8_t)(v)) :      \
        sizeof(T) == 2 ? (void)st_le16((p), (uint16_t)(v)) :                                 \
        sizeof(T) == 4 ? (void)st_le32((p), (uint32_t)(v)) : (void)st_le64((p), (uint64_t)(v)))
#endif

#define R cpu.r
#define SX32(x) ((uint64_t)(int64_t)(int32_t)(x))
// Generated functions may keep the guest registers they use, and the cycle
// count, in locals (r_N, cyc_): the compiler then holds them in host
// registers instead of storing and reloading cpu.r[] around every
// instruction (on the Xbox 360's in-order CPU each such store-then-load
// stalls for dozens of cycles). REC_SYNC_OUT writes the ones the function
// changes back to cpu, REC_SYNC_IN reloads them; each function defines both
// before its body, and REC_CYC names the cycle count it uses.
#ifndef REC_SYNC_OUT
#define REC_SYNC_OUT ((void)0)
#define REC_SYNC_IN ((void)0)
#endif
#ifndef REC_CYC
#define REC_CYC cpu.cycles
#endif
#define REC_RETURN do { REC_SYNC_OUT; return; } while (0)
// interpret one op: it reads and writes cpu.r[] (needs a bool rec_t_ in scope)
#define REC_OP(o, p, d) (REC_SYNC_OUT, rec_t_ = rec_op((o), (p), (d)), REC_SYNC_IN, rec_t_)

#define EXIT(t) do { REC_SYNC_OUT; cpu.pc = (t); cpu.npc = (t) + 4; return; } while (0)
// at a backward branch: give the dispatcher a chance to take interrupts / end the slice
#define STOP() (REC_CYC >= REC_DL || irq_dirty)
// The slice's end as STOP reads it: rec_deadline, or a function's local copy
// (only the dispatcher and CALL change it; REC_DL_SYNC refreshes the copy).
#ifndef REC_DL
#define REC_DL rec_deadline
#define REC_DL_SYNC ((void)0)
#endif

// A call (jal/jalr) into translated code: run the callee from here instead of
// going back to the dispatcher twice (to enter it, and to come back), and
// carry on at the return label L when it returns to ret. The checks are the
// dispatcher's own at the same two points (end of slice, interrupt, code
// still matching RAM), so the machine runs exactly as without the shortcut.
// Linked-in code only: a module has no rec_lookup.
#ifndef REC_CALL_NO_GEN_CHECK
#define REC_CALL_NO_GEN_CHECK 0     // 1 only to reproduce the old behaviour in tests
#endif
#ifdef REC_PROFILE
extern unsigned long long rec_call_code_changed;   // profiling: calls during which code was written
extern unsigned rec_call_depth, rec_call_depth_max; // profiling: nested CALLs on the host stack
void rec_call_note_code(uint32_t callee, uint32_t ret);
#define REC_CALL_NOTE_CODE(c, r) (rec_call_code_changed++, rec_call_note_code((c), (r)))
#define REC_DEPTH_IN() (++rec_call_depth > rec_call_depth_max ? (void)(rec_call_depth_max = rec_call_depth) : (void)0)
#define REC_DEPTH_OUT() (rec_call_depth--)
#else
#define REC_CALL_NOTE_CODE(c, r) ((void)0)
#define REC_DEPTH_IN() ((void)0)
#define REC_DEPTH_OUT() ((void)0)
#endif
// The dispatcher sets rec_deadline before entering each function: the end of
// the slice or the next timer interrupt, whichever is first. A callee may
// move the timer (a write to Compare), so CALL sets it again at the same two
// points, entering the callee and carrying on after it.
#define REC_NEW_DEADLINE() (rec_deadline = rec_slice_end < cpu.next_compare ? rec_slice_end : cpu.next_compare)
// A small leaf callee copied into its caller (KI2_RECOMP_FAST): the same
// checks CALL makes, at the same points, without leaving the caller.
// INLINE_ENTER before the copy (the callee's code must still match RAM:
// rec_lookup, which may read cpu.cycles on its slow path, hence the sync);
// INLINE_RET at the copy's "jr ra": back to the caller at ret unless the
// callee went elsewhere, wrote code, or the slice ended.
#define REC_LOOKUP_OK(t) (rec_map_gen == rec_code_gen ? rec_map[((t) - rec_image_base) >> 2] != 0 : \
        (REC_SYNC_OUT, rec_lookup_slow(t) != 0))
#define INLINE_ENTER(t, L) do {                                              \
        ig_ = rec_code_gen;                                                  \
        REC_NEW_DEADLINE(); REC_DL_SYNC;                                     \
        if (STOP() || !REC_LOOKUP_OK(t)) EXIT(t);                            \
        goto L;                                                              \
    } while (0)
#define INLINE_RET(t, ret, L) do {                                           \
        if ((t) != (ret) || rec_code_gen != ig_) EXIT(t);                    \
        REC_NEW_DEADLINE(); REC_DL_SYNC;                                     \
        if (STOP()) EXIT(ret);                                               \
        goto L;                                                              \
    } while (0)
#ifdef REC_MODULE
#define CALL(t, ret, L) EXIT(t)
#else
#define CALL(t, ret, L) do {                                                \
        uint32_t _ct = (t), _gen = rec_code_gen;                             \
        rec_fn_t _f;                                                         \
        REC_NEW_DEADLINE(); REC_DL_SYNC;                                     \
        if (STOP()) EXIT(_ct);                                               \
        REC_SYNC_OUT;                 /* rec_lookup may read cpu.cycles */   \
        cpu.pc = _ct; cpu.npc = _ct + 4;                                     \
        if (!(_f = rec_lookup(_ct))) return;                                 \
        REC_DEPTH_IN();                                                      \
        _f();                                                                \
        REC_DEPTH_OUT();                                                     \
        REC_SYNC_IN;                                                         \
        /* the callee wrote code (an overlay loaded from disk): this    */   \
        /* function may no longer match RAM, so let the dispatcher check */  \
        if (rec_code_gen != _gen && !REC_CALL_NO_GEN_CHECK) { REC_CALL_NOTE_CODE(_ct, (ret)); return; } \
        REC_NEW_DEADLINE(); REC_DL_SYNC;                                     \
        if (cpu.pc != (ret) || STOP()) return;                               \
        goto L;                                                              \
    } while (0)
#endif

static inline void rec_mult(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    int64_t p = (int64_t)(int32_t)rs * (int32_t)rt;
    cpu.lo = SX32((uint32_t)p); cpu.hi = SX32((uint32_t)(p >> 32));
}
static inline void rec_multu(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    uint64_t p = (uint64_t)(uint32_t)rs * (uint32_t)rt;
    cpu.lo = SX32((uint32_t)p); cpu.hi = SX32((uint32_t)(p >> 32));
}
static inline void rec_div(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    int32_t a = (int32_t)rs, b = (int32_t)rt;
    if (b == 0) { cpu.lo = SX32(a < 0 ? 1 : 0xffffffffu); cpu.hi = SX32(a); }
    else if (a == INT32_MIN && b == -1) { cpu.lo = SX32(a); cpu.hi = 0; }
    else { cpu.lo = SX32(a / b); cpu.hi = SX32(a % b); }
}
static inline void rec_divu(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    uint32_t a = (uint32_t)rs, b = (uint32_t)rt;
    if (b == 0) { cpu.lo = SX32(0xffffffffu); cpu.hi = SX32(a); }
    else { cpu.lo = SX32(a / b); cpu.hi = SX32(a % b); }
}
#if defined(_MSC_VER) && !defined(__TINYC__)
#define rec_mul64 mul64          // x360_compat.h: MSVC has no __int128 either
#endif
#if defined(__TINYC__) || defined(_MSC_VER)
static inline void rec_dmultu(uint64_t rs, uint64_t rt) { REC_LOCALS rec_mul64(rs, rt, &cpu.hi, &cpu.lo); }
static inline void rec_dmult(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    rec_mul64(rs, rt, &cpu.hi, &cpu.lo);                  // signed: correct the high half
    if ((int64_t)rs < 0) cpu.hi -= rt;
    if ((int64_t)rt < 0) cpu.hi -= rs;
}
#else
static inline void rec_dmult(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    __int128 p = (__int128)(int64_t)rs * (int64_t)rt;
    cpu.lo = (uint64_t)p; cpu.hi = (uint64_t)(p >> 64);
}
static inline void rec_dmultu(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    unsigned __int128 p = (unsigned __int128)rs * rt;
    cpu.lo = (uint64_t)p; cpu.hi = (uint64_t)(p >> 64);
}
#endif
static inline void rec_ddiv(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    int64_t a = (int64_t)rs, b = (int64_t)rt;
    if (b == 0) { cpu.lo = a < 0 ? 1 : ~0ull; cpu.hi = a; }
    else if (a == INT64_MIN && b == -1) { cpu.lo = a; cpu.hi = 0; }
    else { cpu.lo = a / b; cpu.hi = a % b; }
}
static inline void rec_ddivu(uint64_t rs, uint64_t rt)
{
    REC_LOCALS
    if (rt == 0) { cpu.lo = ~0ull; cpu.hi = rs; }
    else { cpu.lo = rs / rt; cpu.hi = rs % rt; }
}

// Host memory for a guest address, or NULL when the slow path is needed:
// KSEG0/1 RAM directly (ram0 and ram2 are contiguous in `ram`), mapped
// addresses through the TLB page cache. Stores into the code image take the
// slow path so they invalidate it.
#define REC_PA(a) ((a) & 0x1fffffffu)
#define REC_TLBC(a, wr) (tlbc[((a) >> 12) & (TLBC_SIZE - 1)].tag == ((a) >> 12) + 1 &&             \
        tlbc[((a) >> 12) & (TLBC_SIZE - 1)].wr ? tlbc[((a) >> 12) & (TLBC_SIZE - 1)].wr + ((a) & 0xfff) : (uint8_t *)0)
#define rec_ld_ptr_(a) (((a) & 0xc0000000u) != 0x80000000u ? REC_TLBC(a, rd) :                     \
        REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                                   \
        REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0)
#define rec_st_ptr_(a) (((a) & 0xc0000000u) != 0x80000000u ? REC_TLBC(a, wr) :                     \
        REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                                   \
        REC_PA(a) - rec_code_lo < rec_code_size ? (uint8_t *)0 :                                    \
        REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0)
// The same mappings with the common case (KSEG0/1 RAM) tested first, so it is
// the straight path through the code, and the code image's bounds from
// REC_CODE_LO/REC_CODE_SIZE (a function may keep them in locals: they never
// change while translated code runs, but every store through a byte pointer
// would otherwise make the compiler load them again).
#define rec_ld_ptr_fast_(a) (((a) & 0xc0000000u) == 0x80000000u ?                                    \
        (REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                                  \
         REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0) : \
        REC_TLBC(a, rd))
#define rec_st_ptr_fast_(a) (((a) & 0xc0000000u) == 0x80000000u ?                                    \
        (REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                                  \
         REC_PA(a) - REC_CODE_LO < REC_CODE_SIZE ? (uint8_t *)0 :                                   \
         REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0) : \
        REC_TLBC(a, wr))
// Which of the two LD/ST use, and where the bounds and the slice's end are
// read from: each generated function chooses (plain functions keep these).
#ifndef REC_LD_PTR
#define REC_LD_PTR(a) rec_ld_ptr_(a)
#define REC_ST_PTR(a) rec_st_ptr_(a)
#endif
#ifndef REC_CODE_LO
#define REC_CODE_LO rec_code_lo
#define REC_CODE_SIZE rec_code_size
#endif
// every store into RAM marks its page for save states and rollback (hosts
// without either, REC_NO_DIRTY, skip it: the marks never change emulation)
#ifdef REC_NO_DIRTY
#define ram_touch_(p) ((void)0)
#else
#define ram_touch_(p) (ram_dirty[(size_t)((p) - ram) >> 12] = 1)
#endif

#define LD(T, dst, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = REC_LD_PTR(_a); T _v;              \
        if (_p && !(_a & (size - 1))) REC_LOAD(T, _v, _p);                    \
        else { _v = (T)rec_load(_a, size, PC, DL); if (cpu_exc) REC_RETURN; } \
        dst;                                                                 \
    } while (0)
#define ST(T, val, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = REC_ST_PTR(_a); T _v = (T)(val);   \
        if (_p && !(_a & (size - 1))) { REC_STORE(T, _p, _v); ram_touch_(_p); } \
        else { rec_store(_a, (uint64_t)_v, size, PC, DL); if (cpu_exc) REC_RETURN; } \
    } while (0)

// Unaligned LWL/LWR/SWL/SWR/LDL/LDR/SDL/SDR: read (and write back) the
// aligned word containing ea, merging as the interpreter does. Return false
// when an exception was raised.
static inline bool rec_ld_aligned(uint64_t *w, uint32_t a, int size, uint32_t pc, bool dl)
{
    REC_LOCALS
    uint8_t *p = rec_ld_ptr_(a);
    if (p) {
        if (size == 8) REC_LOAD(uint64_t, *w, p);
        else { uint32_t v; REC_LOAD(uint32_t, v, p); *w = v; }
        return true;
    }
    *w = rec_load(a, size, pc, dl);
    return !cpu_exc;
}
static inline bool rec_st_aligned(uint64_t w, uint32_t a, int size, uint32_t pc, bool dl)
{
    REC_LOCALS
    uint8_t *p = rec_st_ptr_(a);
    if (p) {
        if (size == 8) REC_STORE(uint64_t, p, w);
        else { uint32_t v = (uint32_t)w; REC_STORE(uint32_t, p, v); }
        ram_touch_(p);
        return true;
    }
    rec_store(a, w, size, pc, dl);
    return !cpu_exc;
}
static inline bool rec_lwl(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (3 - (ea & 3));
    uint32_t keep = sh ? ((1u << sh) - 1) : 0;
    *rt = SX32(((uint32_t)*rt & keep) | ((uint32_t)w << sh));
    return true;
}
static inline bool rec_lwr(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (ea & 3);
    uint32_t keep = sh ? ~(0xffffffffu >> sh) : 0;
    uint32_t v = ((uint32_t)*rt & keep) | ((uint32_t)w >> sh);
    *rt = sh ? ((*rt & 0xffffffff00000000ull) | v) : SX32(v);
    return true;
}
static inline bool rec_ldl(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (7 - (ea & 7));
    uint64_t keep = sh ? ((1ull << sh) - 1) : 0;
    *rt = (*rt & keep) | (w << sh);
    return true;
}
static inline bool rec_ldr(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (ea & 7);
    uint64_t keep = sh ? ~(~0ull >> sh) : 0;
    *rt = (*rt & keep) | (w >> sh);
    return true;
}
static inline bool rec_swl(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (3 - (ea & 3));
    uint32_t mask = 0xffffffffu >> sh;
    return rec_st_aligned(((uint32_t)w & ~mask) | ((uint32_t)rt >> sh), ea & ~3u, 4, pc, dl);
}
static inline bool rec_swr(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (ea & 3);
    uint32_t mask = 0xffffffffu << sh;
    return rec_st_aligned(((uint32_t)w & ~mask) | ((uint32_t)rt << sh), ea & ~3u, 4, pc, dl);
}
static inline bool rec_sdl(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (7 - (ea & 7));
    uint64_t mask = ~0ull >> sh;
    return rec_st_aligned((w & ~mask) | (rt >> sh), ea & ~7u, 8, pc, dl);
}
static inline bool rec_sdr(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (ea & 7);
    uint64_t mask = ~0ull << sh;
    return rec_st_aligned((w & ~mask) | (rt << sh), ea & ~7u, 8, pc, dl);
}

// COP0 is usable in kernel mode, at exception level, or with Status.CU0 set.
static inline bool rec_cop0_usable(void)
{
    REC_LOCALS
    uint32_t sr = (uint32_t)cpu.cp0[CP0_STATUS];
    return !(sr & SR_KSU) || (sr & (SR_EXL | SR_ERL)) || (sr & 0x10000000u);
}

// COP1 (FPU) ops translated by KI2_RECOMP_FPU: the interpreter's register
// access and arithmetic (cpu.c), with the same C expressions, so results are
// the same bit for bit on each host. Rarer ops still go through rec_op.
#ifdef REC_MODULE
#define REC_FBITS(d, s) __builtin_memcpy(&(d), &(s), sizeof(d))
#define fabsf __builtin_fabsf
#define nearbyint __builtin_nearbyint
#define trunc __builtin_trunc
#define ceil __builtin_ceil
#define floor __builtin_floor
#define isnan __builtin_isnan
#else
#include <math.h>
#define REC_FBITS(d, s) memcpy(&(d), &(s), sizeof(d))
#endif
#define REC_CU1() (((uint32_t)cpu.cp0[CP0_STATUS] & SR_CU1) != 0)
#define REC_FR64() (((uint32_t)cpu.cp0[CP0_STATUS] & SR_FR) != 0)
static inline uint32_t rec_fget32(int n)
{
    REC_LOCALS
    if (REC_FR64()) return (uint32_t)cpu.fpr[n];
    return (n & 1) ? (uint32_t)(cpu.fpr[n & ~1] >> 32) : (uint32_t)cpu.fpr[n];
}
static inline void rec_fset32(int n, uint32_t v)
{
    REC_LOCALS
    if (REC_FR64()) { cpu.fpr[n] = (cpu.fpr[n] & 0xffffffff00000000ull) | v; return; }
    uint64_t *p = &cpu.fpr[n & ~1];
    if (n & 1) *p = (*p & 0xffffffffull) | ((uint64_t)v << 32);
    else *p = (*p & 0xffffffff00000000ull) | v;
}
static inline uint64_t rec_fget64(int n) { REC_LOCALS return cpu.fpr[REC_FR64() ? n : (n & ~1)]; }
static inline void rec_fset64(int n, uint64_t v) { REC_LOCALS cpu.fpr[REC_FR64() ? n : (n & ~1)] = v; }
static inline float rec_fgets(int n) { uint32_t v = rec_fget32(n); float f; REC_FBITS(f, v); return f; }
static inline void rec_fsets(int n, float f) { uint32_t v; REC_FBITS(v, f); rec_fset32(n, v); }
static inline void rec_fsetd(int n, double d) { uint64_t v; REC_FBITS(v, d); rec_fset64(n, v); }
static inline double rec_fround(double x, int mode)
{
    switch (mode) {
    case 0: return nearbyint(x);
    case 1: return trunc(x);
    case 2: return ceil(x);
    default: return floor(x);
    }
}
// CVT.W.S, in the rounding mode FCSR selects
static inline void rec_cvt_w_s(int fd, int fs)
{
    REC_LOCALS
    rec_fset32(fd, (uint32_t)(int32_t)rec_fround(rec_fgets(fs), cpu.fcr31 & 3));
}
// C.cond.S: the condition bit from the function's low four bits
static inline void rec_fcmp_s(int fs, int ft, int c)
{
    REC_LOCALS
    float a = rec_fgets(fs), b = rec_fgets(ft);
    bool un = isnan(a) || isnan(b);
    bool r = ((c & 4) && !un && a < b) || ((c & 2) && !un && a == b) || ((c & 1) && un);
    if (r) cpu.fcr31 |= 0x00800000u; else cpu.fcr31 &= ~0x00800000u;
}
