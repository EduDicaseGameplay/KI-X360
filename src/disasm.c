// Minimal MIPS III disassembler for tracing.
#include "cpu.h"
#include <stdio.h>

static const char *rn[32] = {
    "zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"
};

static const char *op_main[64] = {
    0,0,"j","jal","beq","bne","blez","bgtz","addi","addiu","slti","sltiu","andi","ori","xori","lui",
    0,0,"cop2",0,"beql","bnel","blezl","bgtzl","daddi","daddiu","ldl","ldr",0,0,0,0,
    "lb","lh","lwl","lw","lbu","lhu","lwr","lwu","sb","sh","swl","sw","sdl","sdr","swr","cache",
    "ll","lwc1",0,0,"lld","ldc1",0,"ld","sc","swc1",0,0,"scd","sdc1",0,"sd"
};

static const char *op_special[64] = {
    "sll",0,"srl","sra","sllv",0,"srlv","srav","jr","jalr",0,0,"syscall","break",0,"sync",
    "mfhi","mthi","mflo","mtlo","dsllv",0,"dsrlv","dsrav","mult","multu","div","divu","dmult","dmultu","ddiv","ddivu",
    "add","addu","sub","subu","and","or","xor","nor",0,0,"slt","sltu","dadd","daddu","dsub","dsubu",
    "tge","tgeu","tlt","tltu","teq",0,"tne",0,"dsll",0,"dsrl","dsra","dsll32",0,"dsrl32","dsra32"
};

static const char *op_regimm[32] = {
    "bltz","bgez","bltzl","bgezl",0,0,0,0,"tgei","tgeiu","tlti","tltiu","teqi",0,"tnei",0,
    "bltzal","bgezal","bltzall","bgezall",0,0,0,0,0,0,0,0,0,0,0,0
};

const char *cpu_disasm(uint32_t pc, uint32_t op, char *buf)
{
    int rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
    int16_t imm = (int16_t)op;
    int o = op >> 26;
    if (op == 0) { sprintf(buf, "nop"); return buf; }
    switch (o) {
    case 0: {
        const char *m = op_special[op & 63];
        if (!m) break;
        int f = op & 63;
        if (f < 4 || (f >= 0x38)) sprintf(buf, "%s %s,%s,%d", m, rn[rd], rn[rt], sa);
        else if (f == 8) sprintf(buf, "jr %s", rn[rs]);
        else if (f == 9) sprintf(buf, "jalr %s,%s", rn[rd], rn[rs]);
        else if (f >= 0x10 && f <= 0x13) sprintf(buf, "%s %s", m, rn[(f & 1) ? rs : rd]);
        else if (f >= 0x18 && f <= 0x1f) sprintf(buf, "%s %s,%s", m, rn[rs], rn[rt]);
        else if (f == 4 || f == 6 || f == 7 || (f >= 0x14 && f <= 0x17)) sprintf(buf, "%s %s,%s,%s", m, rn[rd], rn[rt], rn[rs]);
        else sprintf(buf, "%s %s,%s,%s", m, rn[rd], rn[rs], rn[rt]);
        return buf;
    }
    case 1:
        if (!op_regimm[rt]) break;
        sprintf(buf, "%s %s,%08x", op_regimm[rt], rn[rs], pc + 4 + (imm << 2));
        return buf;
    case 2: case 3:
        sprintf(buf, "%s %08x", op_main[o], ((pc + 4) & 0xf0000000u) | ((op & 0x3ffffff) << 2));
        return buf;
    case 0x10: case 0x11: {
        static const char *c0[] = {"mfc","dmfc","cfc",0,"mtc","dmtc","ctc",0};
        if (rs < 8 && c0[rs]) { sprintf(buf, "%s%d %s,$%d", c0[rs], o & 1, rn[rt], rd); return buf; }
        if (rs == 8) { sprintf(buf, "bc%d%s%s %08x", o & 1, (rt & 1) ? "t" : "f", (rt & 2) ? "l" : "", pc + 4 + (imm << 2)); return buf; }
        if (o == 0x10) {
            switch (op & 63) {
            case 1: return sprintf(buf, "tlbr"), buf;
            case 2: return sprintf(buf, "tlbwi"), buf;
            case 6: return sprintf(buf, "tlbwr"), buf;
            case 8: return sprintf(buf, "tlbp"), buf;
            case 0x18: return sprintf(buf, "eret"), buf;
            }
        }
        sprintf(buf, "cop%d %07x", o & 1, op & 0x1ffffff);
        return buf;
    }
    }
    const char *m = op_main[o];
    if (!m) { sprintf(buf, "??? %08x", op); return buf; }
    if (o >= 4 && o <= 7) sprintf(buf, "%s %s,%s,%08x", m, rn[rs], rn[rt], pc + 4 + (imm << 2));
    else if (o >= 0x14 && o <= 0x17) sprintf(buf, "%s %s,%s,%08x", m, rn[rs], rn[rt], pc + 4 + (imm << 2));
    else if (o == 0x0f) sprintf(buf, "lui %s,0x%04x", rn[rt], (uint16_t)imm);
    else if (o >= 0x0c && o <= 0x0e) sprintf(buf, "%s %s,%s,0x%04x", m, rn[rt], rn[rs], (uint16_t)imm);
    else if (o < 0x1a) sprintf(buf, "%s %s,%s,%d", m, rn[rt], rn[rs], imm);
    else if (o == 0x31 || o == 0x35 || o == 0x39 || o == 0x3d) sprintf(buf, "%s $f%d,%d(%s)", m, rt, imm, rn[rs]);
    else sprintf(buf, "%s %s,%d(%s)", m, rn[rt], imm, rn[rs]);
    return buf;
}
