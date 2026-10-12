// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Roberto Fresca, Grull Osgo
// thanks-to:Marcel De Kogel
/*
 * Intel 8080 / 8085A CPU core, ported from MAME's i8085.cpp ("Portable I8085A
 * emulator V1.3", Juergen Buchmueller). See i8085.h for what was left out.
 */
#include "i8085.h"

#include <string.h>

#include "phx_port.h"

/* Every helper below is forced inline: left to the compiler, rd() and
 * op_push() were emitted as separate functions in flash, and every emulated
 * memory access branched out of the SRAM-resident execute_one(). */
#define I8085_INLINE static inline __attribute__((always_inline))

#define SF  0x80
#define ZF  0x40
#define KF  0x20
#define HF  0x10
#define X3F 0x08
#define PF  0x04
#define VF  0x02
#define CF  0x01

#define IM_SID 0x80
#define IM_I75 0x40
#define IM_I65 0x20
#define IM_I55 0x10
#define IM_IE  0x08
#define IM_M75 0x04
#define IM_M65 0x02
#define IM_M55 0x01

/* Not const: read on every instruction, so they belong in RAM, not flash. */
static uint8_t lut_cycles_8080[256] = {
    /*      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  */
    /* 0 */ 4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
    /* 1 */ 4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
    /* 2 */ 4, 10, 16, 5, 5, 5, 7, 4, 4, 10, 16, 5, 5, 5, 7, 4,
    /* 3 */ 4, 10, 13, 5, 10, 10, 10, 4, 4, 10, 13, 5, 5, 5, 7, 4,
    /* 4 */ 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    /* 5 */ 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    /* 6 */ 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    /* 7 */ 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5,
    /* 8 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 9 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* A */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* B */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* C */ 5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 11, 7, 11,
    /* D */ 5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 11, 7, 11,
    /* E */ 5, 10, 10, 18, 11, 11, 7, 11, 5, 5, 10, 4, 11, 11, 7, 11,
    /* F */ 5, 10, 10, 4, 11, 11, 7, 11, 5, 5, 10, 4, 11, 11, 7, 11};

static uint8_t lut_cycles_8085[256] = {
    /*      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  */
    /* 0 */ 4, 10, 7, 6, 4, 4, 7, 4, 10, 10, 7, 6, 4, 4, 7, 4,
    /* 1 */ 7, 10, 7, 6, 4, 4, 7, 4, 10, 10, 7, 6, 4, 4, 7, 4,
    /* 2 */ 4, 10, 16, 6, 4, 4, 7, 4, 10, 10, 16, 6, 4, 4, 7, 4,
    /* 3 */ 4, 10, 13, 6, 10, 10, 10, 4, 10, 10, 13, 6, 4, 4, 7, 4,
    /* 4 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 5 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 6 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 7 */ 7, 7, 7, 7, 7, 7, 5, 7, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 8 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* 9 */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* A */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* B */ 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    /* C */ 6, 10, 7, 7, 9, 12, 7, 12, 6, 10, 7, 6, 9, 9, 7, 12,
    /* D */ 6, 10, 7, 10, 9, 12, 7, 12, 6, 10, 7, 10, 9, 7, 7, 12,
    /* E */ 6, 10, 7, 16, 9, 12, 7, 12, 6, 6, 7, 4, 9, 10, 7, 12,
    /* F */ 6, 10, 7, 4, 9, 12, 7, 12, 6, 6, 7, 4, 9, 7, 7, 12};

/* Zero/sign and zero/sign/parity flags per result byte. */
static uint8_t lut_zs[256];
static uint8_t lut_zsp[256];
static int luts_ready;

static void init_luts(void)
{
    for (int i = 0; i < 256; i++)
    {
        uint8_t zs = 0;
        if (i == 0)
            zs |= ZF;
        if (i & 0x80)
            zs |= SF;
        int bits = 0;
        for (int b = 0; b < 8; b++)
            bits += (i >> b) & 1;
        lut_zs[i] = zs;
        lut_zsp[i] = zs | ((bits & 1) ? 0 : PF);
    }
    luts_ready = 1;
}

void i8085_init(i8085_t *cpu, int is_8085)
{
    if (!luts_ready)
        init_luts();
    memset(cpu, 0, sizeof(*cpu));
    cpu->is_8085 = is_8085 ? 1 : 0;
    cpu->cycles = is_8085 ? lut_cycles_8085 : lut_cycles_8080;
}

void i8085_reset(i8085_t *cpu)
{
    cpu->pc.w = 0;
    cpu->halt = 0;
    cpu->im &= ~(IM_I75 | IM_IE);
    cpu->im |= IM_M55 | IM_M65 | IM_M75;
}

/* -------------------------------------------------------------------------
 * Memory access
 * ---------------------------------------------------------------------- */

I8085_INLINE uint8_t rd(i8085_t *c, uint16_t a)
{
    const uint8_t *p = c->rd_page[a >> 8];
    return p ? p[a & 0xff] : c->read(c, a);
}

I8085_INLINE void wr(i8085_t *c, uint16_t a, uint8_t v)
{
    uint8_t *p = c->wr_page[a >> 8];
    if (p)
        p[a & 0xff] = v;
    else
        c->write(c, a, v);
}

I8085_INLINE uint8_t read_arg(i8085_t *c)
{
    return rd(c, c->pc.w++);
}

I8085_INLINE i8085_pair_t read_arg16(i8085_t *c)
{
    i8085_pair_t p;
    p.b.l = rd(c, c->pc.w++);
    p.b.h = rd(c, c->pc.w++);
    return p;
}

I8085_INLINE void op_push(i8085_t *c, i8085_pair_t p)
{
    wr(c, --c->sp.w, p.b.h);
    wr(c, --c->sp.w, p.b.l);
}

I8085_INLINE i8085_pair_t op_pop(i8085_t *c)
{
    i8085_pair_t p;
    p.b.l = rd(c, c->sp.w++);
    p.b.h = rd(c, c->sp.w++);
    return p;
}

/* -------------------------------------------------------------------------
 * ALU helpers (A = af.b.h, F = af.b.l)
 * ---------------------------------------------------------------------- */

#define A (c->af.b.h)
#define F (c->af.b.l)

I8085_INLINE void op_ora(i8085_t *c, uint8_t v)
{
    A |= v;
    F = lut_zsp[A];
}

I8085_INLINE void op_xra(i8085_t *c, uint8_t v)
{
    A ^= v;
    F = lut_zsp[A];
}

I8085_INLINE void op_ana(i8085_t *c, uint8_t v)
{
    uint8_t hc = ((A | v) << 1) & HF;
    A &= v;
    F = lut_zsp[A];
    F |= c->is_8085 ? HF : hc;
}

I8085_INLINE uint8_t op_inr(i8085_t *c, uint8_t v)
{
    uint8_t hc = ((v & 0x0f) == 0x0f) ? HF : 0;
    F = (F & CF) | lut_zsp[(uint8_t)(++v)] | hc;
    return v;
}

I8085_INLINE uint8_t op_dcr(i8085_t *c, uint8_t v)
{
    uint8_t hc = ((v & 0x0f) != 0x00) ? HF : 0;
    F = (F & CF) | lut_zsp[(uint8_t)(--v)] | hc | VF;
    return v;
}

I8085_INLINE void op_add(i8085_t *c, uint8_t v)
{
    int q = A + v;
    F = lut_zsp[q & 0xff] | ((q >> 8) & CF) | ((A ^ q ^ v) & HF);
    A = q;
}

I8085_INLINE void op_adc(i8085_t *c, uint8_t v)
{
    int q = A + v + (F & CF);
    F = lut_zsp[q & 0xff] | ((q >> 8) & CF) | ((A ^ q ^ v) & HF);
    A = q;
}

I8085_INLINE void op_sub(i8085_t *c, uint8_t v)
{
    int q = A - v;
    F = lut_zsp[q & 0xff] | ((q >> 8) & CF) | (~(A ^ q ^ v) & HF) | VF;
    A = q;
}

I8085_INLINE void op_sbb(i8085_t *c, uint8_t v)
{
    int q = A - v - (F & CF);
    F = lut_zsp[q & 0xff] | ((q >> 8) & CF) | (~(A ^ q ^ v) & HF) | VF;
    A = q;
}

I8085_INLINE void op_cmp(i8085_t *c, uint8_t v)
{
    int q = A - v;
    F = lut_zsp[q & 0xff] | ((q >> 8) & CF) | (~(A ^ q ^ v) & HF) | VF;
}

I8085_INLINE void op_dad(i8085_t *c, uint16_t v)
{
    int q = c->hl.w + v;
    F = (F & ~CF) | ((q >> 16) & CF);
    c->hl.w = q;
}

/* Extra cycles when a conditional jump/call/return is taken. */
#define JMP_TAKEN() (c->is_8085 ? 3 : 0)
#define CALL_TAKEN() (c->is_8085 ? 9 : 6)
#define RET_TAKEN() 6

I8085_INLINE void op_jmp(i8085_t *c, int cond)
{
    if (cond)
    {
        c->pc = read_arg16(c);
        c->icount -= JMP_TAKEN();
    }
    else
    {
        c->pc.w += 2;
    }
}

I8085_INLINE void op_call(i8085_t *c, int cond)
{
    if (cond)
    {
        i8085_pair_t p = read_arg16(c);
        c->icount -= CALL_TAKEN();
        op_push(c, c->pc);
        c->pc = p;
    }
    else
    {
        c->pc.w += 2;
    }
}

I8085_INLINE void op_ret(i8085_t *c, int cond)
{
    if (cond)
    {
        c->icount -= RET_TAKEN();
        c->pc = op_pop(c);
    }
}

I8085_INLINE void op_rst(i8085_t *c, uint8_t v)
{
    op_push(c, c->pc);
    c->pc.w = 8 * v;
}

/* INX/DCX on the 8085 set the undocumented K flag on wrap. */
#define INX(rp)                                                \
    do                                                         \
    {                                                          \
        (rp).w++;                                              \
        if (c->is_8085)                                        \
            F = ((rp).w == 0x0000) ? (F | KF) : (F & ~KF);     \
    } while (0)
#define DCX(rp)                                                \
    do                                                         \
    {                                                          \
        (rp).w--;                                              \
        if (c->is_8085)                                        \
            F = ((rp).w == 0xffff) ? (F | KF) : (F & ~KF);     \
    } while (0)

/* -------------------------------------------------------------------------
 * Execution
 * ---------------------------------------------------------------------- */

static void PHX_HOT(execute_one)(i8085_t *c, int opcode)
{
    c->icount -= c->cycles[opcode];

    switch (opcode)
    {
    case 0x00: /* NOP */
        break;
    case 0x01: /* LXI B,nnnn */
        c->bc = read_arg16(c);
        break;
    case 0x02: /* STAX B */
        wr(c, c->bc.w, A);
        break;
    case 0x03: /* INX B */
        INX(c->bc);
        break;
    case 0x04: /* INR B */
        c->bc.b.h = op_inr(c, c->bc.b.h);
        break;
    case 0x05: /* DCR B */
        c->bc.b.h = op_dcr(c, c->bc.b.h);
        break;
    case 0x06: /* MVI B,nn */
        c->bc.b.h = read_arg(c);
        break;
    case 0x07: /* RLC */
        A = (A << 1) | (A >> 7);
        F = (F & 0xfe) | (A & CF);
        break;

    case 0x08: /* 8085: undocumented DSUB, otherwise undocumented NOP */
        if (c->is_8085)
        {
            int q_low = c->hl.b.l - c->bc.b.l;
            uint8_t res_low = q_low & 0xff;
            F = lut_zs[res_low] | ((q_low >> 8) & CF) | ((c->hl.b.l ^ res_low ^ c->bc.b.l) & HF) |
                (((c->bc.b.l ^ c->hl.b.l) & (c->hl.b.l ^ res_low) & SF) >> 5);
            c->hl.b.l = res_low;

            int q_high = c->hl.b.h - c->bc.b.h - (F & CF);
            uint8_t res_high = q_high & 0xff;
            F = lut_zs[res_high] | ((q_high >> 8) & CF) | ((c->hl.b.h ^ res_high ^ c->bc.b.h) & HF) |
                (((c->bc.b.h ^ c->hl.b.h) & (c->hl.b.h ^ res_high) & SF) >> 5);
            c->hl.b.h = res_high;

            F = (F & ~ZF) | (((c->hl.b.l | c->hl.b.h) == 0) ? ZF : 0);
        }
        break;
    case 0x09: /* DAD B */
        op_dad(c, c->bc.w);
        break;
    case 0x0a: /* LDAX B */
        A = rd(c, c->bc.w);
        break;
    case 0x0b: /* DCX B */
        DCX(c->bc);
        break;
    case 0x0c: /* INR C */
        c->bc.b.l = op_inr(c, c->bc.b.l);
        break;
    case 0x0d: /* DCR C */
        c->bc.b.l = op_dcr(c, c->bc.b.l);
        break;
    case 0x0e: /* MVI C,nn */
        c->bc.b.l = read_arg(c);
        break;
    case 0x0f: /* RRC */
        F = (F & 0xfe) | (A & CF);
        A = (A >> 1) | (A << 7);
        break;

    case 0x10: /* 8085: undocumented ARHL, otherwise undocumented NOP */
        if (c->is_8085)
        {
            F = (F & ~CF) | (c->hl.b.l & CF);
            c->hl.w = (c->hl.w & 0x8000) | (c->hl.w >> 1);
        }
        break;
    case 0x11: /* LXI D,nnnn */
        c->de = read_arg16(c);
        break;
    case 0x12: /* STAX D */
        wr(c, c->de.w, A);
        break;
    case 0x13: /* INX D */
        INX(c->de);
        break;
    case 0x14: /* INR D */
        c->de.b.h = op_inr(c, c->de.b.h);
        break;
    case 0x15: /* DCR D */
        c->de.b.h = op_dcr(c, c->de.b.h);
        break;
    case 0x16: /* MVI D,nn */
        c->de.b.h = read_arg(c);
        break;
    case 0x17: /* RAL */
    {
        int cy = F & CF;
        F = (F & 0xfe) | (A >> 7);
        A = (A << 1) | cy;
        break;
    }

    case 0x18: /* 8085: undocumented RDEL, otherwise undocumented NOP */
        if (c->is_8085)
        {
            int cy = F & CF;
            F = (F & ~(CF | VF)) | (c->de.b.h >> 7);
            c->de.w = (c->de.w << 1) | cy;
            if ((((c->de.w >> 15) ^ F) & CF) != 0)
                F |= VF;
        }
        break;
    case 0x19: /* DAD D */
        op_dad(c, c->de.w);
        break;
    case 0x1a: /* LDAX D */
        A = rd(c, c->de.w);
        break;
    case 0x1b: /* DCX D */
        DCX(c->de);
        break;
    case 0x1c: /* INR E */
        c->de.b.l = op_inr(c, c->de.b.l);
        break;
    case 0x1d: /* DCR E */
        c->de.b.l = op_dcr(c, c->de.b.l);
        break;
    case 0x1e: /* MVI E,nn */
        c->de.b.l = read_arg(c);
        break;
    case 0x1f: /* RAR */
    {
        int cy = (F & CF) << 7;
        F = (F & 0xfe) | (A & CF);
        A = (A >> 1) | cy;
        break;
    }

    case 0x20: /* 8085: RIM, otherwise undocumented NOP */
        if (c->is_8085)
            A = c->im & ~(IM_SID | IM_I65 | IM_I55); /* no SID, no live RST lines */
        break;
    case 0x21: /* LXI H,nnnn */
        c->hl = read_arg16(c);
        break;
    case 0x22: /* SHLD nnnn */
        c->wz = read_arg16(c);
        wr(c, c->wz.w, c->hl.b.l);
        c->wz.w++;
        wr(c, c->wz.w, c->hl.b.h);
        break;
    case 0x23: /* INX H */
        INX(c->hl);
        break;
    case 0x24: /* INR H */
        c->hl.b.h = op_inr(c, c->hl.b.h);
        break;
    case 0x25: /* DCR H */
        c->hl.b.h = op_dcr(c, c->hl.b.h);
        break;
    case 0x26: /* MVI H,nn */
        c->hl.b.h = read_arg(c);
        break;
    case 0x27: /* DAA */
        c->wz.b.h = A;
        if ((F & HF) || ((A & 0xf) > 9))
            c->wz.b.h += 6;
        if ((F & CF) || (A > 0x99))
            c->wz.b.h += 0x60;
        F = (F & 0x23) | ((A > 0x99) ? 1 : 0) | ((A ^ c->wz.b.h) & 0x10) | lut_zsp[c->wz.b.h];
        A = c->wz.b.h;
        break;

    case 0x28: /* 8085: undocumented LDHI nn, otherwise undocumented NOP */
        if (c->is_8085)
        {
            c->wz.w = read_arg(c);
            c->de.w = c->hl.w + c->wz.w;
        }
        break;
    case 0x29: /* DAD H */
        op_dad(c, c->hl.w);
        break;
    case 0x2a: /* LHLD nnnn */
        c->wz = read_arg16(c);
        c->hl.b.l = rd(c, c->wz.w);
        c->wz.w++;
        c->hl.b.h = rd(c, c->wz.w);
        break;
    case 0x2b: /* DCX H */
        DCX(c->hl);
        break;
    case 0x2c: /* INR L */
        c->hl.b.l = op_inr(c, c->hl.b.l);
        break;
    case 0x2d: /* DCR L */
        c->hl.b.l = op_dcr(c, c->hl.b.l);
        break;
    case 0x2e: /* MVI L,nn */
        c->hl.b.l = read_arg(c);
        break;
    case 0x2f: /* CMA */
        A ^= 0xff;
        if (c->is_8085)
            F |= VF;
        break;

    case 0x30: /* 8085: SIM, otherwise undocumented NOP */
        if (c->is_8085)
        {
            if (A & 0x08)
            {
                c->im &= ~(IM_M55 | IM_M65 | IM_M75 | IM_I55 | IM_I65);
                c->im |= A & (IM_M55 | IM_M65 | IM_M75);
            }
            if (A & 0x10)
                c->im &= ~IM_I75;
            /* SOD (bit 6/7) is not connected on Phoenix */
        }
        break;
    case 0x31: /* LXI SP,nnnn */
        c->sp = read_arg16(c);
        break;
    case 0x32: /* STA nnnn */
        c->wz = read_arg16(c);
        wr(c, c->wz.w, A);
        break;
    case 0x33: /* INX SP */
        INX(c->sp);
        break;
    case 0x34: /* INR M */
        c->wz.b.l = op_inr(c, rd(c, c->hl.w));
        wr(c, c->hl.w, c->wz.b.l);
        break;
    case 0x35: /* DCR M */
        c->wz.b.l = op_dcr(c, rd(c, c->hl.w));
        wr(c, c->hl.w, c->wz.b.l);
        break;
    case 0x36: /* MVI M,nn */
        c->wz.b.l = read_arg(c);
        wr(c, c->hl.w, c->wz.b.l);
        break;
    case 0x37: /* STC */
        F = (F & 0xfe) | CF;
        break;

    case 0x38: /* 8085: undocumented LDSI nn, otherwise undocumented NOP */
        if (c->is_8085)
        {
            c->wz.w = read_arg(c);
            c->de.w = c->sp.w + c->wz.w;
        }
        break;
    case 0x39: /* DAD SP */
        op_dad(c, c->sp.w);
        break;
    case 0x3a: /* LDA nnnn */
        c->wz = read_arg16(c);
        A = rd(c, c->wz.w);
        break;
    case 0x3b: /* DCX SP */
        DCX(c->sp);
        break;
    case 0x3c: /* INR A */
        A = op_inr(c, A);
        break;
    case 0x3d: /* DCR A */
        A = op_dcr(c, A);
        break;
    case 0x3e: /* MVI A,nn */
        A = read_arg(c);
        break;
    case 0x3f: /* CMC */
        F = (F & 0xfe) | (~F & CF);
        break;

    /* MOV [B/C/D/E/H/L/M/A],[B/C/D/E/H/L/M/A] */
    case 0x40: break;
    case 0x41: c->bc.b.h = c->bc.b.l; break;
    case 0x42: c->bc.b.h = c->de.b.h; break;
    case 0x43: c->bc.b.h = c->de.b.l; break;
    case 0x44: c->bc.b.h = c->hl.b.h; break;
    case 0x45: c->bc.b.h = c->hl.b.l; break;
    case 0x46: c->bc.b.h = rd(c, c->hl.w); break;
    case 0x47: c->bc.b.h = A; break;

    case 0x48: c->bc.b.l = c->bc.b.h; break;
    case 0x49: break;
    case 0x4a: c->bc.b.l = c->de.b.h; break;
    case 0x4b: c->bc.b.l = c->de.b.l; break;
    case 0x4c: c->bc.b.l = c->hl.b.h; break;
    case 0x4d: c->bc.b.l = c->hl.b.l; break;
    case 0x4e: c->bc.b.l = rd(c, c->hl.w); break;
    case 0x4f: c->bc.b.l = A; break;

    case 0x50: c->de.b.h = c->bc.b.h; break;
    case 0x51: c->de.b.h = c->bc.b.l; break;
    case 0x52: break;
    case 0x53: c->de.b.h = c->de.b.l; break;
    case 0x54: c->de.b.h = c->hl.b.h; break;
    case 0x55: c->de.b.h = c->hl.b.l; break;
    case 0x56: c->de.b.h = rd(c, c->hl.w); break;
    case 0x57: c->de.b.h = A; break;

    case 0x58: c->de.b.l = c->bc.b.h; break;
    case 0x59: c->de.b.l = c->bc.b.l; break;
    case 0x5a: c->de.b.l = c->de.b.h; break;
    case 0x5b: break;
    case 0x5c: c->de.b.l = c->hl.b.h; break;
    case 0x5d: c->de.b.l = c->hl.b.l; break;
    case 0x5e: c->de.b.l = rd(c, c->hl.w); break;
    case 0x5f: c->de.b.l = A; break;

    case 0x60: c->hl.b.h = c->bc.b.h; break;
    case 0x61: c->hl.b.h = c->bc.b.l; break;
    case 0x62: c->hl.b.h = c->de.b.h; break;
    case 0x63: c->hl.b.h = c->de.b.l; break;
    case 0x64: break;
    case 0x65: c->hl.b.h = c->hl.b.l; break;
    case 0x66: c->hl.b.h = rd(c, c->hl.w); break;
    case 0x67: c->hl.b.h = A; break;

    case 0x68: c->hl.b.l = c->bc.b.h; break;
    case 0x69: c->hl.b.l = c->bc.b.l; break;
    case 0x6a: c->hl.b.l = c->de.b.h; break;
    case 0x6b: c->hl.b.l = c->de.b.l; break;
    case 0x6c: c->hl.b.l = c->hl.b.h; break;
    case 0x6d: break;
    case 0x6e: c->hl.b.l = rd(c, c->hl.w); break;
    case 0x6f: c->hl.b.l = A; break;

    case 0x70: wr(c, c->hl.w, c->bc.b.h); break;
    case 0x71: wr(c, c->hl.w, c->bc.b.l); break;
    case 0x72: wr(c, c->hl.w, c->de.b.h); break;
    case 0x73: wr(c, c->hl.w, c->de.b.l); break;
    case 0x74: wr(c, c->hl.w, c->hl.b.h); break;
    case 0x75: wr(c, c->hl.w, c->hl.b.l); break;
    case 0x76: /* HLT (instead of MOV M,M) */
        c->pc.w--;
        c->halt = 1;
        break;
    case 0x77: wr(c, c->hl.w, A); break;

    case 0x78: A = c->bc.b.h; break;
    case 0x79: A = c->bc.b.l; break;
    case 0x7a: A = c->de.b.h; break;
    case 0x7b: A = c->de.b.l; break;
    case 0x7c: A = c->hl.b.h; break;
    case 0x7d: A = c->hl.b.l; break;
    case 0x7e: A = rd(c, c->hl.w); break;
    case 0x7f: break;

    /* alu op [B/C/D/E/H/L/M/A] */
    case 0x80: op_add(c, c->bc.b.h); break;
    case 0x81: op_add(c, c->bc.b.l); break;
    case 0x82: op_add(c, c->de.b.h); break;
    case 0x83: op_add(c, c->de.b.l); break;
    case 0x84: op_add(c, c->hl.b.h); break;
    case 0x85: op_add(c, c->hl.b.l); break;
    case 0x86: c->wz.b.l = rd(c, c->hl.w); op_add(c, c->wz.b.l); break;
    case 0x87: op_add(c, A); break;

    case 0x88: op_adc(c, c->bc.b.h); break;
    case 0x89: op_adc(c, c->bc.b.l); break;
    case 0x8a: op_adc(c, c->de.b.h); break;
    case 0x8b: op_adc(c, c->de.b.l); break;
    case 0x8c: op_adc(c, c->hl.b.h); break;
    case 0x8d: op_adc(c, c->hl.b.l); break;
    case 0x8e: c->wz.b.l = rd(c, c->hl.w); op_adc(c, c->wz.b.l); break;
    case 0x8f: op_adc(c, A); break;

    case 0x90: op_sub(c, c->bc.b.h); break;
    case 0x91: op_sub(c, c->bc.b.l); break;
    case 0x92: op_sub(c, c->de.b.h); break;
    case 0x93: op_sub(c, c->de.b.l); break;
    case 0x94: op_sub(c, c->hl.b.h); break;
    case 0x95: op_sub(c, c->hl.b.l); break;
    case 0x96: c->wz.b.l = rd(c, c->hl.w); op_sub(c, c->wz.b.l); break;
    case 0x97: op_sub(c, A); break;

    case 0x98: op_sbb(c, c->bc.b.h); break;
    case 0x99: op_sbb(c, c->bc.b.l); break;
    case 0x9a: op_sbb(c, c->de.b.h); break;
    case 0x9b: op_sbb(c, c->de.b.l); break;
    case 0x9c: op_sbb(c, c->hl.b.h); break;
    case 0x9d: op_sbb(c, c->hl.b.l); break;
    case 0x9e: c->wz.b.l = rd(c, c->hl.w); op_sbb(c, c->wz.b.l); break;
    case 0x9f: op_sbb(c, A); break;

    case 0xa0: op_ana(c, c->bc.b.h); break;
    case 0xa1: op_ana(c, c->bc.b.l); break;
    case 0xa2: op_ana(c, c->de.b.h); break;
    case 0xa3: op_ana(c, c->de.b.l); break;
    case 0xa4: op_ana(c, c->hl.b.h); break;
    case 0xa5: op_ana(c, c->hl.b.l); break;
    case 0xa6: c->wz.b.l = rd(c, c->hl.w); op_ana(c, c->wz.b.l); break;
    case 0xa7: op_ana(c, A); break;

    case 0xa8: op_xra(c, c->bc.b.h); break;
    case 0xa9: op_xra(c, c->bc.b.l); break;
    case 0xaa: op_xra(c, c->de.b.h); break;
    case 0xab: op_xra(c, c->de.b.l); break;
    case 0xac: op_xra(c, c->hl.b.h); break;
    case 0xad: op_xra(c, c->hl.b.l); break;
    case 0xae: c->wz.b.l = rd(c, c->hl.w); op_xra(c, c->wz.b.l); break;
    case 0xaf: op_xra(c, A); break;

    case 0xb0: op_ora(c, c->bc.b.h); break;
    case 0xb1: op_ora(c, c->bc.b.l); break;
    case 0xb2: op_ora(c, c->de.b.h); break;
    case 0xb3: op_ora(c, c->de.b.l); break;
    case 0xb4: op_ora(c, c->hl.b.h); break;
    case 0xb5: op_ora(c, c->hl.b.l); break;
    case 0xb6: c->wz.b.l = rd(c, c->hl.w); op_ora(c, c->wz.b.l); break;
    case 0xb7: op_ora(c, A); break;

    case 0xb8: op_cmp(c, c->bc.b.h); break;
    case 0xb9: op_cmp(c, c->bc.b.l); break;
    case 0xba: op_cmp(c, c->de.b.h); break;
    case 0xbb: op_cmp(c, c->de.b.l); break;
    case 0xbc: op_cmp(c, c->hl.b.h); break;
    case 0xbd: op_cmp(c, c->hl.b.l); break;
    case 0xbe: c->wz.b.l = rd(c, c->hl.w); op_cmp(c, c->wz.b.l); break;
    case 0xbf: op_cmp(c, A); break;

    case 0xc0: /* RNZ */
        op_ret(c, !(F & ZF));
        break;
    case 0xc1: /* POP B */
        c->bc = op_pop(c);
        break;
    case 0xc2: /* JNZ nnnn */
        op_jmp(c, !(F & ZF));
        break;
    case 0xc3: /* JMP nnnn */
        op_jmp(c, 1);
        break;
    case 0xc4: /* CNZ nnnn */
        op_call(c, !(F & ZF));
        break;
    case 0xc5: /* PUSH B */
        op_push(c, c->bc);
        break;
    case 0xc6: /* ADI nn */
        c->wz.b.l = read_arg(c);
        op_add(c, c->wz.b.l);
        break;
    case 0xc7: /* RST 0 */
        op_rst(c, 0);
        break;

    case 0xc8: /* RZ */
        op_ret(c, F & ZF);
        break;
    case 0xc9: /* RET */
        c->pc = op_pop(c);
        break;
    case 0xca: /* JZ nnnn */
        op_jmp(c, F & ZF);
        break;
    case 0xcb: /* 8085: undocumented RSTV, otherwise undocumented JMP nnnn */
        if (c->is_8085)
        {
            if (F & VF)
            {
                c->icount -= RET_TAKEN();
                op_rst(c, 8);
            }
        }
        else
            op_jmp(c, 1);
        break;
    case 0xcc: /* CZ nnnn */
        op_call(c, F & ZF);
        break;
    case 0xcd: /* CALL nnnn */
        op_call(c, 1);
        break;
    case 0xce: /* ACI nn */
        c->wz.b.l = read_arg(c);
        op_adc(c, c->wz.b.l);
        break;
    case 0xcf: /* RST 1 */
        op_rst(c, 1);
        break;

    case 0xd0: /* RNC */
        op_ret(c, !(F & CF));
        break;
    case 0xd1: /* POP D */
        c->de = op_pop(c);
        break;
    case 0xd2: /* JNC nnnn */
        op_jmp(c, !(F & CF));
        break;
    case 0xd3: /* OUT nn */
        c->wz.w = read_arg(c);
        c->out(c, c->wz.b.l, A);
        break;
    case 0xd4: /* CNC nnnn */
        op_call(c, !(F & CF));
        break;
    case 0xd5: /* PUSH D */
        op_push(c, c->de);
        break;
    case 0xd6: /* SUI nn */
        c->wz.b.l = read_arg(c);
        op_sub(c, c->wz.b.l);
        break;
    case 0xd7: /* RST 2 */
        op_rst(c, 2);
        break;

    case 0xd8: /* RC */
        op_ret(c, F & CF);
        break;
    case 0xd9: /* 8085: undocumented SHLX, otherwise undocumented RET */
        if (c->is_8085)
        {
            c->wz.w = c->de.w;
            wr(c, c->wz.w, c->hl.b.l);
            c->wz.w++;
            wr(c, c->wz.w, c->hl.b.h);
        }
        else
            c->pc = op_pop(c);
        break;
    case 0xda: /* JC nnnn */
        op_jmp(c, F & CF);
        break;
    case 0xdb: /* IN nn */
        c->wz.w = read_arg(c);
        A = c->in(c, c->wz.b.l);
        break;
    case 0xdc: /* CC nnnn */
        op_call(c, F & CF);
        break;
    case 0xdd: /* 8085: undocumented JNX5 nnnn, otherwise undocumented CALL nnnn */
        if (c->is_8085)
            op_jmp(c, !(F & KF));
        else
            op_call(c, 1);
        break;
    case 0xde: /* SBI nn */
        c->wz.b.l = read_arg(c);
        op_sbb(c, c->wz.b.l);
        break;
    case 0xdf: /* RST 3 */
        op_rst(c, 3);
        break;

    case 0xe0: /* RPO */
        op_ret(c, !(F & PF));
        break;
    case 0xe1: /* POP H */
        c->hl = op_pop(c);
        break;
    case 0xe2: /* JPO nnnn */
        op_jmp(c, !(F & PF));
        break;
    case 0xe3: /* XTHL */
        c->wz = op_pop(c);
        op_push(c, c->hl);
        c->hl.w = c->wz.w;
        break;
    case 0xe4: /* CPO nnnn */
        op_call(c, !(F & PF));
        break;
    case 0xe5: /* PUSH H */
        op_push(c, c->hl);
        break;
    case 0xe6: /* ANI nn */
        c->wz.b.l = read_arg(c);
        op_ana(c, c->wz.b.l);
        break;
    case 0xe7: /* RST 4 */
        op_rst(c, 4);
        break;

    case 0xe8: /* RPE */
        op_ret(c, F & PF);
        break;
    case 0xe9: /* PCHL */
        c->pc.w = c->hl.w;
        break;
    case 0xea: /* JPE nnnn */
        op_jmp(c, F & PF);
        break;
    case 0xeb: /* XCHG */
        c->wz.w = c->de.w;
        c->de.w = c->hl.w;
        c->hl.w = c->wz.w;
        break;
    case 0xec: /* CPE nnnn */
        op_call(c, F & PF);
        break;
    case 0xed: /* 8085: undocumented LHLX, otherwise undocumented CALL nnnn */
        if (c->is_8085)
        {
            c->wz.w = c->de.w;
            c->hl.b.l = rd(c, c->wz.w);
            c->wz.w++;
            c->hl.b.h = rd(c, c->wz.w);
        }
        else
            op_call(c, 1);
        break;
    case 0xee: /* XRI nn */
        c->wz.b.l = read_arg(c);
        op_xra(c, c->wz.b.l);
        break;
    case 0xef: /* RST 5 */
        op_rst(c, 5);
        break;

    case 0xf0: /* RP */
        op_ret(c, !(F & SF));
        break;
    case 0xf1: /* POP PSW */
        c->af = op_pop(c);
        break;
    case 0xf2: /* JP nnnn */
        op_jmp(c, !(F & SF));
        break;
    case 0xf3: /* DI */
        c->im &= ~IM_IE;
        break;
    case 0xf4: /* CP nnnn */
        op_call(c, !(F & SF));
        break;
    case 0xf5: /* PUSH PSW */
        /* X3F is always 0, and on 8080, VF=1 and KF=0 */
        F &= ~X3F;
        if (!c->is_8085)
            F = (F & ~KF) | VF;
        op_push(c, c->af);
        break;
    case 0xf6: /* ORI nn */
        c->wz.b.l = read_arg(c);
        op_ora(c, c->wz.b.l);
        break;
    case 0xf7: /* RST 6 */
        op_rst(c, 6);
        break;

    case 0xf8: /* RM */
        op_ret(c, F & SF);
        break;
    case 0xf9: /* SPHL */
        c->sp.w = c->hl.w;
        break;
    case 0xfa: /* JM nnnn */
        op_jmp(c, F & SF);
        break;
    case 0xfb: /* EI */
        c->im |= IM_IE;
        break;
    case 0xfc: /* CM nnnn */
        op_call(c, F & SF);
        break;
    case 0xfd: /* 8085: undocumented JX5 nnnn, otherwise undocumented CALL nnnn */
        if (c->is_8085)
            op_jmp(c, F & KF);
        else
            op_call(c, 1);
        break;
    case 0xfe: /* CPI nn */
        c->wz.b.l = read_arg(c);
        op_cmp(c, c->wz.b.l);
        break;
    case 0xff: /* RST 7 */
        op_rst(c, 7);
        break;
    }
}

void PHX_HOT(i8085_run)(i8085_t *cpu, int cycles)
{
    cpu->icount += cycles;
    while (cpu->icount > 0)
        execute_one(cpu, rd(cpu, cpu->pc.w++));
}
