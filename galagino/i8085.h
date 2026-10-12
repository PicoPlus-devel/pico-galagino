// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Roberto Fresca, Grull Osgo
/*
 * Intel 8080 / 8085A CPU core.
 *
 * A port of MAME's src/devices/cpu/i8085/i8085.cpp to plain C. The opcode
 * semantics, flag handling and cycle tables are MAME's; the device framework,
 * the status/SOD/INTE callbacks and the interrupt inputs are dropped, since
 * Phoenix has no interrupts (it polls VBLANK) and the CP/M test harness needs
 * none either. RIM and SIM keep their mask register so code using them runs.
 *
 * Memory access goes through a 256-entry page table first: a non-NULL page is
 * read or written directly, anything else goes to the read/write callbacks.
 */
#ifndef I8085_H
#define I8085_H

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
} i8085_pair_t;

typedef struct i8085
{
    i8085_pair_t pc, sp, af, bc, de, hl, wz;
    uint8_t halt;
    uint8_t im;      /* interrupt mask register, as read by RIM */
    uint8_t is_8085; /* 0: behave as an 8080 (used by the CPU exerciser) */
    int32_t icount;  /* cycles left in the current run; may end negative */

    const uint8_t *cycles; /* cycle table for the selected CPU type */

    const uint8_t *rd_page[256];
    uint8_t *wr_page[256];
    uint8_t (*read)(struct i8085 *cpu, uint16_t addr);
    void (*write)(struct i8085 *cpu, uint16_t addr, uint8_t data);
    uint8_t (*in)(struct i8085 *cpu, uint8_t port);
    void (*out)(struct i8085 *cpu, uint8_t port, uint8_t data);
    void *user;
} i8085_t;

/* Clears the CPU and selects 8085 (is_8085 != 0) or 8080 behaviour. The page
 * table and callbacks are left for the caller to fill in. */
void i8085_init(i8085_t *cpu, int is_8085);
void i8085_reset(i8085_t *cpu);

/* Runs for at least `cycles` cycles. Overshoot is carried into the next call
 * through icount, so the long-run rate is exact. */
void i8085_run(i8085_t *cpu, int cycles);

#ifdef __cplusplus
}
#endif

#endif
