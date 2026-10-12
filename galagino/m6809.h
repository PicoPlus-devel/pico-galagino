/*
 * M6809 CPU Emulator for Galagino
 *
 * Lightweight Motorola 6809 emulator designed for ESP32.
 * Supports all standard M6809 instructions including:
 * - 8/16-bit registers: A, B, D(=A:B), X, Y, U, S, PC, DP, CC
 * - Addressing modes: inherent, immediate, direct, indexed, extended
 * - Interrupts: IRQ, FIRQ, NMI
 * - Page 2 (0x10xx) and Page 3 (0x11xx) extended opcodes
 *
 * Interface uses external callbacks for memory access,
 * provided by the machine class (gyruss).
 *
 * From GalaginoPlus (source/src/cpus/m6809). Changed for this port: the
 * memory callbacks and a page table are per instance (see the end of
 * m6809_state), m6809_run with a cycle budget.
 */

#ifndef M6809_H
#define M6809_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Condition Code register bits */
#define M6809_CC_C  0x01  /* Carry */
#define M6809_CC_V  0x02  /* Overflow */
#define M6809_CC_Z  0x04  /* Zero */
#define M6809_CC_N  0x08  /* Negative */
#define M6809_CC_I  0x10  /* IRQ mask */
#define M6809_CC_H  0x20  /* Half carry */
#define M6809_CC_F  0x40  /* FIRQ mask */
#define M6809_CC_E  0x80  /* Entire state saved */

typedef struct m6809_state_S {
    uint8_t  A, B;       /* Accumulators */
    uint16_t X, Y;       /* Index registers */
    uint16_t U, S;       /* User and System stack pointers */
    uint16_t PC;         /* Program counter */
    uint8_t  DP;         /* Direct page register */
    uint8_t  CC;         /* Condition code register */

    int      cycles;     /* Cycles consumed in current step */
    int      total_cycles; /* Total cycles consumed */

    uint8_t  irq_pending;   /* IRQ request pending */
    uint8_t  firq_pending;  /* FIRQ request pending */
    uint8_t  nmi_pending;   /* NMI request pending */
    uint8_t  nmi_armed;     /* NMI armed (requires S to be written first) */

    uint8_t  halted;     /* CPU halted (SYNC/CWAI) */
    uint8_t  spin;       /* this port: a taken branch to itself (see m6809_run) */

    /* Optional direct-ROM window for instruction fetches (opcode and
       operand bytes): while PC is inside [rom_base, rom_base+rom_size)
       bytes are read straight from rom_direct[PC-rom_base], skipping the
       m6809_read/m6809_read_opcode callback chain (global trampoline +
       virtual dispatch), which dominates the per-instruction cost.
       Opt-in per machine (used by mappy, dual 6809): m6809_reset() clears
       the window, so it must be (re)installed after every reset call.
       Do NOT use on CPUs with encrypted opcodes (KONAMI-1: gyruss sub,
       tutankhm, rocnrope) - it would bypass m6809_read_opcode. */
    const uint8_t *rom_direct;
    uint16_t rom_base;
    uint16_t rom_size;

    /* this port: an instance's memory, as the other CPU cores here: a page
     * table read or written directly where not NULL, the callbacks
     * otherwise; read_opcode, when set, fetches the opcode bytes (KONAMI-1);
     * the cycles left of the current m6809_run */
    const uint8_t *rd_page[256];
    uint8_t *wr_page[256];
    uint8_t (*read)(struct m6809_state_S *s, uint16_t addr);
    void (*write)(struct m6809_state_S *s, uint16_t addr, uint8_t val);
    uint8_t (*read_opcode)(struct m6809_state_S *s, uint16_t addr);
    int32_t icount;
    void *user;
} m6809_state;

/* Reset the CPU - sets PC from reset vector at 0xFFFE */
void m6809_reset(m6809_state *s);

/* Execute one instruction, returns cycles consumed */
int m6809_step(m6809_state *s, char count);

/* As z80_run: at least `cycles` cycles, the overshoot carried into the next
 * call through icount */
void m6809_run(m6809_state *s, int cycles);

/* Signal an IRQ (active while pending flag set) */
void m6809_irq(m6809_state *s);

/* Signal a FIRQ */
void m6809_firq(m6809_state *s);

/* Signal NMI */
void m6809_nmi(m6809_state *s);


#ifdef __cplusplus
}
#endif

#endif /* M6809_H */
