// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/*
 * Zilog Z80 CPU core, ported from MAME's src/devices/cpu/z80/z80.cpp. See
 * z80.h. The instruction loop is z80_ops.h, generated from z80.lst.
 */
#include "z80.h"

#include <stddef.h>

#include "gal_port.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;

/* NMOS LD A,I / LD A,R parity quirk, disabled as in MAME */
#define HAS_LDAIR_QUIRK 0

/* bus timing of a standard Z80 */
#define Z80_M1_CYCLES 4
#define Z80_MREQ_CYCLES 3
#define Z80_IORQ_CYCLES 4

/* service attention bits: work the instruction boundary has to look at */
#define SA_BUSREQ 0
#define SA_NMI_PENDING 1
#define SA_IRQ_ON 2
#define SA_HALT 3
#define SA_AFTER_EI 4
#define SA_AFTER_LDAIR 5
#define SET_SA(bit, state) \
    ((state) ? (cpu->service_attention |= (1u << (bit))) : (cpu->service_attention &= ~(1u << (bit))))
#define GET_SA(bit) ((cpu->service_attention >> (bit)) & 1)

/* flags */
#define CF 0x01
#define NF 0x02
#define PF 0x04
#define VF PF
#define HF 0x10
#define YXF 0x28
#define ZF 0x40
#define SF 0x80

/* registers, as in MAME's z80.inc */
#define PRVPC cpu->prvpc.w
#define PC cpu->pc.w
#define SP cpu->sp.w
#define Q cpu->f.q
#define QT cpu->f.qtemp
#define I cpu->i
#define R cpu->r
#define R2 cpu->r2
#define AF cpu->af.w
#define A cpu->af.b.h
#define F cpu->af.b.l
#define BC cpu->bc.w
#define B cpu->bc.b.h
#define C cpu->bc.b.l
#define DE cpu->de.w
#define D cpu->de.b.h
#define E cpu->de.b.l
#define HL cpu->hl.w
#define H cpu->hl.b.h
#define L cpu->hl.b.l
#define IX cpu->ix.w
#define HX cpu->ix.b.h
#define LX cpu->ix.b.l
#define IY cpu->iy.w
#define HY cpu->iy.b.h
#define LY cpu->iy.b.l
#define WZ cpu->wz.w
#define WZ_H cpu->wz.b.h
#define WZ_L cpu->wz.b.l
#define TDAT cpu->shared_data.w
#define TDAT2 cpu->shared_data2.w
#define TDAT_H cpu->shared_data.b.h
#define TDAT_L cpu->shared_data.b.l
#define TDAT8 cpu->shared_data.b.l

/* the flag accessors of MAME's m_f */
#define F_S() (cpu->f.s_val & 0x80)
#define F_Z() (cpu->f.z_val ? 0 : 0x40)
#define F_YX() (cpu->f.yx_val & 0x28)
#define F_H() (cpu->f.h_val & 0x10)
#define F_PV() flag_pv(cpu->f.pv_val)

#define swap(a, b)               \
    do                           \
    {                            \
        __typeof__(a) t_ = (a);  \
        (a) = (b);               \
        (b) = t_;                \
    } while (0)

#define Z80_INLINE static inline __attribute__((always_inline))

/* pv_val holds a value whose parity decides P/V (or 0/1 for overflow) */
Z80_INLINE u8 flag_pv(u8 val)
{
    val ^= val >> 4;
    val ^= val << 2;
    val ^= val >> 1;
    return ~val & 0x04;
}

/* ---------------------------------------------------------------------------
 * Memory and I/O
 * ------------------------------------------------------------------------ */

Z80_INLINE u8 mem_rd(z80_t *cpu, u16 addr)
{
    const u8 *p = cpu->rd_page[addr >> 8];
    return p ? p[addr & 0xff] : cpu->read(cpu, addr);
}

/* An opcode fetch: from the decrypted opcodes when the game has any there */
Z80_INLINE u8 op_rd(z80_t *cpu, u16 addr)
{
    if (cpu->op_page)
    {
        const u8 *p = cpu->op_page[addr >> 8];
        if (p)
            return p[addr & 0xff];
    }
    return mem_rd(cpu, addr);
}

Z80_INLINE void mem_wr(z80_t *cpu, u16 addr, u8 data)
{
    u8 *p = cpu->wr_page[addr >> 8];
    if (p)
        p[addr & 0xff] = data;
    else
        cpu->write(cpu, addr, data);
}

#define data_read(addr) mem_rd(cpu, (addr))
#define data_write(addr, value) mem_wr(cpu, (addr), (value))
#define stack_read(addr) mem_rd(cpu, (addr))
#define stack_write(addr, value) mem_wr(cpu, (addr), (value))
#define opcode_read() op_rd(cpu, PC)
#define arg_read() mem_rd(cpu, PC)
#define io_read(port) cpu->in(cpu, (port))
#define io_write(port, value) cpu->out(cpu, (port), (value))

/* ---------------------------------------------------------------------------
 * Flag helpers (for eg. POP/PUSH AF, EX AF,AF')
 * ------------------------------------------------------------------------ */

uint8_t GAL_HOT(z80_get_f)(const z80_t *cpu)
{
    u8 f = 0;
    f |= F_S();
    f |= F_Z();
    f |= F_YX();
    f |= F_H();
    f |= F_PV();
    f |= cpu->f.n ? NF : 0;
    f |= cpu->f.c ? CF : 0;
    return f;
}

Z80_INLINE void z_set_f(z80_t *cpu, u8 f)
{
    cpu->f.s_val = f;
    cpu->f.z_val = !(f & ZF);
    cpu->f.yx_val = f;
    cpu->f.h_val = f;
    cpu->f.pv_val = !(f & PF);
    cpu->f.n = (f & NF) != 0;
    cpu->f.c = (f & CF) != 0;
}

/* ---------------------------------------------------------------------------
 * Halt
 * ------------------------------------------------------------------------ */

Z80_INLINE void z_halt(z80_t *cpu)
{
    if (!cpu->halt)
    {
        cpu->halt = 1;
        SET_SA(SA_HALT, 1);
    }
}

Z80_INLINE void z_leave_halt(z80_t *cpu)
{
    if (cpu->halt)
    {
        cpu->halt = 0;
        SET_SA(SA_HALT, 0);
    }
}

/* ---------------------------------------------------------------------------
 * ALU
 * ------------------------------------------------------------------------ */

Z80_INLINE void z_inc(z80_t *cpu, u8 *r)
{
    ++*r;
    QT = 0;
    /* keep C */
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = *r;
    cpu->f.pv_val = *r != 0x80;
    cpu->f.h_val = (*r & 0x0f) == 0x00 ? HF : 0;
    cpu->f.n = 0;
}

Z80_INLINE void z_dec(z80_t *cpu, u8 *r)
{
    --*r;
    QT = 0;
    /* keep C */
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = *r;
    cpu->f.pv_val = *r != 0x7f;
    cpu->f.h_val = (*r & 0x0f) == 0x0f ? HF : 0;
    cpu->f.n = 1;
}

Z80_INLINE void z_rlca(z80_t *cpu)
{
    A = (A << 1) | (A >> 7);
    QT = 0;
    /* keep SZP */
    cpu->f.yx_val = A;
    cpu->f.h_val = cpu->f.n = 0;
    cpu->f.c = A & 0x01;
}

Z80_INLINE void z_rrca(z80_t *cpu)
{
    const u8 a0 = A;
    A = (a0 >> 1) | (a0 << 7);
    QT = 0;
    /* keep SZP */
    cpu->f.yx_val = A;
    cpu->f.h_val = cpu->f.n = 0;
    cpu->f.c = a0 & 0x01;
}

Z80_INLINE void z_rla(z80_t *cpu)
{
    u8 res = (A << 1) + cpu->f.c;
    QT = 0;
    /* keep SZP */
    cpu->f.yx_val = res;
    cpu->f.h_val = cpu->f.n = 0;
    cpu->f.c = (A & 0x80) != 0;
    A = res;
}

Z80_INLINE void z_rra(z80_t *cpu)
{
    u8 res = (cpu->f.c << 7) | (A >> 1);
    QT = 0;
    /* keep SZP */
    cpu->f.yx_val = res;
    cpu->f.h_val = cpu->f.n = 0;
    cpu->f.c = A & 0x01;
    A = res;
}

Z80_INLINE void z_add_a(z80_t *cpu, u8 value)
{
    const u16 res = A + value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = (u8)res;
    cpu->f.c = (res & 0x100) != 0;
    cpu->f.h_val = (A & 0x0f) + (value & 0x0f);
    cpu->f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
    cpu->f.n = 0;
    A = (u8)res;
}

Z80_INLINE void z_adc_a(z80_t *cpu, u8 value)
{
    const int c = cpu->f.c;
    const u16 res = A + value + c;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = (u8)res;
    cpu->f.c = (res & 0x100) != 0;
    cpu->f.h_val = (A & 0x0f) + (value & 0x0f) + c;
    cpu->f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
    cpu->f.n = 0;
    A = (u8)res;
}

Z80_INLINE void z_sub_a(z80_t *cpu, u8 value)
{
    const u16 res = A - value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = (u8)res;
    cpu->f.c = (res & 0x100) != 0;
    cpu->f.h_val = (A & 0x0f) - (value & 0x0f);
    cpu->f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
    cpu->f.n = 1;
    A = (u8)res;
}

Z80_INLINE void z_sbc_a(z80_t *cpu, u8 value)
{
    const int c = cpu->f.c;
    const u16 res = A - value - c;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.yx_val = (u8)res;
    cpu->f.c = (res & 0x100) != 0;
    cpu->f.h_val = (A & 0x0f) - (value & 0x0f) - c;
    cpu->f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
    cpu->f.n = 1;
    A = (u8)res;
}

Z80_INLINE void z_neg(z80_t *cpu)
{
    u8 value = A;
    A = 0;
    z_sub_a(cpu, value);
}

Z80_INLINE void z_daa(z80_t *cpu)
{
    u8 a = A;
    if (cpu->f.n)
    {
        if (F_H() || ((A & 0xf) > 9))
            a -= 6;
        if (cpu->f.c || (A > 0x99))
            a -= 0x60;
    }
    else
    {
        if (F_H() || ((A & 0xf) > 9))
            a += 6;
        if (cpu->f.c || (A > 0x99))
            a += 0x60;
    }
    QT = 0;
    /* keep N */
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = cpu->f.yx_val = a;
    cpu->f.h_val = A ^ a;
    cpu->f.c = cpu->f.c || A > 0x99;
    A = a;
}

Z80_INLINE void z_and_a(z80_t *cpu, u8 value)
{
    A &= value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = cpu->f.yx_val = A;
    cpu->f.n = cpu->f.c = 0;
    cpu->f.h_val = HF;
}

Z80_INLINE void z_or_a(z80_t *cpu, u8 value)
{
    A |= value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = cpu->f.yx_val = A;
    cpu->f.h_val = cpu->f.n = cpu->f.c = 0;
}

Z80_INLINE void z_xor_a(z80_t *cpu, u8 value)
{
    A ^= value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = cpu->f.yx_val = A;
    cpu->f.h_val = cpu->f.n = cpu->f.c = 0;
}

Z80_INLINE void z_cp(z80_t *cpu, u8 value)
{
    const u16 res = A - value;
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = (u8)res;
    cpu->f.yx_val = value;
    cpu->f.c = (res & 0x100) != 0;
    cpu->f.h_val = (A & 0x0f) - (value & 0x0f);
    cpu->f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
    cpu->f.n = 1;
}

Z80_INLINE void z_exx(z80_t *cpu)
{
    swap(cpu->bc, cpu->bc2);
    swap(cpu->de, cpu->de2);
    swap(cpu->hl, cpu->hl2);
}

/* rotates and shifts: SZP, H and N from the result, C from the bit shifted out */
Z80_INLINE u8 shift_flags(z80_t *cpu, u8 res, int carry)
{
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = cpu->f.yx_val = res;
    cpu->f.h_val = cpu->f.n = 0;
    cpu->f.c = carry != 0;
    return res;
}

Z80_INLINE u8 z_rlc(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value << 1) | (value >> 7)), value & 0x80); }
Z80_INLINE u8 z_rrc(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value >> 1) | (value << 7)), value & 0x01); }
Z80_INLINE u8 z_rl(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value << 1) + cpu->f.c), value & 0x80); }
Z80_INLINE u8 z_rr(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value >> 1) | (cpu->f.c << 7)), value & 0x01); }
Z80_INLINE u8 z_sla(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)(value << 1), value & 0x80); }
Z80_INLINE u8 z_sra(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value >> 1) | (value & 0x80)), value & 0x01); }
Z80_INLINE u8 z_sll(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)((value << 1) | 0x01), value & 0x80); }
Z80_INLINE u8 z_srl(z80_t *cpu, u8 value) { return shift_flags(cpu, (u8)(value >> 1), value & 0x01); }

/* BIT b,r: Y/X from the operand; BIT b,(HL): from MEMPTR; BIT b,(XY+o): from the address */
Z80_INLINE void z_bit(z80_t *cpu, int bit, u8 value, u8 yx)
{
    QT = 0;
    cpu->f.s_val = cpu->f.z_val = cpu->f.pv_val = value & (1 << bit);
    cpu->f.h_val = HF;
    cpu->f.n = 0;
    cpu->f.yx_val = yx;
}

Z80_INLINE void z_block_io_interrupted_flags(z80_t *cpu)
{
    cpu->f.yx_val = PC >> 8;

    const u8 pv_old = F_PV();
    if (cpu->f.c)
    {
        cpu->f.h_val = 0;
        if (TDAT8 & 0x80)
        {
            cpu->f.pv_val = (B - 1) & 0x07;
            if ((B & 0x0f) == 0x00)
                cpu->f.h_val = HF;
        }
        else
        {
            cpu->f.pv_val = (B + 1) & 0x07;
            if ((B & 0x0f) == 0x0f)
                cpu->f.h_val = HF;
        }
    }
    else
    {
        cpu->f.pv_val = B & 0x07;
    }
    cpu->f.pv_val = (pv_old ^ F_PV()) & PF;
}

Z80_INLINE void z_ei(z80_t *cpu)
{
    cpu->iff1 = cpu->iff2 = 1;
    SET_SA(SA_AFTER_EI, 1);
}

/* the names z80.lst uses */
#define get_f() z80_get_f(cpu)
#define set_f(f) z_set_f(cpu, (f))
#define halt() z_halt(cpu)
#define leave_halt() z_leave_halt(cpu)
#define inc(r) z_inc(cpu, &(r))
#define dec(r) z_dec(cpu, &(r))
#define rlca() z_rlca(cpu)
#define rrca() z_rrca(cpu)
#define rla() z_rla(cpu)
#define rra() z_rra(cpu)
#define add_a(v) z_add_a(cpu, (v))
#define adc_a(v) z_adc_a(cpu, (v))
#define sub_a(v) z_sub_a(cpu, (v))
#define sbc_a(v) z_sbc_a(cpu, (v))
#define neg() z_neg(cpu)
#define daa() z_daa(cpu)
#define and_a(v) z_and_a(cpu, (v))
#define or_a(v) z_or_a(cpu, (v))
#define xor_a(v) z_xor_a(cpu, (v))
#define cp(v) z_cp(cpu, (v))
#define exx() z_exx(cpu)
#define rlc(v) z_rlc(cpu, (v))
#define rrc(v) z_rrc(cpu, (v))
#define rl(v) z_rl(cpu, (v))
#define rr(v) z_rr(cpu, (v))
#define sla(v) z_sla(cpu, (v))
#define sra(v) z_sra(cpu, (v))
#define sll(v) z_sll(cpu, (v))
#define srl(v) z_srl(cpu, (v))
#define bit(b, v) z_bit(cpu, (b), (v), (v))
#define bit_hl(b, v) z_bit(cpu, (b), (v), WZ_H)
#define bit_xy(b, v) z_bit(cpu, (b), (v), (u8)(cpu->ea >> 8))
#define res(b, v) ((u8)((v) & ~(1 << (b))))
#define set(b, v) ((u8)((v) | (1 << (b))))
#define block_io_interrupted_flags() z_block_io_interrupted_flags(cpu)
#define ei() z_ei(cpu)
#define illegal_1() ((void)0)
#define illegal_2() ((void)0)

/* ---------------------------------------------------------------------------
 * Initialisation and execution
 * ------------------------------------------------------------------------ */

void z80_init(z80_t *cpu)
{
    PRVPC = PC = SP = AF = BC = DE = HL = WZ = 0;
    z_set_f(cpu, 0);
    Q = QT = 0;
    cpu->af2.w = cpu->bc2.w = cpu->de2.w = cpu->hl2.w = 0;
    R = R2 = 0;
    cpu->iff1 = cpu->iff2 = 0;
    cpu->halt = 0;
    cpu->im = 0;
    cpu->i = 0;
    cpu->nmi_state = cpu->irq_state = 0;
    cpu->busreq_state = cpu->busack_state = 0;
    cpu->ea = 0;
    cpu->service_attention = 0;
    cpu->irq_vector = 0xff;
    cpu->irq_hold = 0;
    cpu->icount = 0;

    IX = IY = 0xffff; /* IX and IY are FFFF after a reset */
    cpu->f.z_val = 0; /* zero flag is set */
}

void z80_reset(z80_t *cpu)
{
    z_leave_halt(cpu);
    PC = 0;
    WZ = PC;
    cpu->i = 0;
    cpu->r = 0;
    cpu->r2 = 0;
    cpu->iff1 = 0;
    cpu->iff2 = 0;
    SET_SA(SA_NMI_PENDING, 0);
    SET_SA(SA_AFTER_EI, 0);
    SET_SA(SA_AFTER_LDAIR, 0);
}

void GAL_HOT(z80_set_nmi_line)(z80_t *cpu, int state)
{
    /* an NMI is pending from the rising edge on */
    if (!cpu->nmi_state && state)
        SET_SA(SA_NMI_PENDING, 1);
    cpu->nmi_state = state != 0;
}

void GAL_HOT(z80_set_irq_line)(z80_t *cpu, int state)
{
    cpu->irq_state = state != 0;
    cpu->irq_hold = 0;
    SET_SA(SA_IRQ_ON, cpu->irq_state);
}

void GAL_HOT(z80_hold_irq)(z80_t *cpu, uint8_t vector)
{
    cpu->irq_vector = vector;
    cpu->irq_state = 1;
    cpu->irq_hold = 1;
    SET_SA(SA_IRQ_ON, 1);
}

static void GAL_HOT(z80_execute)(z80_t *cpu)
{
    u32 ref = 0xffff00;
    (void)ref;
#include "z80_ops.h"
}

void GAL_HOT(z80_run)(z80_t *cpu, int cycles)
{
    cpu->icount += cycles;
    z80_execute(cpu);
}
