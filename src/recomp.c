// Static recompiler (see recomp.h).
//
// Code is found by recursive descent from the reset entry and the exception
// vector, plus any word in RAM that points into the code (return addresses,
// function pointers, jump tables). JAL targets split the code into functions;
// each becomes one C function with a label for every address the dispatcher
// may need to enter at. Branches inside a function become gotos (backward
// ones check whether to return to the dispatcher), everything else exits.
#include "recomp.h"
#include "rec_abi.h"     // ld_le32: the snapshot is guest memory
#include "game.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x88000000u          // code is linked to run from KSEG0 RAM at phys 0x08000000
#define RAM0_BYTES 0x80000u
#define RAM2_BYTES 0x800000u
#define VECTOR 0x80000180u
#define FILE_BYTES (200 * 1024)    // split the output so it compiles in parallel
#define MAX_FILES 60
#define SLOTS (RAM2_BYTES / 4)

enum { K_NONE, K_B, K_BL, K_J, K_JAL, K_JR, K_JALR, K_ERET };

typedef struct { int kind; uint32_t tgt; bool link, has_tgt; } flow_t;

static const uint8_t *r0, *r2;

static bool word_at(uint32_t va, uint32_t *w)
{
    uint32_t pa = va & 0x1fffffffu;
    if (pa < RAM0_BYTES) { *w = ld_le32(r0 + pa); return true; }
    if (pa - 0x08000000u < RAM2_BYTES) { *w = ld_le32(r2 + pa - 0x08000000u); return true; }
    return false;
}

static uint32_t word(uint32_t va) { uint32_t w = 0; word_at(va, &w); return w; }
static int s16(uint32_t x) { return (int16_t)(x & 0xffff); }

static flow_t flow(uint32_t pc, uint32_t op)
{
    flow_t f = { K_NONE, 0, false, false };
    unsigned o = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31;
    uint32_t btgt = pc + 4 + (uint32_t)(s16(op) * 4);
    uint32_t jtgt = ((pc + 4) & 0xf0000000u) | ((op & 0x3ffffff) << 2);
#define RET(k, t, l, h) do { f.kind = k; f.tgt = t; f.link = l; f.has_tgt = h; return f; } while (0)
    if (o == 0) {
        if ((op & 63) == 0x08) RET(K_JR, 0, false, false);
        if ((op & 63) == 0x09) RET(K_JALR, 0, true, false);
        return f;
    }
    if (o == 1) {
        if (rt <= 0x01) RET(K_B, btgt, false, true);
        if (rt == 0x02 || rt == 0x03) RET(K_BL, btgt, false, true);
        if (rt == 0x10 || rt == 0x11) RET(K_B, btgt, true, true);
        if (rt == 0x12 || rt == 0x13) RET(K_BL, btgt, true, true);
        return f;
    }
    if (o == 2) RET(K_J, jtgt, false, true);
    if (o == 3) RET(K_JAL, jtgt, true, true);
    if (o >= 4 && o <= 7) {
        if (o == 4 && rs == 0 && rt == 0) RET(K_J, btgt, false, true);
        RET(K_B, btgt, false, true);
    }
    if (o >= 0x14 && o <= 0x17) RET(K_BL, btgt, false, true);
    if ((o == 0x11 || o == 0x10) && rs == 8) RET((op >> 17) & 1 ? K_BL : K_B, btgt, false, true);   // BC1 / BC0
    if (o == 0x10 && rs >= 0x10 && (op & 63) == 0x18) RET(K_ERET, 0, false, false);
    return f;
#undef RET
}

// Instructions handed to the interpreter that may need to return to the
// dispatcher afterwards (COP0: interrupts, idle loops, ERET).
static bool stops_here(uint32_t op) { return (op >> 26) == 0x10; }

/* ---------------------------------------------------------------- */
/* Discovery                                                        */
/* ---------------------------------------------------------------- */

static uint8_t *code, *leader, *call;   // per word of ram2, for addresses BASE + 4*i

static bool in_ram2(uint32_t a) { return a - BASE < RAM2_BYTES && !(a & 3); }
static void mark(uint8_t *set, uint32_t a) { if (in_ram2(a)) set[(a - BASE) >> 2] = 1; }
static bool has(const uint8_t *set, uint32_t a) { return in_ram2(a) && set[(a - BASE) >> 2]; }

typedef struct { uint32_t *v; int n, cap; } list_t;
static void push(list_t *l, uint32_t v)
{
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 1024; l->v = (uint32_t *)realloc(l->v, (size_t)l->cap * 4); }
    l->v[l->n++] = v;
}

static void descend(list_t *work, uint32_t lo, uint32_t hi)
{
    while (work->n) {
        uint32_t pc = work->v[--work->n];
        while (pc >= lo && pc < hi && !has(code, pc)) {
            uint32_t op = word(pc);
            mark(code, pc);
            flow_t f = flow(pc, op);
            if (stops_here(op)) mark(leader, pc + 4);
            if (f.kind == K_NONE) { pc += 4; continue; }
            mark(code, pc + 4);                       // delay slot
            if (f.has_tgt) { mark(leader, f.tgt); push(work, f.tgt); }
            if (f.kind == K_JAL) mark(call, f.tgt);
            if (f.link || f.kind == K_B || f.kind == K_BL) { mark(leader, pc + 8); pc += 8; continue; }
            break;                                    // j / jr / eret: no fall-through
        }
    }
}

/* ---------------------------------------------------------------- */
/* Code generation                                                  */
/* ---------------------------------------------------------------- */

typedef struct { char *s; size_t n, cap; } sb_t;

static void sbf(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int need = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? b->cap - b->n : 0, fmt, ap);
        va_end(ap);
        if (need >= 0 && b->s && b->n + (size_t)need < b->cap) { b->n += (size_t)need; return; }
        b->cap = (b->cap + (size_t)need + 1) * 2;
        b->s = (char *)realloc(b->s, b->cap);
    }
}

#define REG "R[%u]"

// C for writing `expr` to register n (nothing for r0).
static void wr(char *out, size_t n, unsigned r, const char *expr)
{
    if (r) snprintf(out, n, REG " = %s;", r, expr);
    else out[0] = 0;
}

// FPU ops are translated (KI2_RECOMP_FPU) in functions not kept plain.
static bool g_fpu;

// C for a COP1 op or FPU load/store that has a translation (helpers in
// rec_gen.h), or false: it stays with the interpreter.
static bool gen_fpu(char *out, size_t n, uint32_t op, const char *ea, const char *P, const char *D)
{
    unsigned o = op >> 26, fmt = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31;
    unsigned fn = op & 63;
    switch (o) {
    case 0x31: snprintf(out, n, "LD(uint32_t, rec_fset32(%u, _v), %s, 4, %s, %s);", ft, ea, P, D); return true;
    case 0x35: snprintf(out, n, "LD(uint64_t, rec_fset64(%u, _v), %s, 8, %s, %s);", ft, ea, P, D); return true;
    case 0x39: snprintf(out, n, "ST(uint32_t, rec_fget32(%u), %s, 4, %s, %s);", ft, ea, P, D); return true;
    case 0x3d: snprintf(out, n, "ST(uint64_t, rec_fget64(%u), %s, 8, %s, %s);", ft, ea, P, D); return true;
    case 0x11: break;
    default: return false;
    }
    if (fmt == 0x00) {                                               // MFC1
        if (ft) snprintf(out, n, REG " = SX32(rec_fget32(%u));", ft, fs); else out[0] = 0;
        return true;
    }
    if (fmt == 0x04) { snprintf(out, n, "rec_fset32(%u, (uint32_t)" REG ");", fs, ft); return true; }   // MTC1
    if (fmt == 0x10) {                                               // single
        static const char ar[4] = { '+', '-', '*', '/' };
        if (fn <= 3) snprintf(out, n, "rec_fsets(%u, rec_fgets(%u) %c rec_fgets(%u));", fd, fs, ar[fn], ft);
        else if (fn == 0x05) snprintf(out, n, "rec_fsets(%u, fabsf(rec_fgets(%u)));", fd, fs);
        else if (fn == 0x06) snprintf(out, n, "rec_fset32(%u, rec_fget32(%u));", fd, fs);
        else if (fn == 0x07) snprintf(out, n, "rec_fsets(%u, -rec_fgets(%u));", fd, fs);
        else if (fn == 0x21) snprintf(out, n, "rec_fsetd(%u, (double)rec_fgets(%u));", fd, fs);
        else if (fn == 0x24) snprintf(out, n, "rec_cvt_w_s(%u, %u);", fd, fs);
        else if (fn >= 0x30) snprintf(out, n, "rec_fcmp_s(%u, %u, %u);", fs, ft, fn & 15);
        else return false;
        return true;
    }
    if (fmt == 0x14 && fn == 0x20) { snprintf(out, n, "rec_fsets(%u, (float)(int32_t)rec_fget32(%u));", fd, fs); return true; }
    if (fmt == 0x14 && fn == 0x21) { snprintf(out, n, "rec_fsetd(%u, (double)(int32_t)rec_fget32(%u));", fd, fs); return true; }
    return false;
}

// C for one non-branch instruction; dl: it executes in a delay slot.
static void gen_op(char *out, size_t outn, uint32_t pc, uint32_t op, bool dl)
{
    unsigned o = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
    int imm = s16(op);
    unsigned uimm = op & 0xffff;
    const char *D = dl ? "true" : "false";
    char P[16], interp[96], RS[8], RT[8], ea[48], e[160];
    snprintf(P, sizeof P, "0x%08xu", pc);
    snprintf(interp, sizeof interp, "if (rec_op(0x%08xu, %s, %s)) return;", op, P, D);
    snprintf(RS, sizeof RS, REG, rs);
    snprintf(RT, sizeof RT, REG, rt);
    snprintf(ea, sizeof ea, "(uint32_t)(%s + %dll)", RS, imm);
    e[0] = 0;

    if (o == 0) {
        unsigned f = op & 63;
        switch (f) {
        case 0x00: snprintf(e, sizeof e, "SX32((uint32_t)%s << %u)", RT, sa); break;
        case 0x02: snprintf(e, sizeof e, "SX32((uint32_t)%s >> %u)", RT, sa); break;
        case 0x03: snprintf(e, sizeof e, "SX32((int32_t)%s >> %u)", RT, sa); break;
        case 0x04: snprintf(e, sizeof e, "SX32((uint32_t)%s << (%s & 31))", RT, RS); break;
        case 0x06: snprintf(e, sizeof e, "SX32((uint32_t)%s >> (%s & 31))", RT, RS); break;
        case 0x07: snprintf(e, sizeof e, "SX32((int32_t)%s >> (%s & 31))", RT, RS); break;
        case 0x10: snprintf(e, sizeof e, "cpu.hi"); break;
        case 0x12: snprintf(e, sizeof e, "cpu.lo"); break;
        case 0x14: snprintf(e, sizeof e, "%s << (%s & 63)", RT, RS); break;
        case 0x16: snprintf(e, sizeof e, "%s >> (%s & 63)", RT, RS); break;
        case 0x17: snprintf(e, sizeof e, "(uint64_t)((int64_t)%s >> (%s & 63))", RT, RS); break;
        case 0x21: snprintf(e, sizeof e, "SX32((uint32_t)%s + (uint32_t)%s)", RS, RT); break;
        case 0x23: snprintf(e, sizeof e, "SX32((uint32_t)%s - (uint32_t)%s)", RS, RT); break;
        case 0x24: snprintf(e, sizeof e, "%s & %s", RS, RT); break;
        case 0x25: snprintf(e, sizeof e, "%s | %s", RS, RT); break;
        case 0x26: snprintf(e, sizeof e, "%s ^ %s", RS, RT); break;
        case 0x27: snprintf(e, sizeof e, "~(%s | %s)", RS, RT); break;
        case 0x2a: snprintf(e, sizeof e, "(uint64_t)((int64_t)%s < (int64_t)%s)", RS, RT); break;
        case 0x2b: snprintf(e, sizeof e, "(uint64_t)(%s < %s)", RS, RT); break;
        case 0x2d: snprintf(e, sizeof e, "%s + %s", RS, RT); break;
        case 0x2f: snprintf(e, sizeof e, "%s - %s", RS, RT); break;
        case 0x38: snprintf(e, sizeof e, "%s << %u", RT, sa); break;
        case 0x3a: snprintf(e, sizeof e, "%s >> %u", RT, sa); break;
        case 0x3b: snprintf(e, sizeof e, "(uint64_t)((int64_t)%s >> %u)", RT, sa); break;
        case 0x3c: snprintf(e, sizeof e, "%s << %u", RT, sa + 32); break;
        case 0x3e: snprintf(e, sizeof e, "%s >> %u", RT, sa + 32); break;
        case 0x3f: snprintf(e, sizeof e, "(uint64_t)((int64_t)%s >> %u)", RT, sa + 32); break;
        }
        if (e[0]) { wr(out, outn, rd, e); return; }
        if (f == 0x0f) { out[0] = 0; return; }                       // SYNC
        if (f == 0x11) { snprintf(out, outn, "cpu.hi = %s;", RS); return; }
        if (f == 0x13) { snprintf(out, outn, "cpu.lo = %s;", RS); return; }
        static const char *const muldiv[8] = { "mult", "multu", "div", "divu", "dmult", "dmultu", "ddiv", "ddivu" };
        if (f >= 0x18 && f <= 0x1f) { snprintf(out, outn, "rec_%s(%s, %s);", muldiv[f - 0x18], RS, RT); return; }
        if (f == 0x20 || f == 0x22) {                                // ADD / SUB: trap on overflow
            char w[64];
            wr(w, sizeof w, rd, "SX32(_r)");
            snprintf(out, outn, "{ int32_t _r; if (__builtin_%s_overflow((int32_t)%s, (int32_t)%s, &_r)) { %s } %s }",
                     f == 0x20 ? "add" : "sub", RS, RT, interp, w);
            return;
        }
        snprintf(out, outn, "%s", interp);
        return;
    }
    if (o == 0x08) {                                                 // ADDI
        char w[64];
        wr(w, sizeof w, rt, "SX32(_r)");
        snprintf(out, outn, "{ int32_t _r; if (__builtin_add_overflow((int32_t)%s, %d, &_r)) { %s } %s }", RS, imm, interp, w);
        return;
    }
    switch (o) {
    case 0x09: snprintf(e, sizeof e, "SX32((uint32_t)%s + %uu)", RS, (uint32_t)imm); break;
    case 0x0a: snprintf(e, sizeof e, "(uint64_t)((int64_t)%s < %dll)", RS, imm); break;
    case 0x0b: snprintf(e, sizeof e, "(uint64_t)(%s < 0x%llxull)", RS, (unsigned long long)(int64_t)imm); break;
    case 0x0c: snprintf(e, sizeof e, "%s & 0x%xu", RS, uimm); break;
    case 0x0d: snprintf(e, sizeof e, "%s | 0x%xu", RS, uimm); break;
    case 0x0e: snprintf(e, sizeof e, "%s ^ 0x%xu", RS, uimm); break;
    case 0x0f: snprintf(e, sizeof e, "0x%llxull", (unsigned long long)(int64_t)(int32_t)(uimm << 16)); break;
    case 0x19: snprintf(e, sizeof e, "%s + %dll", RS, imm); break;
    }
    if (e[0]) { wr(out, outn, rt, e); return; }

    static const struct { unsigned o; const char *t; int size; const char *val; } loads[] = {
        { 0x20, "int8_t", 1, "(uint64_t)(int64_t)_v" }, { 0x21, "int16_t", 2, "(uint64_t)(int64_t)_v" },
        { 0x23, "uint32_t", 4, "SX32(_v)" }, { 0x24, "uint8_t", 1, "_v" }, { 0x25, "uint16_t", 2, "_v" },
        { 0x27, "uint32_t", 4, "_v" }, { 0x37, "uint64_t", 8, "_v" },
    };
    for (size_t i = 0; i < sizeof loads / sizeof loads[0]; i++)
        if (loads[i].o == o) {
            char dst[48];
            if (rt) snprintf(dst, sizeof dst, "%s = %s", RT, loads[i].val); else snprintf(dst, sizeof dst, "(void)_v");
            snprintf(out, outn, "LD(%s, %s, %s, %d, %s, %s);", loads[i].t, dst, ea, loads[i].size, P, D);
            return;
        }
    static const struct { unsigned o; const char *t; int size; } stores[] = {
        { 0x28, "uint8_t", 1 }, { 0x29, "uint16_t", 2 }, { 0x2b, "uint32_t", 4 }, { 0x3f, "uint64_t", 8 },
    };
    for (size_t i = 0; i < sizeof stores / sizeof stores[0]; i++)
        if (stores[i].o == o) { snprintf(out, outn, "ST(%s, %s, %s, %d, %s, %s);", stores[i].t, RT, ea, stores[i].size, P, D); return; }
    if (o == 0x2f) { out[0] = 0; return; }                          // CACHE
    const char *un = o == 0x22 ? "lwl" : o == 0x26 ? "lwr" : o == 0x1a ? "ldl" : o == 0x1b ? "ldr" : NULL;
    if (un) {
        char put[24] = "";
        if (rt) snprintf(put, sizeof put, " %s = _t;", RT);
        snprintf(out, outn, "{ uint64_t _t = %s; if (!rec_%s(&_t, %s, %s, %s)) return;%s }", RT, un, ea, P, D, put);
        return;
    }
    un = o == 0x2a ? "swl" : o == 0x2e ? "swr" : o == 0x2c ? "sdl" : o == 0x2d ? "sdr" : NULL;
    if (un) { snprintf(out, outn, "if (!rec_%s(%s, %s, %s, %s)) return;", un, RT, ea, P, D); return; }
    if (o == 0x10 && rs <= 1 && rd != 1 && rd != 13) {
        // MFC0/DMFC0 except Random and Cause (Cause polling is how idle loops are found)
        char val[96], w[128];
        if (rd == 9) snprintf(val, sizeof val, "SX32(cpu.count_base + (uint32_t)((cpu.cycles - cpu.count_cyc) / 2))");
        else if (rs == 0) snprintf(val, sizeof val, "SX32(cpu.cp0[%u])", rd);
        else snprintf(val, sizeof val, "cpu.cp0[%u]", rd);
        wr(w, sizeof w, rt, val);
        snprintf(out, outn, "if (!rec_cop0_usable()) { rec_op(0x%08xu, %s, %s); return; } %s", op, P, D, w);
        return;
    }
    char fpu[192];
    if (g_fpu && gen_fpu(fpu, sizeof fpu, op, ea, P, D)) {
        // as rec_op would: Coprocessor Unusable from the interpreter, and back
        // to the dispatcher afterwards when an interrupt is pending
        char back[48] = "";
        if (!dl) snprintf(back, sizeof back, " if (irq_dirty) EXIT(0x%08xu);", pc + 4);
        snprintf(out, outn, "if (!REC_CU1()) { %s } else { %s%s }", interp, fpu, back);
        return;
    }
    snprintf(out, outn, "%s", interp);                               // COP0/1, LL/SC, traps...
}

static void gen_cond(char *out, size_t n, uint32_t op)
{
    unsigned o = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31;
    if (o == 1) snprintf(out, n, "(int64_t)" REG " %s 0", rs, (rt == 0x00 || rt == 0x02 || rt == 0x10 || rt == 0x12) ? "<" : ">=");
    else if (o == 4 || o == 0x14) snprintf(out, n, REG " == " REG, rs, rt);
    else if (o == 5 || o == 0x15) snprintf(out, n, REG " != " REG, rs, rt);
    else if (o == 6 || o == 0x16) snprintf(out, n, "(int64_t)" REG " <= 0", rs);
    else if (o == 7 || o == 0x17) snprintf(out, n, "(int64_t)" REG " > 0", rs);
    else if (o == 0x11) snprintf(out, n, "%s((cpu.fcr31 >> 23) & 1)", (op >> 16) & 1 ? "" : "!");
    else snprintf(out, n, "%s", (op >> 16) & 1 ? "false" : "true");  // BC0
}

// Branch a whose delay slot is branch b, where b can't be taken when a is
// (opposite tests of the same registers, as in "bne x,0,L; beq x,0,L").
// Then, as the interpreter runs it: a taken jumps to its target after both
// (b in the slot does nothing), a not taken goes on to b as a plain branch.
// b must not be a "likely" or linking branch, which act even when not taken.
static bool exclusive_branches(uint32_t a, uint32_t b)
{
    unsigned oa = a >> 26, ob = b >> 26;
    unsigned sa = (a >> 21) & 31, ta = (a >> 16) & 31, sb = (b >> 21) & 31, tb = (b >> 16) & 31;
    if ((oa == 4 && ob == 5) || (oa == 5 && ob == 4))                     // beq / bne
        return (sa == sb && ta == tb) || (sa == tb && ta == sb);
    if ((oa == 6 && ob == 7) || (oa == 7 && ob == 6))                     // blez / bgtz
        return sa == sb && ta == 0 && tb == 0;
    if (oa == 1 && ob == 1)                                               // bltz / bgez
        return sa == sb && ((ta == 0 && tb == 1) || (ta == 1 && tb == 0));
    return false;
}

typedef struct { uint32_t pc; int fn; } label_t;

// Functions to translate plainly, without CALL and cached registers: those
// holding any address listed (hex, one per line) in the file KI2_RECOMP_PLAIN
// names. For code whose optimised translation misbehaves on one compiler.
static uint32_t *plain_pcs;
static int plain_n;

static void plain_load(void)
{
    static bool loaded;
    if (loaded) return;
    loaded = true;
    const char *path = getenv("KI2_RECOMP_PLAIN");
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return;
    unsigned v, cap = 0;
    while (fscanf(f, "%x", &v) == 1) {
        if ((unsigned)plain_n == cap) { cap = cap ? cap * 2 : 256; plain_pcs = (uint32_t *)realloc(plain_pcs, cap * sizeof *plain_pcs); }
        plain_pcs[plain_n++] = v;
    }
    fclose(f);
    fprintf(stderr, "recomp: %d addresses to translate plainly\n", plain_n);
}

static bool plain_in(uint32_t lo, uint32_t hi)
{
    plain_load();
    for (int i = 0; i < plain_n; i++) if (plain_pcs[i] >= lo && plain_pcs[i] < hi) return true;
    return false;
}

// KI2_RECOMP_FAST: optimised functions also take the RAM fast path first,
// keep the code image's bounds and the slice's end in locals, and read r0 as
// the constant it is (see rec_ld_ptr_fast_ in rec_gen.h). Plain functions
// get the defaults back, so they are exactly as without the option.
static bool recomp_fast(void)
{
    static int v = -1;
    if (v < 0) v = getenv("KI2_RECOMP_FAST") != NULL;
    return v != 0;
}
static const char fast_macros[] =
    "#undef REC_LD_PTR\n#undef REC_ST_PTR\n#undef REC_CODE_LO\n#undef REC_CODE_SIZE\n#undef REC_DL\n#undef REC_DL_SYNC\n"
    "#define REC_LD_PTR(a) rec_ld_ptr_fast_(a)\n#define REC_ST_PTR(a) rec_st_ptr_fast_(a)\n"
    "#define REC_CODE_LO clo_\n#define REC_CODE_SIZE csz_\n#define REC_DL dl_\n#define REC_DL_SYNC (dl_ = rec_deadline)\n";
static const char plain_macros[] =
    "#undef REC_LD_PTR\n#undef REC_ST_PTR\n#undef REC_CODE_LO\n#undef REC_CODE_SIZE\n#undef REC_DL\n#undef REC_DL_SYNC\n"
    "#define REC_LD_PTR(a) rec_ld_ptr_(a)\n#define REC_ST_PTR(a) rec_st_ptr_(a)\n"
    "#define REC_CODE_LO rec_code_lo\n#define REC_CODE_SIZE rec_code_size\n#define REC_DL rec_deadline\n#define REC_DL_SYNC ((void)0)\n";

static bool ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// Rewrite one generated function so the guest registers it uses, and the
// cycle count, live in locals (see REC_SYNC_OUT in rec_gen.h): R[n] becomes
// r_n (r0 stays in cpu, it is never written), cpu.cycles becomes cyc_, every
// return writes back what the function changes, and interpreted ops sync
// both ways. The function's text in `src` starts "void f_...(void)\n{\n
// REC_LOCALS\n".
static void cache_regs(sb_t *out, const char *src)
{
    const bool fast = recomp_fast();
    bool used[32] = { false }, written[32] = { false };
    for (const char *p = src; (p = strstr(p, "R[")) != NULL; p += 2) {
        if (p > src && ident_char(p[-1])) continue;
        unsigned n = 0;
        const char *q = p + 2;
        while (*q >= '0' && *q <= '9') n = n * 10 + (unsigned)(*q++ - '0');
        if (*q != ']' || n == 0 || n > 31) continue;
        used[n] = true;
        if (q[1] == ' ' && q[2] == '=' && q[3] != '=') written[n] = true;
    }
    // the sync macros for this function
    sbf(out, "#undef REC_CYC\n#define REC_CYC cyc_\n#undef REC_SYNC_OUT\n#undef REC_SYNC_IN\n#define REC_SYNC_OUT (");
    for (int n = 1; n < 32; n++) if (written[n]) sbf(out, "R[%d] = r_%d, ", n, n);
    sbf(out, "cpu.cycles = cyc_)\n#define REC_SYNC_IN (");
    for (int n = 1; n < 32; n++) if (used[n]) sbf(out, "r_%d = R[%d], ", n, n);
    sbf(out, "cyc_ = cpu.cycles)\n");
    if (fast) sbf(out, "%s", fast_macros);

    const char *body = strstr(src, "REC_LOCALS\n");
    body = body ? body + strlen("REC_LOCALS\n") : src;
    sbf(out, "%.*s", (int)(body - src), src);
    sbf(out, "    uint64_t cyc_ = cpu.cycles;\n    bool rec_t_;\n    (void)rec_t_;\n");
    if (fast) sbf(out, "    uint32_t clo_ = rec_code_lo, csz_ = rec_code_size;\n    uint64_t dl_ = rec_deadline;\n"
                       "    (void)clo_; (void)csz_; (void)dl_;\n");
    if (fast && getenv("KI2_RECOMP_INLINE")) sbf(out, "    uint32_t ig_ = 0;    // rec_code_gen on entering an inlined copy\n    (void)ig_;\n");
    for (int n = 1; n < 32; n++) if (used[n]) sbf(out, "    uint64_t r_%d = R[%d];\n", n, n);

    for (const char *p = body; *p;) {
        bool word_start = p == src || !ident_char(p[-1]);
        if (word_start && p[0] == 'R' && p[1] == '[') {
            unsigned n = 0;
            const char *q = p + 2;
            while (*q >= '0' && *q <= '9') n = n * 10 + (unsigned)(*q++ - '0');
            if (*q == ']' && n >= 1 && n <= 31) { sbf(out, "r_%u", n); p = q + 1; continue; }
            // r0 is always zero (nothing writes it): a constant, not a load
            if (fast && *q == ']' && n == 0 && q == p + 3) { sbf(out, "((uint64_t)0)"); p = q + 1; continue; }
        }
        if (word_start && !strncmp(p, "cpu.cycles", 10)) { sbf(out, "cyc_"); p += 10; continue; }
        if (word_start && !strncmp(p, "rec_op(", 7)) { sbf(out, "REC_OP("); p += 7; continue; }
        if (word_start && !strncmp(p, "return;", 7)) { sbf(out, "REC_RETURN;"); p += 7; continue; }
        const char *e = p + 1;                  // copy up to the next place that could match
        while (*e && *e != 'R' && *e != 'c' && *e != 'r') e++;
        sbf(out, "%.*s", (int)(e - p), p);
        p = e;
    }
}

// Every function's start (ascending) and the end of the last, for inlining.
static uint32_t *g_fn_lo;
static int g_nfn;
static uint32_t g_fn_end;
static const char *g_sfx = "";              // appended to label names while a copy is generated

// A call to `t` can take a copy of the callee instead (KI2_RECOMP_FAST): t
// starts a small function that calls nothing, returns only by "jr ra", has
// no branch in a branch's delay slot, and is not one kept plain (hardware,
// disk driver). Its bounds go to *lo, *hi.
static bool inline_ok(uint32_t t, uint32_t *lo, uint32_t *hi)
{
    static int enabled = -1;                    // KI2_RECOMP_INLINE, on top of KI2_RECOMP_FAST
    if (enabled < 0) enabled = getenv("KI2_RECOMP_INLINE") != NULL;
    if (!enabled || !recomp_fast()) return false;
    int a = 0, b = g_nfn - 1, k = -1;
    while (a <= b) {
        int m = (a + b) / 2;
        if (g_fn_lo[m] == t) { k = m; break; }
        if (g_fn_lo[m] < t) a = m + 1; else b = m - 1;
    }
    if (k < 0) return false;
    *lo = t;
    *hi = k + 1 < g_nfn ? g_fn_lo[k + 1] : g_fn_end;
    int n = (int)((*hi - *lo) / 4);
    if (n > 48 || plain_in(*lo, *hi)) return false;
    for (int i = 0; i < n; i++) {
        uint32_t pc = *lo + 4u * (uint32_t)i, op = word(pc);
        flow_t f = flow(pc, op), g = flow(pc + 4, word(pc + 4));
        if (f.kind == K_JAL || f.kind == K_JALR || f.kind == K_ERET) return false;
        if (f.kind == K_JR && ((op >> 21) & 31) != 31) return false;
        if (f.kind && g.kind && !(f.kind == K_B && !f.link && g.kind == K_B && !g.link && f.has_tgt &&
                                  exclusive_branches(op, word(pc + 4)))) return false;
    }
    return true;
}

// C for the MIPS function occupying [lo, hi); its entry labels are appended to `labels`.
// With `sfx`, a copy for inlining instead: no header or entry labels, label
// names end in sfx, "jr ra" returns to the caller at inl_ret, and the raw text
// goes straight into the caller's buffer (final_out).
static void gen_function(sb_t *final_out, uint32_t lo, uint32_t hi, label_t **labels, int *nlabels, int *labcap, int fi,
                         bool plain, const char *sfx = NULL, uint32_t inl_ret = 0)
{
    struct { uint32_t t, lo, hi, ret; } pend[64];  // callees to copy in after this function's own code
    int npend = 0;
    sb_t raw = { 0 };                           // the plain text, rewritten by cache_regs at the end
    sb_t *out = sfx ? final_out : &raw;
    const char *saved_sfx = g_sfx;
    g_sfx = sfx ? sfx : "";
    static int fpu_on = -1;
    if (fpu_on < 0) fpu_on = getenv("KI2_RECOMP_FPU") != NULL;
    const bool saved_fpu = g_fpu;
    g_fpu = fpu_on && !plain;
    int n = (int)((hi - lo) / 4);
    flow_t *fl = (flow_t *)malloc(sizeof *fl * (size_t)(n + 1));
    uint8_t *lset = (uint8_t *)calloc((size_t)n + 1, 1), *bad = (uint8_t *)calloc((size_t)n + 1, 1);
    for (int i = 0; i <= n; i++) fl[i] = flow(lo + 4u * (uint32_t)i, word(lo + 4u * (uint32_t)i));
    // Computed jumps (jr through anything but ra) usually land in unrolled
    // code at an offset worked out at run time: make every address enterable.
    bool computed = false;
    for (int i = 0; i < n; i++)
        if (fl[i].kind == K_JR && ((word(lo + 4u * (uint32_t)i) >> 21) & 31) != 31) computed = true;
    for (int i = 0; i < n; i++) lset[i] = computed || i == 0 || has(leader, lo + 4u * (uint32_t)i);
    // A branch whose delay slot is another branch is left to the interpreter,
    // unless the two exclude each other (pair[i]: exclusive_branches).
    uint8_t *pair = (uint8_t *)calloc((size_t)n + 1, 1);
    for (int i = 0; i < n; i++) {
        uint32_t a = word(lo + 4u * (uint32_t)i), b = word(lo + 4u * (uint32_t)(i + 1));
        pair[i] = i + 1 < n && fl[i].kind == K_B && !fl[i].link && fl[i + 1].kind == K_B && !fl[i + 1].link &&
                  fl[i].has_tgt && exclusive_branches(a, b);
        bad[i] = fl[i].kind && fl[i + 1].kind && !pair[i];
    }
    for (int i = 0; i < n && !sfx; i++)
        if (lset[i] && !bad[i]) {
            if (*nlabels == *labcap) { *labcap = *labcap ? *labcap * 2 : 4096; *labels = (label_t *)realloc(*labels, sizeof **labels * (size_t)*labcap); }
            label_t lb = { lo + 4u * (uint32_t)i, fi };
            (*labels)[(*nlabels)++] = lb;
        }
    // When a delay slot is also a label its code is emitted on its own, so the
    // not-taken path of the branch needs a label to jump over it.
    uint8_t *lab = (uint8_t *)calloc((size_t)n + 1, 1);
    memcpy(lab, lset, (size_t)n);
    for (int i = 0; i + 2 < n; i++)
        if ((fl[i].kind == K_B || fl[i].kind == K_BL) && lset[i + 1]) lab[i + 2] = 1;

#define IDX(pc) ((int)(((pc) - lo) / 4))
#define IN(pc) ((pc) >= lo && (pc) < hi)
    if (!sfx) {
        sbf(out, "void f_%08x(void)\n{\n    REC_LOCALS\n    switch (cpu.pc) {\n", lo);
        for (int i = 0; i < n; i++)
            if (lset[i] && !bad[i]) sbf(out, "    case 0x%08xu: goto L_%08x;\n", lo + 4u * (uint32_t)i, lo + 4u * (uint32_t)i);
        sbf(out, "    default: return;\n    }\n");
    }

    char op1[512], slot[512], cond[64], jmp[96], tail[64];
    uint32_t pc = lo;
    bool seg_start = true;
    while (pc < hi) {
        int i = IDX(pc);
        if (lab[i] || seg_start) {
            // segment length: up to the next label, or through the next branch and its delay slot
            uint32_t k = pc;
            int len;
            for (;;) {
                if (k >= hi) { len = (int)((k - pc) / 4); break; }
                int j = IDX(k);
                if ((k != pc && lab[j]) || bad[j]) { len = (int)((k - pc) / 4); break; }
                if (pair[j]) { len = (int)((k - pc) / 4) + 1; break; }      // the second branch counts itself
                if (fl[j].kind) { len = (int)((k - pc) / 4) + 2; break; }
                k += 4;
            }
            if (len) sbf(out, "L_%08x%s: cpu.cycles += %d;\n", pc, g_sfx, len);
            else sbf(out, "L_%08x%s:\n", pc, g_sfx);
            seg_start = false;
        }
        uint32_t op = word(pc);
        flow_t f = fl[i];
        if (bad[i]) { sbf(out, "    EXIT(0x%08xu);\n", pc); pc += 4; seg_start = true; continue; }
        if (pair[i]) {
            // taken: the second branch runs in the slot, never taken; else on to it
            uint32_t t = f.tgt;
            gen_cond(cond, sizeof cond, op);
            if (IN(t) && !(t & 3) && lab[IDX(t)] && !bad[IDX(t)])
                sbf(out, "    if (%s) { cpu.cycles += 1; if (STOP()) EXIT(0x%08xu); goto L_%08x%s; }\n", cond, t, t, g_sfx);
            else
                sbf(out, "    if (%s) { cpu.cycles += 1; EXIT(0x%08xu); }\n", cond, t);
            pc += 4;
            seg_start = true;
            continue;
        }
        if (f.kind == K_NONE) { gen_op(op1, sizeof op1, pc, op, false); sbf(out, "    %s\n", op1); pc += 4; continue; }

        uint32_t slot_pc = pc + 4;
        gen_op(slot, sizeof slot, slot_pc, word(slot_pc), true);
        unsigned rs = (op >> 21) & 31, rd = (op >> 11) & 31;
        if (f.kind == K_ERET) {
            gen_op(op1, sizeof op1, pc, op, false);
            sbf(out, "    %s\n    EXIT(0x%08xu);\n", op1, pc + 4);           // not reached: ERET always returns
            pc += 4;
            seg_start = true;
            continue;
        }
        // target of a static branch: goto inside this function (backward ones may stop), else exit
        if (f.has_tgt) {
            uint32_t t = f.tgt;
            if (IN(t) && !(t & 3) && lab[IDX(t)] && !bad[IDX(t)]) {
                if (t <= pc) snprintf(jmp, sizeof jmp, "if (STOP()) EXIT(0x%08xu); goto L_%08x%s;", t, t, g_sfx);
                else snprintf(jmp, sizeof jmp, "goto L_%08x%s;", t, g_sfx);
            } else snprintf(jmp, sizeof jmp, "EXIT(0x%08xu);", t);
        }
        // a call whose return address is a label here: run the callee directly (CALL)
        uint32_t ret = pc + 8;
        bool ret_here = !plain && !sfx && IN(ret) && lab[IDX(ret)] && !bad[IDX(ret)];
        uint32_t ilo, ihi;
        if (f.kind == K_JR && sfx && rs == 31) {
            // the copy's "jr ra": back into the caller
            sbf(out, "    { uint32_t _t = (uint32_t)R[31]; %s INLINE_RET(_t, 0x%08xu, L_%08x); }\n", slot, inl_ret, inl_ret);
        } else if (f.kind == K_JR || f.kind == K_JALR) {
            sbf(out, "    { uint32_t _t = (uint32_t)" REG "; ", rs);
            if (f.kind == K_JALR && rd) sbf(out, REG " = SX32(0x%08xu); ", rd, ret);
            if (f.kind == K_JALR && rd == 31 && ret_here) sbf(out, "%s CALL(_t, 0x%08xu, L_%08x); }\n", slot, ret, ret);
            else sbf(out, "%s EXIT(_t); }\n", slot);
        } else if (f.kind == K_JAL) {
            if (ret_here && f.tgt != lo && npend < 64 && inline_ok(f.tgt, &ilo, &ihi)) {
                pend[npend].t = f.tgt; pend[npend].lo = ilo; pend[npend].hi = ihi; pend[npend].ret = ret;
                sbf(out, "    R[31] = SX32(0x%08xu); %s INLINE_ENTER(0x%08xu, L_%08x_i%d);\n", ret, slot, f.tgt, f.tgt, npend);
                npend++;
            } else if (ret_here) sbf(out, "    R[31] = SX32(0x%08xu); %s CALL(0x%08xu, 0x%08xu, L_%08x);\n", ret, slot, f.tgt, ret, ret);
            else sbf(out, "    R[31] = SX32(0x%08xu); %s EXIT(0x%08xu);\n", ret, slot, f.tgt);
        } else if (f.kind == K_J) {
            sbf(out, "    { %s %s }\n", slot, jmp);
        } else {
            gen_cond(cond, sizeof cond, op);
            tail[0] = 0;
            if (f.link) snprintf(tail, sizeof tail, "R[31] = SX32(0x%08xu); ", pc + 8);
            if (f.kind == K_B) sbf(out, "    { bool _c = %s; %s%s if (_c) { %s } }\n", cond, tail, slot, jmp);
            else sbf(out, "    { bool _c = %s; %sif (_c) { %s %s } else cpu.cycles--; }\n", cond, tail, slot, jmp);
        }
        // fall-through (not taken) continues after the delay slot
        uint32_t nxt = pc + 8;
        if (f.kind == K_B || f.kind == K_BL) {
            if (lab[IDX(slot_pc)] && nxt < hi) sbf(out, "    goto L_%08x%s;\n", nxt, g_sfx);
            else if (nxt >= hi) sbf(out, "    EXIT(0x%08xu);\n", nxt);
        }
        pc = (slot_pc < hi && lab[IDX(slot_pc)]) ? slot_pc : nxt;
        seg_start = true;
    }
    sbf(out, "    EXIT(0x%08xu);\n", hi);
    // the copies of small callees, reached only from their INLINE_ENTER
    for (int k = 0; k < npend; k++) {
        char s[16];
        snprintf(s, sizeof s, "_i%d", k);
        sbf(out, "    // copy of f_%08x, returning to %08x\n", pend[k].t, pend[k].ret);
        gen_function(out, pend[k].lo, pend[k].hi, labels, nlabels, labcap, fi, false, s, pend[k].ret);
    }
    g_sfx = saved_sfx;
    g_fpu = saved_fpu;
#undef IDX
#undef IN
    free(fl); free(lset); free(bad); free(lab); free(pair);
    if (sfx) return;                            // a copy: the caller closes and rewrites the whole text
    sbf(out, "}\n\n");
    if (plain) {
        // as translated before CALL and cached registers: cpu.r[] and cpu.cycles directly
        sbf(final_out, "#undef REC_SYNC_OUT\n#undef REC_SYNC_IN\n#define REC_SYNC_OUT ((void)0)\n#define REC_SYNC_IN ((void)0)\n"
                       "#undef REC_CYC\n#define REC_CYC cpu.cycles\n%s%s", recomp_fast() ? plain_macros : "", raw.s);
    } else {
        cache_regs(final_out, raw.s);
    }
    free(raw.s);
}

static int cmp_label(const void *a, const void *b)
{
    const label_t *x = (const label_t *)a, *y = (const label_t *)b;
    if (x->pc != y->pc) return x->pc < y->pc ? -1 : 1;
    return x->fn - y->fn;
}

static bool write_text(const char *dir, const char *name, const sb_t *b, char *err, size_t errlen)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(b->s, 1, b->n, f) != b->n) {
        snprintf(err, errlen, "cannot write %s", path);
        if (f) fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

bool recomp_generate(const uint8_t *snap0, const uint8_t *snap2, const char *outdir,
                     recomp_info_t *info, char *err, size_t errlen)
{
    r0 = snap0; r2 = snap2;
    code = (uint8_t *)calloc(SLOTS, 1); leader = (uint8_t *)calloc(SLOTS, 1); call = (uint8_t *)calloc(SLOTS, 1);
    list_t work = { 0 };
    bool ok = true;

    mark(leader, BASE);
    push(&work, BASE);
    // the general exception vector jumps into the game's handler
    for (uint32_t pc = VECTOR; pc < VECTOR + 0x40; pc += 4) {
        flow_t f = flow(pc, word(pc));
        if (f.has_tgt && (f.tgt & 0xff000000u) == (BASE & 0xff000000u)) { mark(leader, f.tgt); push(&work, f.tgt); }
    }
    descend(&work, BASE, BASE + RAM2_BYTES);
    uint32_t end = BASE;
    for (uint32_t i = 0; i < SLOTS; i++) if (code[i]) end = BASE + 4 * i + 4;
    // Addresses the code builds in a register (lui + addiu/ori) are entry
    // points too: code reached only through a pointer stored at run time
    // (KI1 picks its span renderers per frame) is otherwise never found. A
    // false match only adds a label.
    for (uint32_t pc = BASE; pc < end; pc += 4) {
        uint32_t op = word(pc);
        if (op >> 26 != 0x0f) continue;                              // lui
        unsigned rt = (op >> 16) & 31;
        for (uint32_t q = pc + 4; q < pc + 32 && q < end; q += 4) {
            uint32_t o2 = word(q), k = o2 >> 26;
            if ((k != 0x09 && k != 0x0d) || ((o2 >> 21) & 31) != rt) continue;   // addiu / ori from it
            uint32_t a = k == 0x09 ? (op << 16) + (uint32_t)(int32_t)(int16_t)o2 : (op << 16) | (o2 & 0xffff);
            if (a >= BASE && a < end && !(a & 3)) { mark(leader, a); push(&work, a); }
            break;
        }
    }
    // Pointers into the code found anywhere in RAM (return addresses, function
    // pointers, jump tables) become entry points. They may not extend the
    // region: past its end lies data, and stores there would invalidate it.
    for (int bank = 0; bank < 2; bank++) {
        const uint8_t *m = bank ? snap2 : snap0;
        size_t len = bank ? RAM2_BYTES : RAM0_BYTES;
        for (size_t i = 0; i < len; i += 4) {
            uint32_t w = ld_le32(m + i);
            if (w >= BASE && w < end && !(w & 3)) { mark(leader, w); push(&work, w); }
        }
    }
    descend(&work, BASE, end);
    uint32_t nwords = (end - BASE) / 4 + 1;                          // +1: delay slot of the last word

    // functions: from each call target to the next
    list_t starts = { 0 };
    for (uint32_t i = 0; i < (end - BASE) / 4; i++)
        if (i == 0 || call[i]) push(&starts, BASE + 4 * i);
    for (uint32_t i = 0; i < SLOTS; i++)                             // only leaders inside the region count
        if (BASE + 4 * i >= end) leader[i] = 0;

    g_fn_lo = starts.v; g_nfn = starts.n; g_fn_end = end;     // for inline_ok
    label_t *labels = NULL;
    int nlabels = 0, labcap = 0, nfiles = 0;
    sb_t body = { 0 };
    for (int fi = 0; fi < starts.n && ok; fi++) {
        if (body.n == 0) {
            sbf(&body, "// Translated from the game's own code by the recompiler. Do not edit or share.\n#include \"rec_gen.h\"\n"
                       "#undef REC_CYC\n#define REC_CYC cyc_      // every function keeps the cycle count in a local\n\n");
        }
        uint32_t lo = starts.v[fi], hi = fi + 1 < starts.n ? starts.v[fi + 1] : end;
        gen_function(&body, lo, hi, &labels, &nlabels, &labcap, fi, plain_in(lo, hi));
        if ((body.n > FILE_BYTES && nfiles < MAX_FILES - 1) || fi == starts.n - 1) {
            char name[32];
            snprintf(name, sizeof name, "rec_%03d.c", nfiles++);
            ok = write_text(outdir, name, &body, err, errlen);
            body.n = 0;
        }
    }
    qsort(labels, (size_t)nlabels, sizeof *labels, cmp_label);

    // image hash first: it goes into the module description
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t j = 0; j < nwords; j++) {
        uint32_t w = word(BASE + 4 * j);
        for (int k = 0; k < 4; k++) { h ^= (w >> (8 * k)) & 0xff; h *= 0x100000001b3ull; }
    }
    sb_t tab = { 0 };
    sbf(&tab, "// Translated from the game's own code by the recompiler. Do not edit or share.\n#include \"rec_gen.h\"\n\n");
    for (int fi = 0; fi < starts.n; fi++) sbf(&tab, "void f_%08x(void);\n", starts.v[fi]);
    sbf(&tab, "\nstatic const rec_func_t funcs[] = {\n");
    for (int fi = 0; fi < starts.n; fi++) {
        uint32_t lo = starts.v[fi], hi = fi + 1 < starts.n ? starts.v[fi + 1] : end;
        uint32_t vend = hi + 4 < BASE + nwords * 4 ? hi + 4 : BASE + nwords * 4;   // through the last delay slot
        sbf(&tab, "    { f_%08x, 0x%08x, 0x%08x },\n", lo, lo, vend);
    }
    sbf(&tab, "};\n\nstatic const rec_label_t labels[] = {\n");
    for (int i = 0; i < nlabels; i++) sbf(&tab, "    { 0x%08x, %d },\n", labels[i].pc, labels[i].fn);
    sbf(&tab, "};\n\nstatic const uint32_t image[] = {\n");
    for (uint32_t i = 0; i < nwords; i += 8) {
        sbf(&tab, "    ");
        for (uint32_t j = i; j < nwords && j < i + 8; j++) sbf(&tab, j > i ? ", 0x%08x" : "0x%08x", word(BASE + 4 * j));
        sbf(&tab, ",\n");
    }
    sbf(&tab, "};\n\n"
              "#ifdef REC_MODULE\n"
              "const rec_host_t *rec_host_;\ncpu_t *rec_cpu_;\nuint8_t *rec_ram_, *rec_dirty_;\n"
              "#endif\n\n"
              "// the sound DSP's code (rec_dsp.c)\n"
              "extern const rec_dsp_image_t rec_dsp_images_[];\n"
              "extern const uint32_t rec_dsp_nimages_, rec_dsp_sizeof_adsp_;\n"
              "void rec_dsp_bind_(const rec_host_t *h);\n\n"
              "static rec_module_t module = {\n"
              "    REC_ABI_VERSION, sizeof(cpu_t), sizeof(tlbc_t), funcs, labels, %d, %d,\n"
              "    0x%08xu, %uu, image, 0x%016llxull, \"%s\",\n"
              "};\n\n"
              "REC_EXPORT const rec_module_t *rec_module_init(const rec_host_t *host)\n{\n"
              "    if (host->abi != REC_ABI_VERSION || host->sizeof_cpu != sizeof(cpu_t) || host->sizeof_tlbc != sizeof(tlbc_t))\n"
              "        return 0;\n"
              "    rec_bind_(host);\n"
              "    rec_dsp_bind_(host);\n"
              "    module.dsp = rec_dsp_images_;\n"
              "    module.ndsp = rec_dsp_nimages_;\n"
              "    module.sizeof_adsp = rec_dsp_sizeof_adsp_;\n"
              "    return &module;\n}\n",
        starts.n, nlabels, BASE, nwords, (unsigned long long)h, game.id);
    if (ok) ok = write_text(outdir, "rec_tab.c", &tab, err, errlen);

    if (info) {
        uint32_t reached = 0;
        for (uint32_t i = 0; i < SLOTS; i++) reached += code[i];
        recomp_info_t in = { starts.n, nlabels, nfiles, end, h };
        *info = in;
        fprintf(stderr, "recomp: %u KB of code at %08x-%08x, %d functions, %d entry points, %u words reached by descent\n",
                (end - BASE) / 1024, BASE, end, starts.n, nlabels, reached);
    }
    free(body.s); free(tab.s); free(labels); free(work.v); free(starts.v);
    free(code); free(leader); free(call);
    return ok;
}
