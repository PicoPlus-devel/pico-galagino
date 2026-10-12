/*
 * Konami's sound board of Time Pilot, Pooyan, Tutankham and Roc'n Rope, after
 * MAME's shared/timeplt_a.cpp: a Z80 at 1.79 MHz with two AY-3-8910. The main
 * CPU writes a command to a latch, which the Z80 reads on the first AY's port
 * A, and a rising edge on one of its latch bits interrupts the Z80 (IM 1).
 * The second port reads a timer, the Z80's clock divided by 5120 through a
 * bi-quinary counter. The RC filters on the AY outputs are not emulated.
 *
 * It runs as gal.cpu[1]; its RAM is the 1 KB at gal.ram + ram_ofs.
 */
#include "gal_internal.h"

#include "konami_snd.h"

static struct
{
    uint8_t latch;
    uint8_t ay_addr[2];
    uint8_t last_trigger;
    uint64_t cycles; /* the Z80's clock so far, for the timer */
    int32_t run_start;
} ks;

static uint8_t ks_ay_read(const z80_t *cpu, int chip)
{
    const uint8_t reg = ks.ay_addr[chip];
    if (chip == 0 && reg == 14)
        return ks.latch;
    if (chip == 0 && reg == 15)
    {
        static const uint8_t timer[10] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0};
        const uint64_t now = ks.cycles + (uint64_t)(ks.run_start - cpu->icount);
        return timer[(now / 512) % 10];
    }
    return reg < 16 ? gal.soundregs[16 * chip + reg] : 0xff;
}

static uint8_t ks_read(z80_t *cpu, uint16_t addr)
{
    switch (addr & 0xf000)
    {
    case 0x4000:
        return ks_ay_read(cpu, 0);
    case 0x6000:
        return ks_ay_read(cpu, 1);
    default:
        return 0xff;
    }
}

static void ks_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    switch (addr & 0xf000)
    {
    case 0x4000:
        gal.soundregs[ks.ay_addr[0]] = value;
        break;
    case 0x5000:
        ks.ay_addr[0] = value & 0x0f;
        break;
    case 0x6000:
        gal.soundregs[16 + ks.ay_addr[1]] = value;
        break;
    case 0x7000:
        ks.ay_addr[1] = value & 0x0f;
        break;
    default:
        break; /* 8000-ffff: the filters */
    }
}

void konami_snd_init(const uint8_t *rom, int rom_size, int ram_ofs)
{
    z80_t *cpu = &gal.cpu[1];
    for (int p = 0; p < rom_size / 256 && p < 0x30; p++)
        cpu->rd_page[p] = rom + 256 * p;
    for (int p = 0x30; p < 0x40; p++) /* 3000-33ff, mirrored to 3fff */
        cpu->rd_page[p] = cpu->wr_page[p] = gal.ram + ram_ofs + 256 * (p & 3);
    cpu->read = ks_read;
    cpu->write = ks_write;
}

void konami_snd_reset(void)
{
    memset(&ks, 0, sizeof(ks));
    memset(gal.soundregs, 0, 32);
    z80_init(&gal.cpu[1]);
    z80_reset(&gal.cpu[1]);
    gal_ay_reset();
}

void konami_snd_command(uint8_t data)
{
    ks.latch = data;
}

void konami_snd_trigger(int state)
{
    /* a rising edge interrupts */
    if (!ks.last_trigger && state)
        z80_hold_irq(&gal.cpu[1], 0xff);
    ks.last_trigger = (uint8_t)(state != 0);
}

void konami_snd_run(int cycles)
{
    z80_t *cpu = &gal.cpu[1];
    ks.run_start = cpu->icount + cycles;
    z80_run(cpu, cycles);
    ks.cycles += (uint32_t)(ks.run_start - cpu->icount);
}

void konami_snd_render(int16_t *out, int n)
{
    gal_ay_render(2, KONAMI_SND_CLOCK, 6, out, n);
}
