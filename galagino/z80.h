// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/*
 * Zilog Z80 CPU core.
 *
 * A port of MAME's src/devices/cpu/z80 to plain C. The instruction semantics,
 * cycle counts and flag handling (including MEMPTR and the Q latch behind the
 * SCF/CCF undocumented flags) are MAME's: the opcode bodies are generated from
 * MAME's z80.lst by tools/z80gen.py into z80_ops.h, and the helpers in z80.c
 * are MAME's z80.cpp. Dropped: the device framework, the daisy chain, BUSREQ/
 * WAIT, and the mid-instruction suspension - an instruction always runs to the
 * end, and the cycle budget is checked between instructions.
 *
 * Memory access goes through a 256-entry page table first: a non-NULL page is
 * read or written directly, anything else goes to the read/write callbacks.
 * Operands, stack and data all use the same tables; opcode fetches (M1, the
 * prefix bytes included) look in op_page first, for CPUs whose opcodes are
 * encrypted differently from their data (Sega 315-5010: Pengo): a table of
 * 256 pages the game provides, NULL (as it is unless a game sets it) or with
 * NULL entries where opcodes are read as data.
 *
 * Taken from pico-mooncresta. The additions are z80_hold_irq(), MAME's
 * HOLD_LINE: an IRQ that stays asserted until the CPU acknowledges it, and
 * op_page, MAME's separate opcode space.
 */
#ifndef Z80_H
#define Z80_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register pair, little-endian byte order (true for ARM and x86). */
typedef union
{
    struct
    {
        uint8_t l, h;
    } b;
    uint16_t w;
} z80_pair_t;

typedef struct z80
{
    z80_pair_t prvpc, pc, sp, af, bc, de, hl, ix, iy, wz;
    z80_pair_t af2, bc2, de2, hl2;
    uint8_t r, r2, i, im;
    uint8_t iff1, iff2, halt;
    uint8_t nmi_state, irq_state;
    uint8_t busreq_state, busack_state; /* never driven; kept for the generated code */
    uint8_t service_attention;          /* SA_* bits, see z80.c */
    uint8_t irq_vector;                 /* what the bus shows on an interrupt acknowledge */
    uint8_t irq_hold;                   /* the IRQ is released by its acknowledge (HOLD_LINE) */
    uint16_t ea;

    /* Flags are kept as separate values and assembled into F on demand. */
    struct
    {
        uint8_t s_val, z_val, yx_val, h_val, pv_val;
        bool n, c; /* bool as in MAME: assigning a masked bit sets them */
        uint8_t q, qtemp;
    } f;

    int32_t icount; /* cycles left in the current run; may end negative */
    int32_t tmp_irq_vector;
    z80_pair_t shared_data, shared_data2;

    const uint8_t *rd_page[256];
    uint8_t *wr_page[256];
    const uint8_t *const *op_page; /* NULL, or 256 pages of decrypted opcodes, see above */
    uint8_t (*read)(struct z80 *cpu, uint16_t addr);
    void (*write)(struct z80 *cpu, uint16_t addr, uint8_t data);
    uint8_t (*in)(struct z80 *cpu, uint16_t port);
    void (*out)(struct z80 *cpu, uint16_t port, uint8_t data);
    void *user;
} z80_t;

/* Clears the CPU to its power-on state. The page table and callbacks are left
 * for the caller to fill in. */
void z80_init(z80_t *cpu);
void z80_reset(z80_t *cpu);

/* Runs for at least `cycles` cycles. Overshoot is carried into the next call
 * through icount, so the long-run rate is exact. */
void z80_run(z80_t *cpu, int cycles);

/* Input lines, 0 = clear, 1 = asserted. NMI is taken on the rising edge, INT
 * while asserted and enabled. */
void z80_set_nmi_line(z80_t *cpu, int state);
void z80_set_irq_line(z80_t *cpu, int state);

/* Asserts INT with `vector` on the bus until the CPU takes it, then releases
 * it (MAME's HOLD_LINE). z80_set_irq_line(cpu, 0) withdraws it earlier. */
void z80_hold_irq(z80_t *cpu, uint8_t vector);

/* F as the CPU would push it (the flags are kept unpacked). */
uint8_t z80_get_f(const z80_t *cpu);

#ifdef __cplusplus
}
#endif

#endif
