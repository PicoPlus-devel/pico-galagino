/*
 * Konami's Gyruss (1983), after MAME's konami/gyruss.cpp and gyruss_v.cpp.
 *
 * A Z80 at 3.07 MHz, its NMI at the vertical blank while enabled, shares RAM
 * with a KONAMI-1 (a 6809 that decrypts its opcodes) at 1.54 MHz, which keeps
 * the sprite list. The sound board: a Z80 at 3.58 MHz with five AY-3-8910
 * (the filters on their outputs are left out) and an 8039 whose port drives
 * a DAC (the drums).
 *
 * 256x224 shown (MAME ROT90: the upright picture is 224x256, drawn at y
 * 16..271). A 32x32 tilemap of 8x8 tiles of two bits, those with attribute
 * bit 4 in front of the sprites; 48 sprites of 8x16, four bits per pixel, in
 * two banks. Large: the sprites are read from PSRAM or the flash cache.
 */
#include "gal_internal.h"

#include "i8048.h"
#include "m6809.h"

#define CYCLES_PER_FRAME 50688     /* 3.072 MHz at 60.6 Hz */
#define SUB_CYCLES_PER_FRAME 25344 /* 1.536 MHz */
#define SND_CYCLES_PER_FRAME 59063 /* 3.58 MHz */
#define DRUM_CYCLES_PER_FRAME 8800 /* 8 MHz / 15 */
#define AY_CLOCK 1789772
#define STEPS 16
#define VIEW_TOP 16
#define NSPRITES 512
#define SPR_BYTES 64 /* 8x16, four bits */
#define DAC_SLOTS 256 /* per quarter frame */

/* The RAM, in the arena */
#define RAM_COLOR 0x0000  /* 8000-83ff */
#define RAM_VIDEO 0x0400  /* 8400-87ff */
#define RAM_WORK 0x0800   /* 9000-9fff */
#define RAM_SHARED 0x1800 /* a000-a7ff, the sub CPU's 6000-67ff */
#define RAM_SUB 0x2000    /* the sub CPU's 4000-47ff, the sprites at 4040 */
#define RAM_SOUND 0x2800  /* the sound CPU's 6000-63ff */
#define RAM_SIZE 0x2c00

static struct
{
    uint8_t *ram, *rom, *rom_sub, *rom_snd, *rom_drums;
    m6809_state *sub;
    struct i8048_state_S *drums;
    uint16_t *tiles;          /* [512][8] */
    uint8_t *tile_blank;      /* [512]: all pen 0 */
    const uint32_t *sprites;  /* [512][16]: 16x8 upright */
    uint16_t *tile_colors;    /* [16][4] */
    uint16_t *spr_colors;     /* [16][16] */
    uint8_t nmi_enable, sub_irq, latch, latch2, drum_dac;
    uint8_t ay_addr[5];
    int sub_pos, sub_len;
    uint64_t snd_cycles;
    int32_t snd_run_start;
    unsigned long drum_quarter;
    uint16_t *dac_slot;
} gy;

/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t chr_layout = {
    8, 8, 2, {4, 0}, {0, 1, 2, 3, 64, 65, 66, 67}, {0, 8, 16, 24, 32, 40, 48, 56}, 16 * 8};

/* MAME's spritelayout, 8x16; the second bank 16 bytes in */
static int spr_pixel(const uint8_t *rom, int n, int x, int y)
{
    static const uint32_t planes[4] = {0x4000 * 8 + 4, 0x4000 * 8, 4, 0};
    const uint32_t base = (uint32_t)(n & 0xff) * 512 + (n >> 8) * 128 + (y < 8 ? 8 * y : 256 + 8 * (y - 8)) +
                          (x < 4 ? x : 64 + x - 4);
    int v = 0;
    for (int p = 0; p < 4; p++)
    {
        const uint32_t bit = base + planes[p];
        v = (v << 1) | ((rom[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return v;
}

/* upright (MAME ROT90): 16 wide, 8 tall, two words of eight pixels a row */
static void conv_sprites(const uint8_t *rom, int first, int count, uint32_t *out)
{
    for (int n = first; n < first + count; n++)
        for (int uy = 0; uy < 8; uy++)
            for (int w = 0; w < 2; w++)
            {
                uint32_t word = 0;
                for (int k = 0; k < 8; k++)
                    word |= (uint32_t)spr_pixel(rom, n, uy, 15 - (8 * w + k)) << (4 * k);
                *out++ = word;
            }
}

/* the table the flash cache keeps: the sprites */
static void gy_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    (void)i;
    conv_sprites(src, ofs / SPR_BYTES, len / SPR_BYTES, (uint32_t *)out);
}

/* --- Main CPU ---------------------------------------------------------------------- */

static uint8_t gy_read(z80_t *cpu, uint16_t addr)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    if (addr < 0x6000)
    {
        /* ROM page 58, read here for the main loop's wait (05f4-0603): it
         * reads 5865 and polls the task queue at 9500 + (9433) until an entry
         * has bit 7 clear, 65 cycles a round. Only the NMI at the vertical
         * blank posts tasks, so the rounds within this run that would find
         * none are skipped, as MAME does with its speed-ups: the loop ends at
         * the same cycle. */
        if (addr == 0x5865 && cpu->pc.w == 0x05fa && cpu->icount > 65 &&
            (gy.ram[RAM_WORK + 0x500 + gy.ram[RAM_WORK + 0x433]] & 0x80))
            cpu->icount -= 65 * ((cpu->icount - 1) / 65);
        return gy.rom[addr];
    }
    switch (addr)
    {
    case 0xc000:
        return 0x69; /* DSW2, as GalaginoPlus: 5 lives, upright, bonus at 30000, normal */
    case 0xc080: /* IN0, active low */
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        return v;
    case 0xc0a0: /* IN1 */
        if (keys & GAL_BTN_LEFT)
            v &= ~0x01;
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x02;
        if (keys & GAL_BTN_UP)
            v &= ~0x04;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        return v;
    case 0xc100:
        return 0x00; /* DSW3 */
    default: /* c0c0 the second player, c0e0 DSW1 (1C1C) */
        return 0xff;
    }
}

static void gy_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if (addr == 0xc080)
        z80_hold_irq(&gal.cpu[1], 0xff);
    else if (addr == 0xc100)
        gy.latch = value;
    else if ((addr & 0xfff8) == 0xc180)
    {
        /* the latch: bit addr & 7 */
        if ((addr & 7) == 0)
        {
            gy.nmi_enable = value & 1;
            if (!gy.nmi_enable)
                z80_set_nmi_line(cpu, 0);
        }
        /* 2 and 3 the coin counters, 5 the flip */
    }
    /* c000: the watchdog */
}

/* --- Sub CPU ----------------------------------------------------------------------- */

/* KONAMI-1: an opcode is stored XORed by a mask chosen by address bits 1 and 3 */
static uint8_t gy_sub_opcode(m6809_state *s, uint16_t addr)
{
    static const uint8_t mask[4] = {0x22, 0x82, 0x28, 0x88};
    const uint8_t v = s->rd_page[addr >> 8] ? s->rd_page[addr >> 8][addr & 0xff] : s->read(s, addr);
    return v ^ mask[((addr >> 1) & 1) | ((addr >> 2) & 2)];
}

static uint8_t gy_sub_read(m6809_state *s, uint16_t addr)
{
    if (addr == 0x0000)
    {
        /* the raster line */
        const int pos = gy.sub_pos + gy.sub_len - (s->icount > 0 ? s->icount : 0);
        return (uint8_t)(pos * 264 / SUB_CYCLES_PER_FRAME);
    }
    return 0xff;
}

static void gy_sub_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    if (addr == 0x2000)
    {
        gy.sub_irq = value & 1;
        if (!gy.sub_irq)
            s->irq_pending = 0;
    }
}

/* --- Sound CPUs -------------------------------------------------------------------- */

static uint8_t gy_snd_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    return addr == 0x8000 ? gy.latch : 0xff;
}

static void gy_snd_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    (void)addr;
    (void)value;
}

static uint8_t gy_snd_in(z80_t *cpu, uint16_t port)
{
    const int p = port & 0xff, chip = p >> 2;
    if (p > 0x12 || (p & 3) != 1)
        return 0xff;
    const uint8_t reg = gy.ay_addr[chip];
    if (chip == 2 && reg == 14)
    {
        /* port A of the third: a timer of the CPU clock / 1024 */
        static const uint8_t timer[10] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0a, 0x0b, 0x0a, 0x0d};
        const uint64_t now = gy.snd_cycles + (uint64_t)(gy.snd_run_start - cpu->icount);
        const uint8_t v = timer[(now / 1024) % 10];
        /* The program's tempo loop (00fe-010b) waits, reading this through
         * RST 10h, for bit 2 to fall (returning to 0101) and then to rise
         * (0108), one read every 69 cycles. While it waits, the reads that
         * would see no change are skipped within the run, as MAME does with
         * its speed-ups: the loop ends at the same cycle. Not while an
         * interrupt waits. */
        if (!(cpu->irq_state && cpu->iff1))
        {
            const uint16_t sp = cpu->sp.w;
            const uint8_t *page = cpu->rd_page[sp >> 8], *page2 = cpu->rd_page[(uint16_t)(sp + 1) >> 8];
            if (page && page2)
            {
                const uint16_t ret = (uint16_t)(page[sp & 0xff] | (page2[(sp + 1) & 0xff] << 8));
                const int want = ret == 0x0101 ? 0 : ret == 0x0108 ? 4 : -1;
                if (want >= 0 && (v & 4) != want && cpu->icount > 69)
                {
                    /* the iterations to the change, but none past the end of
                     * this run, where the main CPU may send a command */
                    uint32_t i = 1;
                    const uint32_t most = (uint32_t)(cpu->icount - 1) / 69;
                    while (i - 1 < most && (timer[((now + 69 * i) / 1024) % 10] & 4) != want)
                        i++;
                    cpu->icount -= (int32_t)(69 * (i - 1));
                }
            }
        }
        return v;
    }
    return reg < 16 ? gal.soundregs[16 * chip + reg] : 0xff;
}

static void gy_snd_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    const int p = port & 0xff;
    if (p == 0x14)
        gy.drums->notINT = false; /* the 8039's interrupt */
    else if (p == 0x18)
        gy.latch2 = value;
    else if (p <= 0x12)
    {
        const int chip = p >> 2;
        if ((p & 3) == 0)
            gy.ay_addr[chip] = value & 0x0f;
        else if ((p & 3) == 2)
            gal.soundregs[16 * chip + gy.ay_addr[chip]] = value;
    }
}

static void gy_drum_port_write(struct i8048_state_S *s, unsigned char port, unsigned char value)
{
    if (port == 1)
    {
        /* the DAC, in the slot of the quarter frame this write falls in */
        int slot = (int)((s->cycles - gy.drum_quarter) * DAC_SLOTS / (DRUM_CYCLES_PER_FRAME / 4));
        if (slot >= DAC_SLOTS)
            slot = DAC_SLOTS - 1;
        gy.dac_slot[slot] = value;
    }
    else if (port == 2)
    {
        s->p2_state = value;
        s->notINT = true; /* P2 clears the interrupt */
    }
}

static unsigned char gy_drum_port_read(struct i8048_state_S *s, unsigned char port)
{
    if (port == 0)
        return gy.latch2;
    return port == 2 ? (unsigned char)s->p2_state : 0xff;
}

static unsigned char gy_drum_xdm_read(struct i8048_state_S *s, unsigned char addr)
{
    (void)s;
    (void)addr;
    return gy.latch2;
}

static const gal_i8048_io_t gy_drum_io = {gy_drum_port_write, gy_drum_port_read, gy_drum_xdm_read};

/* --- Set-up ----------------------------------------------------------------------- */

/* MAME's gyruss palette: resistors of 1K, 470 and 220 ohm against 470 */
static void gy_palette(const uint8_t *prom, uint16_t *pal)
{
    const double rg[3] = {1000, 470, 220}, bl[2] = {470, 220}, pd = 1.0 / 470;
    double s_rg = 0, s_b = 0;
    for (int k = 0; k < 3; k++)
        s_rg += 1 / rg[k];
    for (int k = 0; k < 2; k++)
        s_b += 1 / bl[k];
    const double out_rg = s_rg / (s_rg + pd), out_b = s_b / (s_b + pd);
    const double scale = 255 / (out_rg > out_b ? out_rg : out_b);
    double wrg[3], wb[2];
    for (int k = 0; k < 3; k++)
        wrg[k] = scale * (1 / rg[k]) / (s_rg + pd);
    for (int k = 0; k < 2; k++)
        wb[k] = scale * (1 / bl[k]) / (s_b + pd);
    for (int i = 0; i < 32; i++)
    {
        const uint8_t c = prom[i];
        const int r = (int)(wrg[0] * (c & 1) + wrg[1] * ((c >> 1) & 1) + wrg[2] * ((c >> 2) & 1) + 0.5);
        const int g = (int)(wrg[0] * ((c >> 3) & 1) + wrg[1] * ((c >> 4) & 1) + wrg[2] * ((c >> 5) & 1) + 0.5);
        const int b = (int)(wb[0] * ((c >> 6) & 1) + wb[1] * ((c >> 7) & 1) + 0.5);
        pal[i] = gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }
}

static int gy_init(uint8_t *const *regions)
{
    memset(&gy, 0, sizeof(gy));
    /* the programs and the RAM to SRAM */
    gy.ram = (uint8_t *)gal_fast_alloc(RAM_SIZE);
    gy.rom = (uint8_t *)gal_fast_alloc(0x6000);
    gy.rom_sub = (uint8_t *)gal_fast_alloc(0x2000);
    gy.rom_snd = (uint8_t *)gal_fast_alloc(0x4000);
    gy.rom_drums = (uint8_t *)gal_fast_alloc(0x1000);
    gy.sub = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    gy.drums = (struct i8048_state_S *)gal_fast_alloc(sizeof(struct i8048_state_S));
    gy.tiles = (uint16_t *)gal_fast_alloc(512 * 8 * 2);
    gy.tile_blank = (uint8_t *)gal_fast_alloc(512);
    gy.tile_colors = (uint16_t *)gal_fast_alloc(16 * 4 * 2);
    gy.spr_colors = (uint16_t *)gal_fast_alloc(16 * 16 * 2);
    gy.dac_slot = (uint16_t *)gal_fast_alloc(DAC_SLOTS * 2);
    if (!gy.ram || !gy.rom || !gy.rom_sub || !gy.rom_snd || !gy.rom_drums || !gy.sub || !gy.drums || !gy.tiles || !gy.tile_blank ||
        !gy.tile_colors || !gy.spr_colors || !gy.dac_slot)
        return 0;
    memcpy(gy.rom, regions[0], 0x6000);
    memcpy(gy.rom_sub, regions[1], 0x2000);
    memcpy(gy.rom_snd, regions[2], 0x4000);
    memcpy(gy.rom_drums, regions[3], 0x1000);
    dkong_rom_cpu2 = gy.rom_drums;
    gal_i8048_io = &gy_drum_io;

    gal_conv_tiles8(regions[5], &chr_layout, 512, GAL_ROT90, gy.tiles);
    for (int n = 0; n < 512; n++)
    {
        uint16_t any = 0;
        for (int k = 0; k < 8; k++)
            any |= gy.tiles[8 * n + k];
        gy.tile_blank[n] = !any;
    }
    if (regions[GAL_MAX_REGIONS])
        gy.sprites = (const uint32_t *)regions[GAL_MAX_REGIONS];
    else
    {
        uint32_t *sprites = (uint32_t *)gal_alloc(NSPRITES * SPR_BYTES);
        if (!sprites)
            return 0;
        conv_sprites(regions[4], 0, NSPRITES, sprites);
        gy.sprites = sprites;
    }

    /* sprites through the first lookup PROM into the first 16 colours,
     * characters through the second into the second 16 */
    uint16_t pal[32];
    gy_palette(regions[6], pal);
    for (int i = 0; i < 256; i++)
        gy.spr_colors[i] = pal[regions[7][i] & 0x0f];
    for (int i = 0; i < 64; i++)
        gy.tile_colors[i] = pal[(regions[8][i] & 0x0f) | 0x10];

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x100; p++)
    {
        cpu->rd_page[p] = gal_zeros;
        cpu->wr_page[p] = gal_ignored;
    }
    for (int p = 0; p < 0x60; p++)
        cpu->rd_page[p] = gy.rom + 256 * p;
    for (int p = 0; p < 8; p++) /* 8000-87ff: colour and video RAM */
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = gy.ram + RAM_COLOR + 256 * p;
    for (int p = 0; p < 0x10; p++)
        cpu->rd_page[0x90 + p] = cpu->wr_page[0x90 + p] = gy.ram + RAM_WORK + 256 * p;
    for (int p = 0; p < 8; p++)
        cpu->rd_page[0xa0 + p] = cpu->wr_page[0xa0 + p] = gy.ram + RAM_SHARED + 256 * p;
    cpu->rd_page[0x58] = NULL; /* the main loop's wait, see gy_read */
    cpu->rd_page[0xc0] = cpu->rd_page[0xc1] = NULL;
    cpu->wr_page[0xc0] = cpu->wr_page[0xc1] = NULL;
    cpu->read = gy_read;
    cpu->write = gy_write;

    m6809_state *s = gy.sub;
    memset(s, 0, sizeof(*s));
    for (int p = 0; p < 0x100; p++)
    {
        s->rd_page[p] = gal_zeros;
        s->wr_page[p] = gal_ignored;
    }
    s->rd_page[0x00] = NULL; /* the raster line */
    s->wr_page[0x20] = NULL; /* the IRQ enable */
    for (int p = 0; p < 8; p++)
    {
        s->rd_page[0x40 + p] = s->wr_page[0x40 + p] = gy.ram + RAM_SUB + 256 * p;
        s->rd_page[0x60 + p] = s->wr_page[0x60 + p] = gy.ram + RAM_SHARED + 256 * p;
    }
    for (int p = 0; p < 0x20; p++)
        s->rd_page[0xe0 + p] = gy.rom_sub + 256 * p;
    s->read = gy_sub_read;
    s->write = gy_sub_write;
    s->read_opcode = gy_sub_opcode;

    z80_t *snd = &gal.cpu[1];
    for (int p = 0; p < 0x100; p++)
    {
        snd->rd_page[p] = gal_zeros;
        snd->wr_page[p] = gal_ignored;
    }
    for (int p = 0; p < 0x40; p++)
        snd->rd_page[p] = gy.rom_snd + 256 * p;
    for (int p = 0; p < 4; p++)
        snd->rd_page[0x60 + p] = snd->wr_page[0x60 + p] = gy.ram + RAM_SOUND + 256 * p;
    snd->rd_page[0x80] = NULL; /* the command */
    snd->read = gy_snd_read;
    snd->write = gy_snd_write;
    snd->in = gy_snd_in;
    snd->out = gy_snd_out;
    return 1;
}

static void gy_reset(void)
{
    memset(gy.ram, 0, RAM_SIZE);
    gy.nmi_enable = gy.sub_irq = gy.latch = gy.latch2 = 0;
    memset(gy.ay_addr, 0, sizeof(gy.ay_addr));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    gy.snd_cycles = 0;
    gy.drum_dac = 0x80;
    for (int i = 0; i < DAC_SLOTS; i++)
        gy.dac_slot[i] = 0xffff;
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    z80_init(&gal.cpu[1]);
    z80_reset(&gal.cpu[1]);
    m6809_reset(gy.sub);
    gy.sub->icount = 0;
    i8048_reset(gy.drums);
    gal_ay_reset();
}

/* The 8039's timer advanced by n cycles, as i8048.c's tick() would: an
 * increment every 32, and the overflow flag. */
static void drums_advance(struct i8048_state_S *d, long n)
{
    d->cycles += (unsigned long)n;
    if (d->timerRunning)
    {
        const long c = d->cyclesUntilCount;
        const long k = ((c - 1) >> 5) - ((c - n - 1) >> 5);
        if (k >= 256 || d->T + k > 0xff)
            d->TF = d->timerInterruptRequested = true;
        d->T = (unsigned char)(d->T + k);
        d->cyclesUntilCount = c - n;
    }
}

/* The 8039 until its cycle count reaches end. Its program waits for the
 * sound CPU's interrupt in a loop (0019: NOP, 1 cycle; JMP 0019, 2 cycles),
 * and times the drum samples with DJNZ loops on themselves (2 cycles a
 * round). While nothing can interrupt it there, the rounds up to end are
 * skipped, stopping where stepping them would, as MAME does with its
 * speed-ups. */
static void drums_run(unsigned long end)
{
    struct i8048_state_S *d = gy.drums;
    while ((long)(d->cycles - end) < 0)
    {
        if (d->notINT && !d->tcntInterruptsEnabled && !d->counterRunning)
        {
            const long r = (long)(end - d->cycles);
            if (d->PC == 0x19)
            {
                long n = 3 * (r / 3);
                if (r % 3 == 1)
                {
                    n += 1; /* stopped after the NOP */
                    d->PC = 0x1a;
                }
                else if (r % 3 == 2)
                    n += 3; /* the JMP runs past end */
                drums_advance(d, n);
                return;
            }
            const uint8_t op = dkong_rom_cpu2[d->PC & 0xfff];
            if ((op & 0xf8) == 0xe8 && dkong_rom_cpu2[(d->PC + 1) & 0xfff] == (d->PC & 0xff))
            {
                /* DJNZ Rn on itself: Rn rounds (256 for 0) */
                uint8_t *reg = &d->ram[((d->PSW & 0x10) ? REGISTER_BANK_1_BASE : REGISTER_BANK_0_BASE) + (op & 7)];
                const long rounds = *reg ? *reg : 256, fit = (r + 1) / 2;
                if (rounds <= fit)
                {
                    *reg = 0;
                    d->PC = (unsigned short)(d->PC + 2);
                    drums_advance(d, 2 * rounds);
                }
                else
                {
                    *reg = (uint8_t)(*reg - fit);
                    drums_advance(d, 2 * fit);
                    return;
                }
                continue;
            }
        }
        i8048_step(d);
    }
}

/* Profiling builds count the main, sub and sound CPUs and the 8039 in
 * gal_prof[2..5], the drawing in [6] (the tiles behind the sprites) and [7]. */
static void gy_run_slice(int q)
{
    gy.drum_quarter = gy.drums->cycles;
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        GAL_PROF_START(t0);
        z80_run(&gal.cpu[0], (step + 1) * CYCLES_PER_FRAME / STEPS - step * CYCLES_PER_FRAME / STEPS);
        GAL_PROF_ADD(2, t0);
        gy.sub_pos = step * SUB_CYCLES_PER_FRAME / STEPS;
        gy.sub_len = (step + 1) * SUB_CYCLES_PER_FRAME / STEPS - gy.sub_pos;
        GAL_PROF_START(t1);
        m6809_run(gy.sub, gy.sub_len);
        GAL_PROF_ADD(3, t1);

        z80_t *snd = &gal.cpu[1];
        const int len = (step + 1) * SND_CYCLES_PER_FRAME / STEPS - step * SND_CYCLES_PER_FRAME / STEPS;
        gy.snd_run_start = snd->icount + len;
        GAL_PROF_START(t2);
        z80_run(snd, len);
        GAL_PROF_ADD(4, t2);
        gy.snd_cycles += (uint32_t)(gy.snd_run_start - snd->icount);

        const unsigned long end = gy.drum_quarter + (unsigned long)((i + 1) * (DRUM_CYCLES_PER_FRAME / 4) / (STEPS / 4));
        GAL_PROF_START(t3);
        drums_run(end);
        GAL_PROF_ADD(5, t3);
    }
    if (q == 3)
    {
        if (gy.nmi_enable)
            z80_set_nmi_line(&gal.cpu[0], 1);
        if (gy.sub_irq)
            m6809_irq(gy.sub);
    }
}

static void gy_sound(int16_t *out, int n)
{
    gal_ay_render(5, AY_CLOCK, 2, out, n);
    /* the drums' DAC */
    for (int i = 0; i < n; i++)
    {
        const uint16_t w = gy.dac_slot[i * DAC_SLOTS / n];
        if (w != 0xffff)
            gy.drum_dac = (uint8_t)w;
        const int32_t v = out[i] + ((int)gy.drum_dac - 0x80) * 32;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    for (int i = 0; i < DAC_SLOTS; i++)
        gy.dac_slot[i] = 0xffff;
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (239 - y,
 * x + 16), so a w x h element at (x, y) has its upright corner at
 * (240 - h - y, x + 16); its flips in x and y are flips in y and x upright */
#define UP_X(y, h) (240 - (h) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void GAL_HOT(gy_prepare_frame)(void)
{
    const uint8_t *sr = gy.ram + RAM_SUB + 0x40;
    gal.active_sprites = 0;
    /* MAME's draw_sprites: the later on top */
    for (int offs = 0xbc; offs >= 0; offs -= 4)
    {
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        const int code = (((sr[offs + 2] & 0x20) << 2) | (sr[offs + 1] >> 1)) + 256 * (sr[offs + 1] & 1);
        o->code = (uint8_t)code;
        o->color = sr[offs + 2] & 0x0f;
        /* flags: 1 flip x, 2 flip y (upright), 0x80 code bit 8 */
        o->flags = (uint8_t)(((sr[offs + 2] & 0x80) ? 1 : 0) | ((~sr[offs + 2] & 0x40) ? 2 : 0) | ((code >> 8) << 7));
        o->x = (int16_t)UP_X(241 - sr[offs + 3], 16);
        o->y = (int16_t)UP_Y(sr[offs]);
    }
}

/* a 16x8 sprite, pen 0 transparent */
static void GAL_HOT(draw_sprite)(const gal_band_t *b, const gal_sprite_t *o)
{
    const int px = o->x, py = o->y, flipx = o->flags & 1, flipy = (o->flags & 2) != 0;
    int ys = py, ye = py + 8, xs = px, xe = px + 16;
    if (ys < b->y0)
        ys = b->y0;
    if (ye > b->y0 + b->h)
        ye = b->y0 + b->h;
    if (xs < b->x0)
        xs = b->x0;
    if (xe > b->x0 + b->w)
        xe = b->x0 + b->w;
    if (ys >= ye || xs >= xe)
        return;
    const uint32_t *rows = gy.sprites + 16 * (o->code | ((o->flags >> 7) << 8));
    const uint16_t *colors = gy.spr_colors + 16 * o->color;
    uint16_t *row = b->buf + (ys - b->y0) * b->w - b->x0;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        const uint32_t *w = rows + 2 * (flipy ? py + 7 - y : y - py);
        for (int x = xs; x < xe; x++)
        {
            const int c = flipx ? px + 15 - x : x - px;
            const unsigned pen = (w[c >> 3] >> ((c & 7) << 2)) & 15;
            if (pen)
                row[x] = colors[pen];
        }
    }
}

/* the tiles: all of them, or (front) those in front of the sprites */
static void GAL_HOT(draw_tiles)(const gal_band_t *b, int front)
{
    const uint8_t *vram = gy.ram + RAM_VIDEO, *cram = gy.ram + RAM_COLOR;
    const int ry0 = 239 - (b->x0 + b->w - 1), ry1 = 239 - b->x0;
    int rx0 = b->y0 - VIEW_TOP, rx1 = b->y0 + b->h - 1 - VIEW_TOP;
    if (rx0 < 0)
        rx0 = 0;
    if (rx1 > 255)
        rx1 = 255;
    for (int row = ry0 >> 3; row <= ry1 >> 3; row++)
        for (int col = rx0 >> 3; col <= rx1 >> 3; col++)
        {
            const int i = row * 32 + col;
            const uint8_t attr = cram[i];
            if (front && !(attr & 0x10))
                continue;
            const int code = ((attr & 0x20) << 3) | vram[i];
            const uint16_t *colors = gy.tile_colors + 4 * (attr & 0x0f);
            /* behind the sprites, a blank tile of the background colour is
             * the band as cleared */
            if (!front && gy.tile_blank[code] && colors[0] == 0)
                continue;
            const uint16_t *rows = gy.tiles + 8 * code;
            /* raster flips (bit 6 x, bit 7 y) are upright flips in y and x */
            const int fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
            if (!fx && !fy)
            {
                gal_draw_tile_opaque(b, UP_X(8 * row, 8), UP_Y(8 * col), rows, colors);
                continue;
            }
            uint16_t t[8];
            for (int k = 0; k < 8; k++)
            {
                uint32_t v = rows[fy ? 7 - k : k];
                if (fx)
                    v = gal_rev2(v) >> 16;
                t[k] = (uint16_t)v;
            }
            gal_draw_tile_opaque(b, UP_X(8 * row, 8), UP_Y(8 * col), t, colors);
        }
}

static void GAL_HOT(gy_render_band)(const gal_band_t *b)
{
    GAL_PROF_START(t0);
    gal_fill_band(b, 0);
    draw_tiles(b, 0);
    GAL_PROF_ADD(6, t0);
    GAL_PROF_START(t1);
    for (int s = 0; s < gal.active_sprites; s++)
        draw_sprite(b, &gal.sprite[s]);
    draw_tiles(b, 1);
    GAL_PROF_ADD(7, t1);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t gy_hiscore[] = {
    {0x9488, 0x28, 0x00, 0x83},
    {0x940b, 0x03, 0x00, 0x01},
};

const gal_game_ops_t gal_gyruss_ops = {
    .init = gy_init,
    .reset = gy_reset,
    .run_slice = gy_run_slice,
    .prepare_frame = gy_prepare_frame,
    .render_band = gy_render_band,
    .sound = gy_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .large = 1,
    .nderived = 1,
    .derived_src = {4},
    .derived_size = {NSPRITES * SPR_BYTES},
    .derive = gy_derive,
    .hiscore = gy_hiscore,
    .nhiscore = sizeof(gy_hiscore) / sizeof(gy_hiscore[0]),
};
