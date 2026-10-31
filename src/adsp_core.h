// ADSP-2105 core: state and instruction semantics, shared by the interpreter
// (adsp.c) and the translated sound code (adsprec.c's output, compiled into
// the same module as the game code). One copy of the semantics keeps the two
// bit-identical.
//
// Instruction semantics follow the ADSP-2100 Family User's Manual; edge cases
// (rounding, shifter NORM/EXP, SPORT autobuffer setup) were checked against
// MAME's ADSP-21xx core (BSD-3-Clause, Aaron Giles) so the DCS firmware
// behaves the same way it does there.
//
// Types first; the functions are compiled where A is defined (as the adsp_t
// to work on) before including this. Like rec_abi.h, it stands alone without
// the C library under REC_MODULE, and must build with TinyCC too.
#ifndef ADSP_CORE_TYPES_
#define ADSP_CORE_TYPES_

#ifdef REC_MODULE
#include "rec_abi.h"
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

enum { ADSP_IRQ0 = 0, ADSP_IRQ1 = 1, ADSP_IRQ2 = 2, ADSP_TIMER = 5 };

typedef struct {
    // board hooks for external memory
    uint32_t (*pm_read)(uint16_t addr);            // 24-bit program word
    void     (*pm_write)(uint16_t addr, uint32_t v);
    uint16_t (*dm_read)(uint16_t addr);
    void     (*dm_write)(uint16_t addr, uint16_t v);
    void     (*sample_out)(int16_t s);             // one SPORT1 sample
    void     (*sample_rate)(double hz);            // SPORT1 rate changed
    void     (*sysctl_reboot)(void);               // SYSCONTROL bit 9 soft reboot
} adsp_hooks_t;

typedef struct {
    uint16_t ax0, ax1, ay0, ay1, ar, af;
    uint16_t mx0, mx1, my0, my1, mf;
    uint16_t si;
    int8_t   se;
    int8_t   sb;
    uint32_t sr;
    int64_t  mr;      // 40-bit accumulator, kept sign-extended
} regset_t;

typedef struct {
    adsp_hooks_t h;
    regset_t r, alt;

    uint16_t i[8], l[8], base[8];
    int16_t  m[8];

    uint16_t pc;
    uint16_t pc_stack[16]; int pc_sp;
    uint16_t cntr, cntr_stack[4]; int cntr_sp;
    uint32_t loop_stack[4]; int loop_sp;
    uint16_t loop_end; int loop_cond;
    uint16_t stat_stack[4][3]; int stat_sp;

    uint8_t astat, mstat, sstat, imask, icntl;
    uint16_t ifc;
    uint8_t px;
    bool idle;
    bool polling;             // just looped in a recognised wait-for-autobuffer loop

    union {                   // same layout either way; irq_all for a quick look at all of them
        struct { uint8_t irq_state[6], irq_latch[6]; };
        uint8_t irq_all[12];
    };

    uint32_t ipm[0x400];      // internal program RAM
    uint16_t idm[0x200];      // internal data RAM at 0x3800
    uint16_t ctrl[0x20];      // 0x3fe0-0x3fff

    // timer
    int tscale_cnt;
    // SPORT1 autobuffer transmit
    bool tx_on;
    int tx_ireg, tx_mreg;
    uint16_t tx_base, tx_size;
    int tx_period, tx_cnt;
} adsp_t;

// Instruction groups (opcode bits 23-16) and the function in the core that
// runs each: X(first group, last group, function), every group exactly once.
// The interpreter's switch and the translator's output both come from this.
#define ADSP_GROUPS(X) \
    X(0x00, 0x00, op_nop)        X(0x01, 0x01, op_dual)       X(0x02, 0x02, op_idle) \
    X(0x03, 0x03, op_nop) /* jump/call on FLAG_IN: FI is never asserted */ \
    X(0x04, 0x04, op_stack)      X(0x05, 0x05, op_sat_mr)     X(0x06, 0x06, op_divs) \
    X(0x07, 0x07, op_divq)       X(0x08, 0x08, op_dual)       X(0x09, 0x09, op_modify) \
    X(0x0a, 0x0a, op_rts)        X(0x0b, 0x0b, op_jump_ind)   X(0x0c, 0x0c, op_mode) \
    X(0x0d, 0x0d, op_move)       X(0x0e, 0x0e, op_shift_cond) X(0x0f, 0x0f, op_shift_imm) \
    X(0x10, 0x10, op_shift_move) X(0x11, 0x11, op_shift_pm)   X(0x12, 0x12, op_shift_dm1) \
    X(0x13, 0x13, op_shift_dm2)  X(0x14, 0x17, op_do)         X(0x18, 0x1b, op_jump) \
    X(0x1c, 0x1f, op_call) \
    X(0x20, 0x21, op_mr_cond)    X(0x22, 0x23, op_ar_cond)    X(0x24, 0x25, op_mf_cond) \
    X(0x26, 0x27, op_af_cond)    X(0x28, 0x29, op_mr_move)    X(0x2a, 0x2b, op_ar_move) \
    X(0x2c, 0x2d, op_mf_move)    X(0x2e, 0x2f, op_af_move)    X(0x30, 0x3f, op_load_imm14) \
    X(0x40, 0x4f, op_load_imm16) \
    X(0x50, 0x51, op_mr_pmrd)    X(0x52, 0x53, op_ar_pmrd)    X(0x54, 0x55, op_mf_pmrd) \
    X(0x56, 0x57, op_af_pmrd)    X(0x58, 0x59, op_mr_pmwr)    X(0x5a, 0x5b, op_ar_pmwr) \
    X(0x5c, 0x5d, op_mf_pmwr)    X(0x5e, 0x5f, op_af_pmwr) \
    X(0x60, 0x61, op_mr_dm1rd)   X(0x62, 0x63, op_ar_dm1rd)   X(0x64, 0x65, op_mf_dm1rd) \
    X(0x66, 0x67, op_af_dm1rd)   X(0x68, 0x69, op_mr_dm1wr)   X(0x6a, 0x6b, op_ar_dm1wr) \
    X(0x6c, 0x6d, op_mf_dm1wr)   X(0x6e, 0x6f, op_af_dm1wr) \
    X(0x70, 0x71, op_mr_dm2rd)   X(0x72, 0x73, op_ar_dm2rd)   X(0x74, 0x75, op_mf_dm2rd) \
    X(0x76, 0x77, op_af_dm2rd)   X(0x78, 0x79, op_mr_dm2wr)   X(0x7a, 0x7b, op_ar_dm2wr) \
    X(0x7c, 0x7d, op_mf_dm2wr)   X(0x7e, 0x7f, op_af_dm2wr) \
    X(0x80, 0x8f, op_dm_rd_abs)  X(0x90, 0x9f, op_dm_wr_abs)  X(0xa0, 0xaf, op_dm1_wr_imm) \
    X(0xb0, 0xbf, op_dm2_wr_imm) X(0xc0, 0xff, op_dual)

#endif

#if defined(A) && !defined(ADSP_CORE_FUNCS_)
#define ADSP_CORE_FUNCS_

// Translated code (ADSP_TRANSLATED) calls the instruction functions with
// constant opcodes: forcing them inline folds the decoding away. What runs
// rarely (ADSP_SLOW) stays out of line, or every instruction would carry a
// copy of it.
#if defined(ADSP_TRANSLATED) && defined(__GNUC__) && !defined(__TINYC__)
#define ADSP_FN static inline __attribute__((always_inline))
#define ADSP_SLOW static __attribute__((noinline))
#else
#define ADSP_FN static inline
#define ADSP_SLOW static inline
#endif

// ASTAT bits
#define AZ 0x01
#define AN 0x02
#define AV 0x04
#define AC 0x08
#define AS 0x10
#define AQ 0x20
#define MV 0x40
#define SS 0x80

// MSTAT bits
#define M_BANK  0x01
#define M_BREV  0x02
#define M_STKV  0x04
#define M_SAT   0x08
#define M_INT   0x10
#define M_TIMER 0x20
#define M_GO    0x40

// memory-mapped control registers (offset from 0x3fe0)
enum {
    CR_S1_AUTOBUF = 0x0f, CR_S1_RFSDIV, CR_S1_SCLKDIV, CR_S1_CONTROL,
    CR_TSCALE = 0x1b, CR_TCOUNT, CR_TPERIOD, CR_WAIT, CR_SYSCTL,
};

// Called after the DSP writes its internal program RAM (code may have changed).
#ifndef ADSP_PM_WRITTEN
#define ADSP_PM_WRITTEN(addr) ((void)0)
#endif

#if defined(__GNUC__) && !defined(__TINYC__)
#define adsp_clz_(v) __builtin_clz(v)
#else
static int adsp_clz_(uint32_t v)
{
    int n = 0;
    while (!(v & 0x80000000u)) { v <<= 1; n++; }
    return n;
}
#endif

/* ---------------- memory ---------------- */

ADSP_SLOW uint16_t dm_read(uint16_t addr)
{
    addr &= 0x3fff;
    if (addr >= 0x3800 && addr < 0x3a00) return A.idm[addr - 0x3800];
    if (addr >= 0x3fe0) return A.ctrl[addr - 0x3fe0];
    return A.h.dm_read(addr);
}

ADSP_SLOW void ctrl_write(int reg, uint16_t v);

ADSP_SLOW void dm_write(uint16_t addr, uint16_t v)
{
    addr &= 0x3fff;
    if (addr >= 0x3800 && addr < 0x3a00) { A.idm[addr - 0x3800] = v; return; }
    if (addr >= 0x3fe0) { ctrl_write(addr - 0x3fe0, v); return; }
    A.h.dm_write(addr, v);
}

ADSP_SLOW uint32_t pm_read(uint16_t addr)
{
    addr &= 0x3fff;
    if (addr < 0x400) return A.ipm[addr];
    return A.h.pm_read(addr) & 0xffffff;
}

ADSP_SLOW void pm_write(uint16_t addr, uint32_t v)
{
    addr &= 0x3fff;
    if (addr < 0x400) { A.ipm[addr] = v & 0xffffff; ADSP_PM_WRITTEN(addr); return; }
    A.h.pm_write(addr, v & 0xffffff);
}

/* ---------------- stacks ---------------- */

ADSP_SLOW void pc_push(uint16_t v)
{
    if (A.pc_sp < 16) { A.pc_stack[A.pc_sp++] = v; A.sstat &= ~0x01; }
    else A.sstat |= 0x02;
}
ADSP_SLOW uint16_t pc_pop(void)
{
    if (A.pc_sp > 0 && --A.pc_sp == 0) A.sstat |= 0x01;
    return A.pc_stack[A.pc_sp];
}
ADSP_FN uint16_t pc_top(void) { return A.pc_stack[A.pc_sp ? A.pc_sp - 1 : 0]; }

ADSP_SLOW void cntr_push(void)
{
    if (A.cntr_sp < 4) { A.cntr_stack[A.cntr_sp++] = A.cntr; A.sstat &= ~0x04; }
    else A.sstat |= 0x08;
}
ADSP_SLOW void cntr_pop(void)
{
    if (A.cntr_sp > 0 && --A.cntr_sp == 0) A.sstat |= 0x04;
    A.cntr = A.cntr_stack[A.cntr_sp];
}

ADSP_SLOW void loop_push(uint32_t v)
{
    if (A.loop_sp < 4) {
        A.loop_stack[A.loop_sp++] = v;
        A.loop_end = (uint16_t)(v >> 4);
        A.loop_cond = v & 15;
        A.sstat &= ~0x40;
    } else A.sstat |= 0x80;
}
ADSP_SLOW void loop_pop(void)
{
    if (A.loop_sp == 0) return;
    if (--A.loop_sp == 0) { A.loop_end = 0xffff; A.loop_cond = 0; A.sstat |= 0x40; }
    else {
        A.loop_end = (uint16_t)(A.loop_stack[A.loop_sp - 1] >> 4);
        A.loop_cond = A.loop_stack[A.loop_sp - 1] & 15;
    }
}

ADSP_SLOW void set_mstat(uint8_t v);

ADSP_SLOW void stat_push(void)
{
    if (A.stat_sp < 4) {
        A.stat_stack[A.stat_sp][0] = A.mstat;
        A.stat_stack[A.stat_sp][1] = A.imask;
        A.stat_stack[A.stat_sp][2] = A.astat;
        A.stat_sp++;
        A.sstat &= ~0x10;
    } else A.sstat |= 0x20;
}
ADSP_SLOW void stat_pop(void)
{
    if (A.stat_sp > 0 && --A.stat_sp == 0) A.sstat |= 0x10;
    set_mstat((uint8_t)A.stat_stack[A.stat_sp][0]);
    A.imask = (uint8_t)A.stat_stack[A.stat_sp][1];
    A.astat = (uint8_t)A.stat_stack[A.stat_sp][2];
}

/* ---------------- status ---------------- */

ADSP_SLOW void set_mstat(uint8_t v)
{
    v &= 0x7f;
    if ((v ^ A.mstat) & M_BANK) { regset_t t = A.r; A.r = A.alt; A.alt = t; }
    if ((v ^ A.mstat) & M_TIMER) A.tscale_cnt = 0;
    A.mstat = v;
}

ADSP_FN uint8_t clear_mask(void)
{
    return (A.mstat & M_STKV) ? (uint8_t)~(AC | AN | AZ) : (uint8_t)~(AC | AV | AN | AZ);
}

ADSP_FN bool cond(int c)
{
    uint8_t s = A.astat;
    bool z = s & AZ, n = s & AN, v = s & AV, cy = s & AC;
    switch (c) {
    case 0: return z;
    case 1: return !z;
    case 2: return !((n ^ v) | z);
    case 3: return (n ^ v) | z;
    case 4: return n ^ v;
    case 5: return !(n ^ v);
    case 6: return v;
    case 7: return !v;
    case 8: return cy;
    case 9: return !cy;
    case 10: return s & AS;
    case 11: return !(s & AS);
    case 12: return s & MV;
    case 13: return !(s & MV);
    case 14: // NOT CE: decrement the loop counter, pop it when it expires
        A.cntr--;
        if ((int16_t)A.cntr > 0) return true;
        cntr_pop();
        return false;
    default: return true;
    }
}

/* ---------------- registers ---------------- */

ADSP_FN uint16_t mr0(void) { return (uint16_t)A.r.mr; }
ADSP_FN uint16_t mr1(void) { return (uint16_t)(A.r.mr >> 16); }
ADSP_FN uint16_t mr2(void) { return (uint16_t)(int16_t)(int8_t)(A.r.mr >> 32); }

ADSP_FN void set_mr0(uint16_t v) { A.r.mr = (A.r.mr & ~0xffffLL) | v; }
ADSP_FN void set_mr1(uint16_t v)
{
    // writing MR1 sign-extends into MR2
    A.r.mr = (int64_t)(int16_t)v * 65536 | (A.r.mr & 0xffff);
}
ADSP_FN void set_mr2(uint16_t v)
{
    A.r.mr = (int64_t)(int8_t)v * 4294967296LL | (A.r.mr & 0xffffffffLL);
}

ADSP_SLOW void update_l(int n)
{
    uint16_t l = A.l[n] & 0x3fff;
    uint16_t mask;
    if (l <= 1) mask = 0x3fff;
    else {
        int bits = 32 - adsp_clz_((uint32_t)(l - 1)); // smallest power of two >= l
        mask = (uint16_t)(0x3fff & ~((1u << bits) - 1));
    }
    A.base[n] = A.i[n] & mask;
}

ADSP_SLOW uint16_t lmask(int n)
{
    uint16_t l = A.l[n] & 0x3fff;
    if (l <= 1) return 0x3fff;
    int bits = 32 - adsp_clz_((uint32_t)(l - 1));
    return (uint16_t)(0x3fff & ~((1u << bits) - 1));
}

ADSP_FN void set_i(int n, uint16_t v) { A.i[n] = v & 0x3fff; A.base[n] = A.i[n] & lmask(n); }
ADSP_FN void set_l(int n, uint16_t v) { A.l[n] = v & 0x3fff; update_l(n); }

ADSP_FN uint16_t rd0(int r)
{
    switch (r) {
    case 0: return A.r.ax0;  case 1: return A.r.ax1;
    case 2: return A.r.mx0;  case 3: return A.r.mx1;
    case 4: return A.r.ay0;  case 5: return A.r.ay1;
    case 6: return A.r.my0;  case 7: return A.r.my1;
    case 8: return A.r.si;   case 9: return (uint16_t)(int16_t)A.r.se;
    case 10: return A.r.ar;  case 11: return mr0();
    case 12: return mr1();   case 13: return mr2();
    case 14: return (uint16_t)A.r.sr;
    default: return (uint16_t)(A.r.sr >> 16);
    }
}

ADSP_FN void wr0(int r, uint16_t v)
{
    switch (r) {
    case 0: A.r.ax0 = v; break;  case 1: A.r.ax1 = v; break;
    case 2: A.r.mx0 = v; break;  case 3: A.r.mx1 = v; break;
    case 4: A.r.ay0 = v; break;  case 5: A.r.ay1 = v; break;
    case 6: A.r.my0 = v; break;  case 7: A.r.my1 = v; break;
    case 8: A.r.si = v; break;   case 9: A.r.se = (int8_t)v; break;
    case 10: A.r.ar = v; break;  case 11: set_mr0(v); break;
    case 12: set_mr1(v); break;  case 13: set_mr2(v); break;
    case 14: A.r.sr = (A.r.sr & 0xffff0000u) | v; break;
    default: A.r.sr = (A.r.sr & 0xffffu) | ((uint32_t)v << 16); break;
    }
}

ADSP_FN uint16_t rd_dag(int base, int r)
{
    int n = base + (r & 3);
    switch (r >> 2) {
    case 0: return A.i[n];
    case 1: return (uint16_t)A.m[n];
    default: return A.l[n];
    }
}

ADSP_FN void wr_dag(int base, int r, uint16_t v)
{
    int n = base + (r & 3);
    switch (r >> 2) {
    case 0: set_i(n, v); break;
    case 1: A.m[n] = (int16_t)(v << 2) >> 2; break;   // 14-bit signed
    case 2: set_l(n, v); break;
    default: break;
    }
}

ADSP_SLOW void tx1_write(uint16_t v);

ADSP_SLOW uint16_t rd3(int r)
{
    switch (r) {
    case 0: return A.astat;
    case 1: return A.mstat;
    case 2: return A.sstat;
    case 3: return A.imask;
    case 4: return A.icntl;
    case 5: return A.cntr;
    case 6: return (uint16_t)(int16_t)A.r.sb;
    case 7: return A.px;
    case 15: return pc_pop();
    default: return 0;
    }
}

ADSP_SLOW void wr3(int r, uint16_t v)
{
    switch (r) {
    case 0: A.astat = (uint8_t)v; break;
    case 1: set_mstat((uint8_t)v); break;
    case 3: A.imask = v & 0x3f; break;
    case 4: A.icntl = v & 0x1f; break;
    case 5: cntr_push(); A.cntr = v & 0x3fff; break;
    case 6: A.r.sb = (int8_t)((int8_t)(v << 3) >> 3); break;   // 5-bit signed
    case 7: A.px = (uint8_t)v; break;
    case 11: tx1_write(v); break;
    case 12:
        A.ifc = v;
        // clear bits 1..5, force bits 7..11 (IRQ0, IRQ1, SPORT0 RX, SPORT0 TX, IRQ2)
        if (v & 0x0002) A.irq_latch[0] = 0;
        if (v & 0x0004) A.irq_latch[1] = 0;
        if (v & 0x0008) A.irq_latch[3] = 0;
        if (v & 0x0010) A.irq_latch[4] = 0;
        if (v & 0x0020) A.irq_latch[2] = 0;
        if (v & 0x0080) A.irq_latch[0] = 1;
        if (v & 0x0100) A.irq_latch[1] = 1;
        if (v & 0x0200) A.irq_latch[3] = 1;
        if (v & 0x0400) A.irq_latch[4] = 1;
        if (v & 0x0800) A.irq_latch[2] = 1;
        break;
    case 13: A.cntr = v & 0x3fff; break;
    case 15: pc_push(v & 0x3fff); break;
    default: break;
    }
}

ADSP_FN uint16_t rdg(int g, int r)
{
    switch (g) {
    case 0: return rd0(r);
    case 1: return rd_dag(0, r);
    case 2: return rd_dag(4, r);
    default: return rd3(r);
    }
}

ADSP_FN void wrg(int g, int r, uint16_t v)
{
    switch (g) {
    case 0: wr0(r, v); break;
    case 1: wr_dag(0, r, v); break;
    case 2: wr_dag(4, r, v); break;
    default: wr3(r, v); break;
    }
}

/* ---------------- DAGs ---------------- */

ADSP_FN void post_modify(int ir, int mr)
{
    uint16_t base = A.base[ir], l = A.l[ir];
    uint16_t i = (uint16_t)((A.i[ir] + A.m[mr]) & 0x3fff);
    if (l) {
        if (i < base) i += l;
        else if (i >= base + l) i -= l;
    }
    A.i[ir] = i;
}

ADSP_FN uint16_t bit_reverse14(uint16_t v)
{
    uint32_t r = v & 0x3fff;                // reverse 16 bits, then drop the two that were zero
    r = ((r >> 1) & 0x5555) | ((r & 0x5555) << 1);
    r = ((r >> 2) & 0x3333) | ((r & 0x3333) << 2);
    r = ((r >> 4) & 0x0f0f) | ((r & 0x0f0f) << 4);
    r = ((r >> 8) & 0x00ff) | ((r & 0x00ff) << 8);
    return (uint16_t)(r >> 2);
}

ADSP_FN uint16_t dag1_read(uint32_t op)
{
    int ir = (op >> 2) & 3, mr = op & 3;
    uint16_t addr = (A.mstat & M_BREV) ? bit_reverse14(A.i[ir]) : A.i[ir];
    uint16_t v = dm_read(addr);
    post_modify(ir, mr);
    return v;
}
ADSP_FN void dag1_write(uint32_t op, uint16_t v)
{
    int ir = (op >> 2) & 3, mr = op & 3;
    uint16_t addr = (A.mstat & M_BREV) ? bit_reverse14(A.i[ir]) : A.i[ir];
    dm_write(addr, v);
    post_modify(ir, mr);
}
ADSP_FN uint16_t dag2_read(uint32_t op)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    uint16_t v = dm_read(A.i[ir]);
    post_modify(ir, mr);
    return v;
}
ADSP_FN void dag2_write(uint32_t op, uint16_t v)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    dm_write(A.i[ir], v);
    post_modify(ir, mr);
}
ADSP_FN uint16_t pm_dag2_read(uint32_t op)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    uint32_t w = pm_read(A.i[ir]);
    post_modify(ir, mr);
    A.px = (uint8_t)w;
    return (uint16_t)(w >> 8);
}
ADSP_FN void pm_dag2_write(uint32_t op, uint16_t v)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    pm_write(A.i[ir], ((uint32_t)v << 8) | A.px);
    post_modify(ir, mr);
}

/* ---------------- ALU ---------------- */

ADSP_FN uint16_t alu_x(int x)
{
    switch (x) {
    case 0: return A.r.ax0; case 1: return A.r.ax1; case 2: return A.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)A.r.sr; default: return (uint16_t)(A.r.sr >> 16);
    }
}
ADSP_FN uint16_t alu_y(int y)
{
    switch (y) { case 0: return A.r.ay0; case 1: return A.r.ay1; case 2: return A.r.af; default: return 0; }
}

ADSP_FN void flags_nz(int32_t r)
{
    A.astat &= clear_mask();
    if ((r & 0xffff) == 0) A.astat |= AZ;
    if (r & 0x8000) A.astat |= AN;
}
ADSP_FN void flags_v(int32_t s, int32_t d, int32_t r)
{
    A.astat |= ((s ^ d ^ r ^ (r >> 1)) >> 13) & AV;
}

// Returns the 16-bit result and updates ASTAT.
ADSP_FN uint16_t alu(uint32_t op, bool saturate)
{
    int32_t x = alu_x((op >> 8) & 7), y = alu_y((op >> 11) & 3);
    int32_t c = (A.astat & AC) ? 1 : 0;
    int32_t r;
    switch ((op >> 13) & 15) {
    case 0x0: r = y; flags_nz(r); break;
    case 0x1: r = y + 1; flags_nz(r); if (y == 0x7fff) A.astat |= AV; else if (y == 0xffff) A.astat |= AC; break;
    case 0x2: y += c; r = x + y; flags_nz(r); flags_v(x, y, r); A.astat |= (r >> 13) & AC; break;
    case 0x3: r = x + y; flags_nz(r); flags_v(x, y, r); A.astat |= (r >> 13) & AC; break;
    case 0x4: r = y ^ 0xffff; flags_nz(r); break;
    case 0x5: r = -y; flags_nz(r); if (y == 0x8000) A.astat |= AV; if (y == 0) A.astat |= AC; break;
    case 0x6: r = x - y + c - 1; flags_nz(r); flags_v(x, y, r); A.astat |= (~r >> 13) & AC; break;
    case 0x7: r = x - y; flags_nz(r); flags_v(x, y, r); A.astat |= (~r >> 13) & AC; break;
    case 0x8: r = y - 1; flags_nz(r); if (y == 0x8000) A.astat |= AV; else if (y == 0) A.astat |= AC; break;
    case 0x9: r = y - x; flags_nz(r); flags_v(y, x, r); A.astat |= (~r >> 13) & AC; break;
    case 0xa: r = y - x + c - 1; flags_nz(r); flags_v(y, x, r); A.astat |= (~r >> 13) & AC; break;
    case 0xb: r = x ^ 0xffff; flags_nz(r); break;
    case 0xc: r = x & y; flags_nz(r); break;
    case 0xd: r = x | y; flags_nz(r); break;
    case 0xe: r = x ^ y; flags_nz(r); break;
    default:  // ABS
        r = (x & 0x8000) ? -x : x;
        A.astat &= clear_mask();
        A.astat &= ~AS;
        if (x == 0) A.astat |= AZ;
        if (x == 0x8000) A.astat |= AN | AV;
        if (x & 0x8000) A.astat |= AS;
        break;
    }
    if (saturate && (A.mstat & M_SAT) && (A.astat & AV)) r = (A.astat & AC) ? -32768 : 32767;
    return (uint16_t)r;
}

ADSP_FN void alu_ar(uint32_t op) { A.r.ar = alu(op, true); }
ADSP_FN void alu_af(uint32_t op) { A.r.af = alu(op, false); }

/* ---------------- MAC ---------------- */

ADSP_FN uint16_t mac_x(int x)
{
    switch (x) {
    case 0: return A.r.mx0; case 1: return A.r.mx1; case 2: return A.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)A.r.sr; default: return (uint16_t)(A.r.sr >> 16);
    }
}
ADSP_FN uint16_t mac_y(int y)
{
    switch (y) { case 0: return A.r.my0; case 1: return A.r.my1; case 2: return A.r.mf; default: return 0; }
}

// Computes the MAC result; returns false for the no-op encoding.
ADSP_FN bool mac(uint32_t op, int64_t *out)
{
    int shift = (A.mstat & M_INT) ? 0 : 1;
    int xs = (op >> 8) & 7, ys = (op >> 11) & 3;
    int f = (op >> 13) & 15;
    if (f == 0) return false;
    // operand signedness: bit0 of the low two bits = y unsigned, bit1 = x unsigned (for SS/SU/US/UU forms)
    int32_t x, y;
    int form = (f <= 3) ? 0 : (f & 3);   // RND forms use signed*signed
    x = (form & 2) ? (int32_t)mac_x(xs) : (int32_t)(int16_t)mac_x(xs);
    y = (form & 1) ? (int32_t)mac_y(ys) : (int32_t)(int16_t)mac_y(ys);
    int32_t prod = (int32_t)((uint32_t)(x * y) << shift);
    int64_t res;
    switch (f) {
    case 0x1: res = prod; break;
    case 0x2: res = A.r.mr + prod; break;
    case 0x3: res = A.r.mr - prod; break;
    case 0x4: case 0x5: case 0x6: case 0x7: res = prod; break;
    case 0x8: case 0x9: case 0xa: case 0xb: res = A.r.mr + prod; break;
    default: res = A.r.mr - prod; break;
    }
    if (f <= 3) { // RND: round to nearest, ties to even on the product's low half
        res += 0x8000;
        if ((prod & 0xffff) == 0x8000) res &= ~(int64_t)0x10000;
    }
    *out = res;
    return true;
}

ADSP_FN void mac_mr(uint32_t op)
{
    int64_t res;
    if (!mac(op, &res)) return;
    uint32_t top = (uint32_t)(res >> 31) & 0x1ff;
    A.astat &= ~MV;
    if (top != 0 && top != 0x1ff) A.astat |= MV;
    // keep MR as a sign-extended 40-bit quantity
    A.r.mr = (int64_t)((uint64_t)res << 24) >> 24;
}

ADSP_FN void mac_mf(uint32_t op)
{
    int64_t res;
    if (!mac(op, &res)) return;
    A.r.mf = (uint16_t)((uint32_t)res >> 16);
}

/* ---------------- shifter ---------------- */

ADSP_FN uint16_t sh_x(int x)
{
    switch (x) {
    case 0: case 1: return A.r.si; case 2: return A.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)A.r.sr; default: return (uint16_t)(A.r.sr >> 16);
    }
}

ADSP_FN int clz32(uint32_t v) { return v ? adsp_clz_(v) : 32; }

ADSP_FN void shift(uint32_t op, int sc)
{
    int f = (op >> 11) & 15;
    uint16_t xu = sh_x((op >> 8) & 7);
    int32_t xs = (int16_t)xu;
    bool hi = !(f & 2);
    bool or_ = f & 1;
    uint32_t res = 0;

    switch (f >> 2) {
    case 0: { // LSHIFT
        uint32_t v = hi ? (uint32_t)xu << 16 : xu;
        if (sc > 0) res = sc < 32 ? v << sc : 0;
        else res = sc > -32 ? v >> -sc : 0;
        break;
    }
    case 1: { // ASHIFT
        int32_t v = hi ? (int32_t)((uint32_t)xs << 16) : xs;
        if (sc > 0) res = sc < 32 ? (uint32_t)v << sc : 0;
        else res = (uint32_t)(sc > -32 ? v >> -sc : v >> 31);
        break;
    }
    case 2: // NORM
        if (hi) {
            int32_t v = (int32_t)((uint32_t)xs << 16);
            if (sc > 0) {
                v = (int32_t)(((uint32_t)v >> 1) | ((uint32_t)(A.astat & AC) << 28));
                res = (uint32_t)(v >> (sc - 1));
            } else res = sc > -32 ? (uint32_t)v << -sc : 0;
        } else {
            uint32_t v = xu;
            if (sc > 0) res = sc < 32 ? v >> sc : 0;
            else res = sc > -32 ? v << -sc : 0;
        }
        break;
    default: // EXP / EXPADJ (immediate form never reaches here)
        switch (f & 3) {
        case 0: // EXP (HI)
            if (xs < 0) { A.astat |= SS; A.r.se = (int8_t)-(clz32(~(uint32_t)xs) - 17); }
            else { A.astat &= ~SS; A.r.se = (int8_t)-(clz32((uint32_t)xs) - 17); }
            return;
        case 1: // EXP (HIX)
            if (A.astat & AV) {
                A.r.se = 1;
                if (xs < 0) A.astat &= ~SS; else A.astat |= SS;
            } else {
                if (xs < 0) { A.astat |= SS; A.r.se = (int8_t)-(clz32(~(uint32_t)xs) - 17); }
                else { A.astat &= ~SS; A.r.se = (int8_t)-(clz32((uint32_t)xs) - 17); }
            }
            return;
        case 2: // EXP (LO)
            if (A.r.se == -15) {
                uint32_t v = (uint32_t)((A.astat & SS) ? ~xs : xs) & 0xffff;
                A.r.se = (int8_t)-(clz32(v) - 1);
            }
            return;
        default: { // EXPADJ
            int r = clz32((uint32_t)(xs < 0 ? ~xs : xs)) - 17;
            if (r < -A.r.sb) A.r.sb = (int8_t)-r;
            return;
        }
        }
    }
    if (or_) A.r.sr |= res; else A.r.sr = res;
}

ADSP_FN void shift_op(uint32_t op) { shift(op, A.r.se); }
ADSP_FN void shift_imm(uint32_t op)
{
    if (((op >> 11) & 15) >= 12) return;
    shift(op, (int8_t)op);
}

/* ---------------- control regs, timer, SPORT1 ---------------- */

ADSP_SLOW void tx_recompute(void)
{
    if (A.ctrl[CR_S1_CONTROL] & 0x4000)
        A.tx_period = 2 * (A.ctrl[CR_S1_SCLKDIV] + 1) * 16;
    else
        A.tx_period = 320;   // external 31.25 kHz frame clock at 10 MHz
    if (A.h.sample_rate) A.h.sample_rate(10000000.0 / A.tx_period);
}

ADSP_SLOW void tx1_write(uint16_t v)
{
    if ((A.ctrl[CR_SYSCTL] & 0x0800) && (A.ctrl[CR_S1_AUTOBUF] & 0x0002)) {
        A.tx_ireg = (A.ctrl[CR_S1_AUTOBUF] >> 9) & 7;
        A.tx_mreg = ((A.ctrl[CR_S1_AUTOBUF] >> 7) & 3) | (A.tx_ireg & 4);
        A.tx_size = A.l[A.tx_ireg];
        // The firmware may or may not have already advanced the pointer;
        // align to the buffer start as the DCS buffers are 16-word aligned.
        A.tx_base = A.i[A.tx_ireg] & ~0xf;
        A.i[A.tx_ireg] = A.tx_base;
        A.tx_on = A.tx_size != 0;
        A.tx_cnt = 0;
        tx_recompute();
        return;
    }
    (void)v;
    A.tx_on = false;
}

ADSP_SLOW void ctrl_write(int reg, uint16_t v)
{
    A.ctrl[reg] = v;
    switch (reg) {
    case CR_SYSCTL:
        if (v & 0x0200) {
            if (A.h.sysctl_reboot) A.h.sysctl_reboot();
            A.ctrl[CR_SYSCTL] = 0;
            return;
        }
        if (!(v & 0x0800)) A.tx_on = false;
        break;
    case CR_S1_AUTOBUF:
        if (!(v & 0x0002)) A.tx_on = false;
        break;
    case CR_S1_CONTROL: case CR_S1_SCLKDIV:
        if (A.tx_on) tx_recompute();
        break;
    case CR_TSCALE:
        A.tscale_cnt = 0;
        break;
    }
}

ADSP_SLOW void timer_tick(int cycles)
{
    if (!(A.mstat & M_TIMER)) return;
    int scale = (A.ctrl[CR_TSCALE] & 0xff) + 1;
    A.tscale_cnt += cycles;
    while (A.tscale_cnt >= scale) {
        A.tscale_cnt -= scale;
        if (A.ctrl[CR_TCOUNT] == 0) {
            A.ctrl[CR_TCOUNT] = A.ctrl[CR_TPERIOD];
            A.irq_latch[ADSP_TIMER] = 1;
        } else A.ctrl[CR_TCOUNT]--;
    }
}

ADSP_SLOW void pulse_irq(int line);

ADSP_SLOW void tx_tick(int cycles)
{
    if (!A.tx_on) return;
    A.tx_cnt += cycles;
    while (A.tx_cnt >= A.tx_period) {
        A.tx_cnt -= A.tx_period;
        uint16_t s = dm_read(A.i[A.tx_ireg]);
        if (A.h.sample_out) A.h.sample_out((int16_t)s);
        uint16_t i = (uint16_t)(A.i[A.tx_ireg] + A.m[A.tx_mreg]);
        if (i >= A.tx_base + A.tx_size) {
            i = A.tx_base;
            pulse_irq(ADSP_IRQ1);
        }
        A.i[A.tx_ireg] = i & 0x3fff;
    }
}

/* ---------------- interrupts ---------------- */

ADSP_SLOW bool take_irq(int line, int index)
{
    if (!(A.imask & (0x20 >> index))) return false;
    A.irq_latch[line] = 0;
    pc_push(A.pc);
    stat_push();
    A.pc = (uint16_t)(0x04 + index * 4);
    A.idle = A.polling = false;
    if (A.icntl & 0x10) A.imask &= ~(0x3f >> index);
    else A.imask &= ~0x3f;
    return true;
}

ADSP_SLOW void check_irqs(void)
{
    bool c;
    c = (A.icntl & 4) ? A.irq_latch[ADSP_IRQ2] : A.irq_state[ADSP_IRQ2];
    if (c && take_irq(ADSP_IRQ2, 0)) return;
    if (A.irq_latch[4] && take_irq(4, 1)) return;
    if (A.irq_latch[3] && take_irq(3, 2)) return;
    c = (A.icntl & 2) ? A.irq_latch[ADSP_IRQ1] : A.irq_state[ADSP_IRQ1];
    if (c && take_irq(ADSP_IRQ1, 3)) return;
    c = (A.icntl & 1) ? A.irq_latch[ADSP_IRQ0] : A.irq_state[ADSP_IRQ0];
    if (c && take_irq(ADSP_IRQ0, 4)) return;
    if (A.irq_latch[ADSP_TIMER] && take_irq(ADSP_TIMER, 5)) return;
}

ADSP_SLOW void set_irq(int line, bool state)
{
    if (state && !A.irq_state[line]) A.irq_latch[line] = 1;
    A.irq_state[line] = state;
}

ADSP_SLOW void pulse_irq(int line)
{
    set_irq(line, true);
    set_irq(line, false);
}

/* ---------------- execution ---------------- */

// "top: Dreg = Ireg; AF|AR = Dreg op AYn; IF cond JUMP top" only rereads an
// index register, which nothing but the autobuffer (or an interrupt) changes,
// and every iteration leaves the same state behind. Once it has looped, the
// DSP may skip ahead to the next transmitted sample. Decided from program
// memory as it is now, so translated and interpreted code always agree.
ADSP_SLOW bool poll_words(uint32_t mv, uint32_t alu)
{
    static const uint8_t xop_dreg[8] = { 0, 1, 10, 11, 12, 13, 14, 15 };
    int amf = (alu >> 13) & 31, src_grp = (mv >> 8) & 3;
    return (mv >> 16) == 0x0d && ((mv >> 10) & 3) == 0 &&               // Dreg = ...
        (src_grp == 1 || src_grp == 2) && (mv & 15) < 4 &&              // ... I0-I7
        ((alu >> 16) == 0x22 || (alu >> 16) == 0x23 || (alu >> 16) == 0x26 || (alu >> 16) == 0x27) &&
        (alu & 15) == 15 && amf >= 0x10 && amf != 0x12 && amf != 0x16 && amf != 0x1a &&  // ALU, no carry in
        ((alu >> 11) & 3) != 2 &&                                       // Y is not AF
        xop_dreg[(alu >> 8) & 7] == ((mv >> 4) & 15);                   // X is the register just loaded
}

ADSP_SLOW bool is_poll_loop(uint16_t top, uint16_t jump_at)
{
    return jump_at == top + 2 && poll_words(pm_read(top), pm_read((uint16_t)(top + 1))) &&
        !(A.loop_sp && A.loop_end >= top && A.loop_end <= jump_at);
}

// One function per instruction group: see ADSP_GROUPS.
ADSP_FN void op_nop(uint32_t op) { (void)op; }
ADSP_FN void op_idle(uint32_t op)
{
    if (op & 0x8000) A.idle = true;   // IDLE; flag outputs are not wired on DCS
}
ADSP_FN void op_stack(uint32_t op)
{
    if (op & 0x10) pc_pop();
    if (op & 0x08) loop_pop();
    if (op & 0x04) cntr_pop();
    if (op & 0x02) { if (op & 1) stat_pop(); else stat_push(); }
}
ADSP_FN void op_sat_mr(uint32_t op)
{
    (void)op;
    if (A.astat & MV) {
        if (A.r.mr & 0x8000000000LL) A.r.mr = -0x80000000LL;      // 0xff 8000 0000
        else A.r.mr = 0x7fffffffLL;                                  // 0x00 7fff ffff
    }
}
ADSP_FN void op_divs(uint32_t op)
{
    uint16_t y = alu_y((op >> 11) & 3), x = alu_x((op >> 8) & 7);
    uint16_t tq = x ^ y;
    A.astat = (A.astat & ~AQ) | ((tq >> 10) & AQ);
    A.r.af = (uint16_t)((y << 1) | (A.r.ay0 >> 15));
    A.r.ay0 = (uint16_t)((A.r.ay0 << 1) | (tq >> 15));
}
ADSP_FN void op_divq(uint32_t op)
{
    uint16_t x = alu_x((op >> 8) & 7);
    uint32_t res = (A.astat & AQ) ? (uint32_t)A.r.af + x : (uint32_t)A.r.af - x;
    uint32_t tq = res ^ x;
    A.astat = (A.astat & ~AQ) | ((tq >> 10) & AQ);
    A.r.af = (uint16_t)((res << 1) | (A.r.ay0 >> 15));
    A.r.ay0 = (uint16_t)((A.r.ay0 << 1) | ((~tq >> 15) & 1));
}
ADSP_FN void op_modify(uint32_t op)
{
    int ir = (op >> 2) & 7;
    post_modify(ir, (ir & 4) | (op & 3));
}
ADSP_FN void op_rts(uint32_t op)   // RTS / RTI
{
    if (cond(op & 15)) {
        A.pc = pc_pop();
        if (op & 0x10) stat_pop();
    }
}
ADSP_FN void op_jump_ind(uint32_t op)
{
    if (cond(op & 15)) {
        if (op & 0x10) pc_push(A.pc);
        A.pc = A.i[4 + ((op >> 6) & 3)] & 0x3fff;
    }
}
ADSP_FN void op_mode(uint32_t op)   // ENA/DIS mode
{
    uint8_t m = A.mstat;
    if (op & (1 << 3)) m = (op & (1 << 2)) ? (m | M_GO) : (m & ~M_GO);
    if (op & (1 << 13)) m = (op & (1 << 12)) ? (m | M_INT) : (m & ~M_INT);
    if (op & (1 << 15)) m = (op & (1 << 14)) ? (m | M_TIMER) : (m & ~M_TIMER);
    if (op & (1 << 5)) m = (op & (1 << 4)) ? (m | M_BANK) : (m & ~M_BANK);
    if (op & (1 << 7)) m = (op & (1 << 6)) ? (m | M_BREV) : (m & ~M_BREV);
    if (op & (1 << 9)) m = (op & (1 << 8)) ? (m | M_STKV) : (m & ~M_STKV);
    if (op & (1 << 11)) m = (op & (1 << 10)) ? (m | M_SAT) : (m & ~M_SAT);
    set_mstat(m);
}
ADSP_FN void op_move(uint32_t op) { wrg((op >> 10) & 3, (op >> 4) & 15, rdg((op >> 8) & 3, op & 15)); }
ADSP_FN void op_shift_cond(uint32_t op) { if (cond(op & 15)) shift_op(op); }
ADSP_FN void op_shift_imm(uint32_t op) { shift_imm(op); }
ADSP_FN void op_shift_move(uint32_t op) { shift_op(op); uint16_t t = rd0(op & 15); wr0((op >> 4) & 15, t); }
ADSP_FN void op_shift_pm(uint32_t op)
{
    if (op & 0x8000) { pm_dag2_write(op, rd0((op >> 4) & 15)); shift_op(op); }
    else { shift_op(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
}
ADSP_FN void op_shift_dm1(uint32_t op)
{
    if (op & 0x8000) { dag1_write(op, rd0((op >> 4) & 15)); shift_op(op); }
    else { shift_op(op); wr0((op >> 4) & 15, dag1_read(op)); }
}
ADSP_FN void op_shift_dm2(uint32_t op)
{
    if (op & 0x8000) { dag2_write(op, rd0((op >> 4) & 15)); shift_op(op); }
    else { shift_op(op); wr0((op >> 4) & 15, dag2_read(op)); }
}
ADSP_FN void op_do(uint32_t op)   // DO UNTIL
{
    loop_push(op & 0x3ffff);
    pc_push(A.pc);
}
ADSP_FN void op_jump(uint32_t op)
{
    if (cond(op & 15)) {
        uint16_t at = (uint16_t)(A.pc - 1);
        A.pc = (op >> 4) & 0x3fff;
        if (A.pc < at && is_poll_loop(A.pc, at)) A.polling = true;
    }
}
ADSP_FN void op_call(uint32_t op)
{
    if (cond(op & 15)) { pc_push(A.pc); A.pc = (op >> 4) & 0x3fff; }
}
ADSP_FN void op_mr_cond(uint32_t op) { if (cond(op & 15)) mac_mr(op); }
ADSP_FN void op_ar_cond(uint32_t op) { if (cond(op & 15)) alu_ar(op); }
ADSP_FN void op_mf_cond(uint32_t op) { if (cond(op & 15)) mac_mf(op); }
ADSP_FN void op_af_cond(uint32_t op) { if (cond(op & 15)) alu_af(op); }
ADSP_FN void op_mr_move(uint32_t op) { uint16_t t = rd0(op & 15); mac_mr(op); wr0((op >> 4) & 15, t); }
ADSP_FN void op_ar_move(uint32_t op) { uint16_t t = rd0(op & 15); alu_ar(op); wr0((op >> 4) & 15, t); }
ADSP_FN void op_mf_move(uint32_t op) { uint16_t t = rd0(op & 15); mac_mf(op); wr0((op >> 4) & 15, t); }
ADSP_FN void op_af_move(uint32_t op) { uint16_t t = rd0(op & 15); alu_af(op); wr0((op >> 4) & 15, t); }
ADSP_FN void op_load_imm14(uint32_t op)   // non-data register, 14-bit immediate
{
    uint16_t imm = (uint16_t)((int16_t)(((op >> 4) & 0x3fff) << 2) >> 2);
    wrg((op >> 18) & 3, op & 15, imm);
}
ADSP_FN void op_load_imm16(uint32_t op) { wr0(op & 15, (uint16_t)(op >> 4)); }
ADSP_FN void op_mr_pmrd(uint32_t op) { mac_mr(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
ADSP_FN void op_ar_pmrd(uint32_t op) { alu_ar(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
ADSP_FN void op_mf_pmrd(uint32_t op) { mac_mf(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
ADSP_FN void op_af_pmrd(uint32_t op) { alu_af(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
ADSP_FN void op_mr_pmwr(uint32_t op) { pm_dag2_write(op, rd0((op >> 4) & 15)); mac_mr(op); }
ADSP_FN void op_ar_pmwr(uint32_t op) { pm_dag2_write(op, rd0((op >> 4) & 15)); alu_ar(op); }
ADSP_FN void op_mf_pmwr(uint32_t op) { pm_dag2_write(op, rd0((op >> 4) & 15)); mac_mf(op); }
ADSP_FN void op_af_pmwr(uint32_t op) { pm_dag2_write(op, rd0((op >> 4) & 15)); alu_af(op); }
ADSP_FN void op_mr_dm1rd(uint32_t op) { mac_mr(op); wr0((op >> 4) & 15, dag1_read(op)); }
ADSP_FN void op_ar_dm1rd(uint32_t op) { alu_ar(op); wr0((op >> 4) & 15, dag1_read(op)); }
ADSP_FN void op_mf_dm1rd(uint32_t op) { mac_mf(op); wr0((op >> 4) & 15, dag1_read(op)); }
ADSP_FN void op_af_dm1rd(uint32_t op) { alu_af(op); wr0((op >> 4) & 15, dag1_read(op)); }
ADSP_FN void op_mr_dm1wr(uint32_t op) { dag1_write(op, rd0((op >> 4) & 15)); mac_mr(op); }
ADSP_FN void op_ar_dm1wr(uint32_t op) { dag1_write(op, rd0((op >> 4) & 15)); alu_ar(op); }
ADSP_FN void op_mf_dm1wr(uint32_t op) { dag1_write(op, rd0((op >> 4) & 15)); mac_mf(op); }
ADSP_FN void op_af_dm1wr(uint32_t op) { dag1_write(op, rd0((op >> 4) & 15)); alu_af(op); }
ADSP_FN void op_mr_dm2rd(uint32_t op) { mac_mr(op); wr0((op >> 4) & 15, dag2_read(op)); }
ADSP_FN void op_ar_dm2rd(uint32_t op) { alu_ar(op); wr0((op >> 4) & 15, dag2_read(op)); }
ADSP_FN void op_mf_dm2rd(uint32_t op) { mac_mf(op); wr0((op >> 4) & 15, dag2_read(op)); }
ADSP_FN void op_af_dm2rd(uint32_t op) { alu_af(op); wr0((op >> 4) & 15, dag2_read(op)); }
ADSP_FN void op_mr_dm2wr(uint32_t op) { dag2_write(op, rd0((op >> 4) & 15)); mac_mr(op); }
ADSP_FN void op_ar_dm2wr(uint32_t op) { dag2_write(op, rd0((op >> 4) & 15)); alu_ar(op); }
ADSP_FN void op_mf_dm2wr(uint32_t op) { dag2_write(op, rd0((op >> 4) & 15)); mac_mf(op); }
ADSP_FN void op_af_dm2wr(uint32_t op) { dag2_write(op, rd0((op >> 4) & 15)); alu_af(op); }
ADSP_FN void op_dm_rd_abs(uint32_t op) { wrg((op >> 18) & 3, op & 15, dm_read((op >> 4) & 0x3fff)); }
ADSP_FN void op_dm_wr_abs(uint32_t op) { dm_write((op >> 4) & 0x3fff, rdg((op >> 18) & 3, op & 15)); }
ADSP_FN void op_dm1_wr_imm(uint32_t op) { dag1_write(op, (uint16_t)(op >> 4)); }
ADSP_FN void op_dm2_wr_imm(uint32_t op) { dag2_write(op, (uint16_t)(op >> 4)); }
ADSP_FN void op_dual(uint32_t op)   // ALU/MAC with dual memory read (and the unused groups)
{
    uint32_t hi = op >> 16;
    if (hi & 2) alu_ar(op); else mac_mr(op);
    uint16_t dv = dag1_read(op);
    uint16_t pv = pm_dag2_read(op >> 4);
    switch ((hi >> 2) & 3) {
    case 0: A.r.ax0 = dv; break; case 1: A.r.ax1 = dv; break;
    case 2: A.r.mx0 = dv; break; default: A.r.mx1 = dv; break;
    }
    switch ((hi >> 4) & 3) {
    case 0: A.r.ay0 = pv; break; case 1: A.r.ay1 = pv; break;
    case 2: A.r.my0 = pv; break; default: A.r.my1 = pv; break;
    }
}


#ifdef _MSC_VER
// No case ranges in MSVC: a table of the group functions, filled once.
typedef void (*adsp_op_fn_)(uint32_t);
static adsp_op_fn_ adsp_op_tab_[256];
static void adsp_op_tab_fill_(void)
{
#define ADSP_TAB_(lo, hi, fn) for (int g = lo; g <= hi; g++) adsp_op_tab_[g] = fn;
    ADSP_GROUPS(ADSP_TAB_)
#undef ADSP_TAB_
}
ADSP_FN void exec(uint32_t op)
{
    if (!adsp_op_tab_[0]) adsp_op_tab_fill_();
    adsp_op_tab_[(op >> 16) & 0xff](op);
}
#else
ADSP_FN void exec(uint32_t op)
{
    switch (op >> 16) {
#define ADSP_CASE_(lo, hi, fn) case lo ... hi: fn(op); break;
    ADSP_GROUPS(ADSP_CASE_)
#undef ADSP_CASE_
    }
}
#endif

// Some line is raised or latched (the usual case is none): a quick first test.
ADSP_FN bool irq_raised(void)
{
    uint64_t lo;
    uint32_t hi;
#if (defined(__GNUC__) || defined(_MSC_VER)) && !defined(__TINYC__)   // MSVC: memcpy, no unaligned loads on PowerPC
    __builtin_memcpy(&lo, A.irq_all, 8);
    __builtin_memcpy(&hi, A.irq_all + 8, 4);
#else
    lo = *(const uint64_t *)A.irq_all;      // x86: unaligned loads are fine
    hi = *(const uint32_t *)(A.irq_all + 8);
#endif
    return (lo | hi) != 0;
}

// check_irqs() would take an interrupt: one is raised and not masked.
ADSP_SLOW bool irq_ready(void)
{
    uint8_t m = A.imask;
    if (!m) return false;
    return (((A.icntl & 4) ? A.irq_latch[ADSP_IRQ2] : A.irq_state[ADSP_IRQ2]) && (m & 0x20)) ||
           (A.irq_latch[4] && (m & 0x10)) || (A.irq_latch[3] && (m & 0x08)) ||
           (((A.icntl & 2) ? A.irq_latch[ADSP_IRQ1] : A.irq_state[ADSP_IRQ1]) && (m & 0x04)) ||
           (((A.icntl & 1) ? A.irq_latch[ADSP_IRQ0] : A.irq_state[ADSP_IRQ0]) && (m & 0x02)) ||
           (A.irq_latch[ADSP_TIMER] && (m & 0x01));
}

// The start of a pass of the run loop: take an interrupt if one is ready.
// True if one was taken (the pc is now its vector).
ADSP_SLOW bool irq_step(void)
{
    if (!irq_ready()) return false;
    check_irqs();
    return true;
}

// irq_ready() depends on these alone. Lines stay raised while masked (the
// host's command latch until it is read, a latched sample interrupt during
// another handler), so translated code remembers the last combination it
// found nothing to take in, and skips asking again until one changes.
typedef struct { uint64_t lo; uint32_t hi; uint8_t imask, icntl; } adsp_irq_seen_t;

ADSP_FN bool irq_seen(const adsp_irq_seen_t *seen)
{
    uint64_t lo;
    uint32_t hi;
#if (defined(__GNUC__) || defined(_MSC_VER)) && !defined(__TINYC__)   // MSVC: memcpy, no unaligned loads on PowerPC
    __builtin_memcpy(&lo, A.irq_all, 8);
    __builtin_memcpy(&hi, A.irq_all + 8, 4);
#else
    lo = *(const uint64_t *)A.irq_all;
    hi = *(const uint32_t *)(A.irq_all + 8);
#endif
    return lo == seen->lo && hi == seen->hi && A.imask == seen->imask && A.icntl == seen->icntl;
}

ADSP_SLOW void irq_remember(adsp_irq_seen_t *seen)
{
#if (defined(__GNUC__) || defined(_MSC_VER)) && !defined(__TINYC__)   // MSVC: memcpy, no unaligned loads on PowerPC
    __builtin_memcpy(&seen->lo, A.irq_all, 8);
    __builtin_memcpy(&seen->hi, A.irq_all + 8, 4);
#else
    seen->lo = *(const uint64_t *)A.irq_all;
    seen->hi = *(const uint32_t *)(A.irq_all + 8);
#endif
    seen->imask = A.imask;
    seen->icntl = A.icntl;
}

// Fetch and sequencing for the instruction at the current pc, as one
// iteration of the run loop does before executing it: the next pc, with
// DO UNTIL ends looping back or finishing the loop.
ADSP_FN void loop_end_step(void)
{
    if (cond(A.loop_cond)) A.pc = pc_top();
    else { loop_pop(); pc_pop(); A.pc = (A.pc + 1) & 0x3fff; }
}
ADSP_SLOW void sequence_loop_end(void) { loop_end_step(); }

ADSP_FN void sequence(void)
{
    if (A.pc != A.loop_end) A.pc = (A.pc + 1) & 0x3fff;
    else sequence_loop_end();
}

// The clocks one instruction cycle advances, after it executed.
ADSP_FN void clock_cycle(void)
{
    if (A.mstat & M_TIMER) timer_tick(1);
    if (A.tx_on && ++A.tx_cnt >= A.tx_period) { A.tx_cnt--; tx_tick(1); }
}

#endif
