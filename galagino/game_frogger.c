/*
 * Frogger (Konami, 1981), from upstream Galagino's frogger.h.
 *
 * Galaxian-class hardware: a Z80 at 3.072 MHz with the playfield and a Z80 at
 * 1.79 MHz (14.318 MHz / 8) driving one AY-3-8910. The raster is 256 lines, so
 * the game uses the upright rows 16..271 (tile rows 2..33).
 *
 * Galagino's RAM buffer holds the main RAM 8000-87ff at 0000, the video RAM
 * a800-abff at 0800, the object RAM b000-b0ff at 0c00 and the sound CPU's
 * RAM 4000-43ff at 1000.
 *
 * Where Galagino approximates timing with instruction counts, this follows
 * MAME's galaxian.cpp: the frame is 50688 main CPU cycles (60.6 Hz), the AY's
 * port B timer is derived from the sound CPU's cycle count, and the sound IRQ
 * is a HOLD_LINE raised by the falling edge of control bit 3.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#define MAIN_CYCLES_PER_FRAME 50688
#define SOUND_CLOCK 1789772 /* 14.318181 MHz / 8 */
#define SOUND_CYCLES_PER_FRAME 29531 /* SOUND_CLOCK / 60.606 Hz */
#define STEPS_PER_QUARTER 16
#define AY_VOL 11

static struct
{
    const uint8_t *rom[2];
    uint16_t *tiles;     /* [256][8] */
    uint32_t *sprites;   /* [64][16] */
    uint32_t *sprites_x; /* the same flipped in x */
    uint16_t colormap[8][4];
    uint16_t water;

    uint8_t nmi_enable;
    uint8_t snd_command; /* AY port A */
    uint8_t snd_control; /* the sound control port of the second 8255 */
    uint8_t ay_reg;
    uint64_t snd_cycles; /* sound CPU cycles before the current run */
    int32_t snd_run_start;
} fr;

/* --- Main CPU ---------------------------------------------------------------- */

static uint8_t GAL_HOT(fr_main_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if (addr == 0x4000) /* the game reads $4000 and jumps to $4001 if it is $55 */
        return 0x00;
    if ((addr & 0xf800) == 0x8800) /* watchdog */
        return 0x00;
    if ((addr & 0xc000) == 0xc000)
    {
        const uint16_t keys = gal.buttons;
        uint8_t v = 0xff;
        /* e000 = IN0: Coin1,Coin0,L1,R1,Fire1A,CREDIT,Fire1B,U2 */
        if (addr == 0xe000)
        {
            if (keys & GAL_BTN_LEFT)
                v &= ~0x20;
            if (keys & GAL_BTN_RIGHT)
                v &= ~0x10;
            if (keys & GAL_BTN_FIRE)
                v &= ~0x08;
            if (keys & GAL_BTN_COIN)
                v &= ~0x04;
        }
        /* e002 = IN1: START1,START2,L2,R2,Fire2A,Fire2B,LIVES1,LIVES0 */
        else if (addr == 0xe002)
        {
            v = FROGGER_DIP1 | 0xfc;
            if (keys & GAL_BTN_START)
                v &= ~0x80;
            if (keys & GAL_BTN_START2)
                v &= ~0x40;
        }
        /* e004 = IN2: -,D1,-,U1,UPRIGHT,COINS_P_PLAY,D2 */
        else if (addr == 0xe004)
        {
            v = FROGGER_DIP2 | 0xf9;
            if (keys & GAL_BTN_UP)
                v &= ~0x10;
            if (keys & GAL_BTN_DOWN)
                v &= ~0x40;
        }
        return v;
    }
    return 0xff;
}

static void GAL_HOT(fr_main_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xf800) == 0xb800)
    {
        if ((addr & 0x1c) == 0x08) /* b808: NMI enable */
        {
            fr.nmi_enable = value & 1;
            if (!fr.nmi_enable)
                z80_set_nmi_line(cpu, 0);
        }
        return; /* b80c/b810 flip, b818/b81c coin counters */
    }
    if (addr == 0xd000) /* 8255 #2 port A: the sound command, AY port A */
        fr.snd_command = value;
    else if (addr == 0xd002) /* 8255 #2 port B: sound control */
    {
        /* the falling edge of bit 3 interrupts the sound CPU until it acknowledges */
        if ((fr.snd_control & 0x08) && !(value & 0x08))
            z80_hold_irq(&gal.cpu[1], 0xff);
        fr.snd_control = value;
    }
}

static uint8_t fr_main_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void fr_main_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Sound CPU ---------------------------------------------------------------- */

/* MAME konami_sound_timer_r: a 40960-step counter clocked at 8x the sound
 * CPU clock; Frogger has bits 3 and 5 swapped. */
static uint8_t fr_sound_timer(const z80_t *cpu)
{
    const uint64_t now = fr.snd_cycles + (uint64_t)(fr.snd_run_start - cpu->icount);
    uint32_t cycles = (uint32_t)((now * 8) % (16 * 16 * 2 * 8 * 5 * 2));
    uint8_t hibit = 0;
    if (cycles >= 16 * 16 * 2 * 8 * 5)
    {
        hibit = 1;
        cycles -= 16 * 16 * 2 * 8 * 5;
    }
    const uint8_t v = (uint8_t)((hibit << 7) | (((cycles >> 14) & 1) << 6) | (((cycles >> 13) & 1) << 5) |
                                (((cycles >> 11) & 1) << 4) | 0x0e);
    /* bitswap<8>(v, 7,6,3,4,5,2,1,0) */
    return (uint8_t)((v & 0xc7) | ((v & 0x08) << 2) | ((v & 0x20) >> 2) | (v & 0x10));
}

static uint8_t GAL_HOT(fr_snd_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    (void)addr;
    return 0xff;
}

static void GAL_HOT(fr_snd_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    (void)addr;
    (void)value; /* 6000-6fff: the RC filters of the AY outputs, not emulated */
}

static uint8_t GAL_HOT(fr_snd_in)(z80_t *cpu, uint16_t port)
{
    if (port & 0x40) /* AY data */
    {
        if (fr.ay_reg < 14)
            return gal.soundregs[fr.ay_reg];
        if (fr.ay_reg == 14)
            return fr.snd_command;
        return fr_sound_timer(cpu);
    }
    return 0xff;
}

static void GAL_HOT(fr_snd_out)(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    if (port & 0x40) /* AY data */
        gal.soundregs[fr.ay_reg] = value;
    else if (port & 0x80) /* AY register select */
        fr.ay_reg = value & 15;
}

/* --- Life cycle ---------------------------------------------------------------- */

static int fr_init(uint8_t *const *regions)
{
    fr.rom[0] = regions[0];
    fr.rom[1] = regions[1];
    fr.tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    fr.sprites = (uint32_t *)gal_alloc(64 * 16 * 4);
    fr.sprites_x = (uint32_t *)gal_alloc(64 * 16 * 4);
    if (!fr.tiles || !fr.sprites || !fr.sprites_x)
        return 0;

    /* "the first ROM of the second CPU has data lines D0 and D1 swapped" */
    for (int i = 0; i < 0x800; i++)
    {
        const uint8_t b = regions[1][i];
        regions[1][i] = (uint8_t)((b & 0xfc) | ((b & 2) >> 1) | ((b & 1) << 1));
    }

    gal_conv_chars_2(regions[2], regions[2] + 0x800, fr.tiles);
    gal_conv_sprites_frogger(regions[2], regions[2] + 0x800, fr.sprites, fr.sprites_x);

    /* cmapconv.py dump_palette: 8 groups of four of the 32 palette entries */
    for (int i = 0; i < 32; i++)
        fr.colormap[i / 4][i % 4] = gal_color(gal_prom_bgr233(regions[3][i]));
    /* the river: Galagino fills with bytes of 8, RGB565 0x0808 */
    fr.water = gal_color(0x0808);

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x30; p++)
        cpu->rd_page[p] = fr.rom[0] + p * 256;
    for (int p = 0; p < 8; p++) /* 8000-87ff RAM */
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = gal.ram + p * 256;
    for (int p = 0; p < 8; p++) /* a800-abff video RAM, mirrored at ac00 */
        cpu->rd_page[0xa8 + p] = cpu->wr_page[0xa8 + p] = gal.ram + 0x800 + (p & 3) * 256;
    for (int p = 0; p < 8; p++) /* b000-b0ff object RAM, mirrored up to b7ff */
        cpu->rd_page[0xb0 + p] = cpu->wr_page[0xb0 + p] = gal.ram + 0xc00;
    cpu->read = fr_main_read;
    cpu->write = fr_main_write;
    cpu->in = fr_main_in;
    cpu->out = fr_main_out;

    cpu = &gal.cpu[1];
    for (int m = 0; m < 0x100; m += 0x80) /* A15 is not decoded */
    {
        for (int p = 0; p < 0x18; p++)
            cpu->rd_page[m + p] = fr.rom[1] + p * 256;
        for (int p = 0x40; p < 0x60; p++) /* 4000-43ff RAM, mirrored up to 5fff */
            cpu->rd_page[m + p] = cpu->wr_page[m + p] = gal.ram + 0x1000 + (p & 3) * 256;
    }
    cpu->read = fr_snd_read;
    cpu->write = fr_snd_write;
    cpu->in = fr_snd_in;
    cpu->out = fr_snd_out;
    return 1;
}

static void fr_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    fr.nmi_enable = 0;
    fr.snd_command = 0;
    fr.snd_control = 0;
    fr.ay_reg = 0;
    fr.snd_cycles = 0;
    gal.game_started = 0;
    for (int c = 0; c < 2; c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    gal_ay_reset();
}

static void GAL_HOT(fr_run_slice)(int q)
{
    for (int i = 0; i < STEPS_PER_QUARTER; i++)
    {
        const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
        const int main_cycles =
            (step + 1) * MAIN_CYCLES_PER_FRAME / steps - step * MAIN_CYCLES_PER_FRAME / steps;
        const int snd_cycles =
            (step + 1) * SOUND_CYCLES_PER_FRAME / steps - step * SOUND_CYCLES_PER_FRAME / steps;

        z80_run(&gal.cpu[0], main_cycles);

        z80_t *snd = &gal.cpu[1];
        fr.snd_run_start = snd->icount + snd_cycles;
        z80_run(snd, snd_cycles);
        fr.snd_cycles += (uint32_t)(fr.snd_run_start - snd->icount);
    }

    if (q == 3)
    {
        /* tile 800 shows "1" once the game has booted, as in Galagino */
        if (gal.ram[0x800 + 800] == 1)
            gal.game_started = 1;
        /* VBLANK: NMI while enabled */
        if (fr.nmi_enable)
        {
            z80_set_nmi_line(&gal.cpu[0], 1);
            z80_set_nmi_line(&gal.cpu[0], 0);
        }
    }
}

/* --- Video --------------------------------------------------------------------- */

/* Frogger's palette bits: colour index bits 2,1,0 from attribute bits 0,2,1 */
static inline int fr_color(int c)
{
    c &= 7;
    return ((c >> 1) & 0x03) | ((c << 2) & 0x04);
}

static void GAL_HOT(fr_prepare_frame)(void)
{
    /* 8 sprites at 0c40; drawn from 7 to 0, so 0 is on top */
    gal.active_sprites = 0;
    for (int idx = 7; idx >= 0; idx--)
    {
        const uint8_t *base = gal.ram + 0xc40 + 4 * idx;
        if (!base[3])
            continue;
        gal_sprite_t spr;
        const int x = base[0];
        spr.x = (((x << 4) & 0xf0) | ((x >> 4) & 0x0f)) - 16;
        spr.y = base[3] + 16;
        spr.color = (uint8_t)fr_color(base[2]);
        spr.code = base[1] & 0x3f;
        /* MAME: attribute bit 6 flips the raw raster in x, bit 7 in y; upright
         * that is a flip in y (1) and in x (2) */
        spr.flags = (uint8_t)(((base[1] & 0x40) ? 1 : 0) | ((base[1] & 0x80) ? 2 : 0));
        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
            gal.sprite[gal.active_sprites++] = spr;
    }
}

static void GAL_HOT(fr_render_band)(const gal_band_t *b)
{
    /* the river half has a blue background */
    for (int y = b->y0; y < b->y0 + b->h; y++)
    {
        const uint16_t c = (y >= 16 && y < 18 * 8) ? fr.water : 0;
        uint16_t *p = b->buf + (y - b->y0) * b->w;
        for (int x = 0; x < b->w; x++)
            p[x] = c;
    }

    const int row_first = b->y0 / 8 < 2 ? 2 : b->y0 / 8;
    const int row_last = (b->y0 + b->h - 1) / 8 > 33 ? 33 : (b->y0 + b->h - 1) / 8;
    for (int row = row_first; row <= row_last; row++)
    {
        /* each row scrolls on its own (water 8..17, road 20..29) */
        uint8_t scroll = gal.ram[0xc00 + 2 * (row - 2)];
        scroll = (uint8_t)(((scroll << 4) & 0xf0) | ((scroll >> 4) & 0x0f));
        const int sub = scroll & 7;
        const int addr_add = (scroll & ~7) << 2;
        /* the leftmost tile is only partly visible when the scroll is not a multiple of 8 */
        int col_first = (b->x0 - sub) / 8, col_last = (b->x0 + b->w - 1 - sub) / 8;
        if (b->x0 - sub < 0)
            col_first = -1;
        if (col_last > 27)
            col_last = 27;
        for (int col = col_first; col <= col_last; col++)
        {
            unsigned addr =
                (col >= 0) ? tileaddr[row][col] + addr_add : tileaddr[row][0] + 32 + addr_add;
            addr &= 1023;
            const int c = fr_color(gal.ram[0xc00 + 2 * (addr & 31) + 1]);
            gal_draw_tile(b, 8 * col + sub, 8 * row, fr.tiles + 8 * gal.ram[0x800 + addr], fr.colormap[c]);
        }
    }

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint32_t *rows = ((spr->flags & 2) ? fr.sprites_x : fr.sprites) + 16 * spr->code;
        gal_draw_sprite_ex(b, spr->x, spr->y, rows, spr->flags & 1, fr.colormap[spr->color], 1);
    }
}

static void GAL_HOT(fr_sound)(int16_t *out, int n)
{
    gal_ay_render(1, SOUND_CLOCK, AY_VOL, out, n);
}


/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t fr_hiscore[] = {
    {0x83f1, 0x0a, 0x63, 0x01},
    {0x83ef, 0x02, 0x63, 0x04},
};

const gal_game_ops_t gal_frogger_ops = {
    .init = fr_init,
    .reset = fr_reset,
    .run_slice = fr_run_slice,
    .prepare_frame = fr_prepare_frame,
    .render_band = fr_render_band,
    .sound = fr_sound,
    .view_top = 16,
    .view_h = 256,
    .hiscore = fr_hiscore,
    .nhiscore = sizeof(fr_hiscore) / sizeof(fr_hiscore[0]),
};

#ifdef GAL_CONV_CHECK
int gal_tables_frogger(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"frogger_rom_cpu1", fr.rom[0], 0x3000};
    t[n++] = (gal_table_t){"frogger_rom_cpu2", fr.rom[1], 0x1800};
    t[n++] = (gal_table_t){"frogger_606", fr.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"frogger_sprites", fr.sprites, 64 * 16 * 4};
    t[n++] = (gal_table_t){"frogger_sprites_x", fr.sprites_x, 64 * 16 * 4};
    t[n++] = (gal_table_t){"frogger_colormap", fr.colormap, sizeof(fr.colormap)};
    return n;
}
#endif
