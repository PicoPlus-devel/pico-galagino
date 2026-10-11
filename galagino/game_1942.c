/*
 * 1942 (Capcom, 1984), from upstream Galagino's 1942.h.
 *
 * A Z80 at 4 MHz with banked ROM at 8000-bfff, a Z80 at 3 MHz with two
 * AY-3-8910s at 1.5 MHz, a vertically scrolling playfield of 16x16 tiles at
 * 3 bits per pixel, 512 sprites of 16x16 at 4 bits per pixel and 2-bit
 * characters over it all. The raster is 256 lines: the game uses the upright
 * rows 16..271 (tile rows 2..33). The frame is 59.6 Hz (6 MHz / 384 / 262).
 *
 * Galagino's RAM buffer: main RAM e000-efff at 0000, characters d000-d7ff at
 * 1000, the sound CPU's RAM 4000-47ff at 1800, the playfield d800-dbff at 2000
 * and the sprites cc00-cc7f at 2400.
 *
 * Its converted data is about 233 KB, too much for SRAM: the game runs from
 * the PSRAM block (gal_set_psram), or on a board without PSRAM from the flash
 * store, which keeps the converted tiles and sprites as well (g42_derive). The
 * main and sound CPUs' ROMs, the characters and the colour tables are copied
 * to SRAM either way, as they are read all the time. Upstream keeps four flipped copies of the playfield tiles; here the
 * flips are done while drawing, which saves 192 KB.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#define MAIN_CYCLES_PER_FRAME 67072  /* 4 MHz / 59.637 Hz */
#define SOUND_CYCLES_PER_FRAME 50304 /* 3 MHz / 59.637 Hz */
#define STEPS_PER_QUARTER 16
#define AY_CLOCK 1500000
#define AY_VOL 4

static struct
{
    const uint8_t *rom_main;  /* 0000-7fff, SRAM */
    const uint8_t *bank[3];   /* 8000-bfff, PSRAM */
    const uint8_t *rom_sound; /* 0000-3fff, SRAM */
    uint32_t *tiles;          /* [512][16][2], 4 bits per pixel; PSRAM */
    uint32_t *sprites;        /* [512][16][2]; PSRAM */
    uint16_t *chars;          /* [512][8]; SRAM */
    uint16_t *cmap_tiles;     /* [16][32][8]; SRAM */
    uint16_t *cmap_chars;     /* [64][4]; SRAM */
    uint16_t *cmap_sprite;    /* [16][16]; SRAM */

    uint8_t bank_sel;
    uint8_t palette;
    uint16_t scroll;
    uint8_t sound_latch;
    uint8_t ay_addr[2];
    uint8_t sub_cpu_reset;
    uint8_t last_coin;
} g42;

/* --- Main CPU ---------------------------------------------------------------- */

static void select_bank(uint8_t bank)
{
    g42.bank_sel = bank;
    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[0x80 + p] = (bank < 3) ? g42.bank[bank] + p * 256 : NULL;
}

static uint8_t GAL_HOT(m_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    switch (addr)
    {
    case 0xc000: /* COIN1,COIN2,X,SERVICE1,X,START2,START1 */
    {
        uint8_t v = 0xff;
        if ((keys & GAL_BTN_COIN) && !g42.last_coin)
            v &= ~0x80;
        if (keys & GAL_BTN_START)
            v &= ~0x01;
        if (keys & GAL_BTN_START2)
            v &= ~0x02;
        g42.last_coin = (keys & GAL_BTN_COIN) != 0;
        return v;
    }
    case 0xc001: /* P1: X,X,B2,B1,U,D,L,R */
    {
        uint8_t v = 0xff;
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x01;
        if (keys & GAL_BTN_LEFT)
            v &= ~0x02;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x04;
        if (keys & GAL_BTN_UP)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        /* the loop; Galagino, short of buttons, puts it on START */
        if (keys & GAL_BTN_EXTRA)
            v &= ~0x20;
        return v;
    }
    case 0xc002: /* P2 */
        return 0xff;
    case 0xc003:
        return (uint8_t)~_1942_DIP_A;
    case 0xc004:
        return (uint8_t)~_1942_DIP_B;
    }
    return 0xff;
}

static void GAL_HOT(m_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    switch (addr)
    {
    case 0xc800:
        g42.sound_latch = value;
        break;
    case 0xc802:
        g42.scroll = (uint16_t)((g42.scroll & 0xff00) | value);
        break;
    case 0xc803:
        g42.scroll = (uint16_t)((g42.scroll & 0x00ff) | (value << 8));
        break;
    case 0xc804:
        /* bit 7: flip screen, bit 4: sound CPU reset, bit 0: coin counter */
        g42.sub_cpu_reset = (value & 0x10) != 0;
        if (g42.sub_cpu_reset)
            z80_reset(&gal.cpu[1]);
        break;
    case 0xc805:
        g42.palette = value;
        break;
    case 0xc806:
        select_bank(value);
        break;
    }
}

/* --- Sound CPU ---------------------------------------------------------------- */

static uint8_t GAL_HOT(s_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if (addr == 0x6000) /* the command from the main CPU */
        return g42.sound_latch;
    return 0xff;
}

static void GAL_HOT(s_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr & 0x8000) /* 8000: AY #1, c000: AY #2; A0 selects data or address */
    {
        const int ay = (addr & 0x4000) != 0;
        if (addr & 1)
            gal.soundregs[16 * ay + g42.ay_addr[ay]] = value;
        else
            g42.ay_addr[ay] = value & 15;
    }
}

static uint8_t io_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void io_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* cmapconv.py parse_palette with three PROMs: 4 bits each of red, green, blue */
static uint16_t palette_entry(const uint8_t *proms, int i)
{
    const unsigned r = (proms[i] & 15) << 1, g = (proms[0x100 + i] & 15) << 2, b = (proms[0x200 + i] & 15) << 1;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* tileconv.py parse_tile_1942: tiles first..first+count-1 of the 512, one
 * bitplane per 16 KB */
static void conv_tiles(const uint8_t *rom, uint32_t *out, int first, int count)
{
    for (int t = first; t < first + count; t++)
    {
        const uint8_t *b0 = rom + 32 * t, *b1 = rom + 0x4000 + 32 * t, *b2 = rom + 0x8000 + 32 * t;
        for (int y = 0; y < 16; y++)
        {
            const uint8_t bit = (uint8_t)(1 << (y & 7));
            uint32_t lo = 0, hi = 0;
            for (int x = 0; x < 16; x++)
            {
                const int byte = x + ((y / 8) ? 0 : 16);
                const uint32_t pix = ((b0[byte] & bit) ? 4 : 0) | ((b1[byte] & bit) ? 2 : 0) | ((b2[byte] & bit) ? 1 : 0);
                if (x < 8)
                    lo |= pix << (4 * x);
                else
                    hi |= pix << (4 * (x - 8));
            }
            *out++ = lo;
            *out++ = hi;
        }
    }
}

/* tileconv.py parse_chr_1942: 512 characters of 16 bytes */
static void conv_chars(const uint8_t *rom, uint16_t *out)
{
    for (int chr = 0; chr < 512; chr++)
    {
        const uint8_t *data = rom + 16 * chr;
        for (int y = 0; y < 8; y++)
        {
            uint16_t val = 0;
            for (int x = 0; x < 8; x++)
            {
                const uint8_t byte = (y < 4) ? data[2 * x + 1] : data[2 * x];
                const int c0 = (byte & (0x10 << (y & 3))) ? 1 : 0;
                const int c1 = (byte & (0x01 << (y & 3))) ? 2 : 0;
                val = (uint16_t)((val >> 2) | ((c0 + c1) << 14));
            }
            *out++ = val;
        }
    }
}

/* spriteconv.py parse_sprite_1942: sprites 0-255 from sr-14 (low) and sr-16
 * (high bitplanes), 256-511 from sr-15 and sr-17; first..first+count-1 */
static void conv_sprites(const uint8_t *rom, uint32_t *out, int first, int count)
{
    for (int s = first; s < first + count; s++)
    {
        const uint8_t *lo_rom = rom + (s / 256) * 0x4000 + 64 * (s % 256);
        const uint8_t *hi_rom = lo_rom + 0x8000;
        for (int y = 0; y < 16; y++)
        {
            uint32_t lo = 0, hi = 0;
            for (int x = 0; x < 16; x++)
            {
                const int byte = 2 * x + (((15 - y) & 4) >> 2) + (((15 - y) & 8) << 2);
                const int bit = (15 - y) & 3;
                const uint32_t pix = ((lo_rom[byte] & (0x80 >> bit)) ? 1 : 0) | ((lo_rom[byte] & (0x08 >> bit)) ? 2 : 0) |
                                     ((hi_rom[byte] & (0x80 >> bit)) ? 4 : 0) | ((hi_rom[byte] & (0x08 >> bit)) ? 8 : 0);
                if (x < 8)
                    lo |= pix << (4 * x);
                else
                    hi |= pix << (4 * (x - 8));
            }
            *out++ = lo;
            *out++ = hi;
        }
    }
}

/* The flash store keeps the converted tiles (0) and sprites (1); a tile or
 * sprite is 128 bytes. */
static void g42_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    if (i == 0)
        conv_tiles(src, (uint32_t *)out, (int)(ofs / 128), (int)(len / 128));
    else
        conv_sprites(src, (uint32_t *)out, (int)(ofs / 128), (int)(len / 128));
}

static int g42_init(uint8_t *const *regions)
{
    /* the CPUs' ROMs and the data read all the time go to SRAM */
    uint8_t *rom_main = (uint8_t *)gal_fast_alloc(0x8000);
    uint8_t *rom_sound = (uint8_t *)gal_fast_alloc(0x4000);
    g42.chars = (uint16_t *)gal_fast_alloc(512 * 8 * 2);
    g42.cmap_tiles = (uint16_t *)gal_fast_alloc(16 * 32 * 8 * 2);
    g42.cmap_chars = (uint16_t *)gal_fast_alloc(64 * 4 * 2);
    g42.cmap_sprite = (uint16_t *)gal_fast_alloc(16 * 16 * 2);
    /* the tiles and sprites: converted in the flash store, or converted here
     * into PSRAM */
    const int stored = regions[GAL_MAX_REGIONS] && regions[GAL_MAX_REGIONS + 1];
    if (stored)
    {
        g42.tiles = (uint32_t *)regions[GAL_MAX_REGIONS];
        g42.sprites = (uint32_t *)regions[GAL_MAX_REGIONS + 1];
    }
    else
    {
        g42.tiles = (uint32_t *)gal_alloc(512 * 16 * 2 * 4);
        g42.sprites = (uint32_t *)gal_alloc(512 * 16 * 2 * 4);
    }
    if (!rom_main || !rom_sound || !g42.chars || !g42.cmap_tiles || !g42.cmap_chars || !g42.cmap_sprite ||
        !g42.tiles || !g42.sprites)
        return 0;

    memcpy(rom_main, regions[0], 0x8000);
    memcpy(rom_sound, regions[4], 0x4000);
    g42.rom_main = rom_main;
    g42.rom_sound = rom_sound;
    for (int b = 0; b < 3; b++)
        g42.bank[b] = regions[1 + b];

    conv_chars(regions[5], g42.chars);
    if (!stored)
    {
        conv_tiles(regions[6], g42.tiles, 0, 512);
        conv_sprites(regions[7], g42.sprites, 0, 512);
    }

    /* cmapconv.py: characters use palette entries 128-143, sprites 64-79, the
     * playfield 0-63 through two lookup PROMs */
    const uint8_t *proms = regions[8];
    for (int i = 0; i < 64 * 4; i++)
        g42.cmap_chars[i] = gal_color(palette_entry(proms, 128 + (regions[9][i] & 15)));
    for (int i = 0; i < 16 * 16; i++)
        g42.cmap_sprite[i] = gal_color(palette_entry(proms, 64 + (regions[11][i] & 15)));
    /* parse_colormap_1942_tiles: sb-3 holds the low four bits of the palette
     * index, the low two bits of sb-2 the high ones; sb-4 picks the entries */
    const uint8_t *sb4 = regions[10], *sb3 = regions[10] + 0x100, *sb2 = regions[10] + 0x200;
    for (int tab = 0; tab < 16; tab++)
        for (int idx = 0; idx < 32; idx++)
            for (int k = 0; k < 8; k++)
            {
                const int a = 16 * tab + (sb4[8 * idx + k] & 15);
                g42.cmap_tiles[(tab * 32 + idx) * 8 + k] = gal_color(palette_entry(proms, sb3[a] + 16 * (sb2[a] & 3)));
            }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x80; p++)
        cpu->rd_page[p] = g42.rom_main + p * 256;
    for (int p = 0; p < 0x10; p++) /* e000-efff main RAM */
        cpu->rd_page[0xe0 + p] = cpu->wr_page[0xe0 + p] = gal.ram + p * 256;
    for (int p = 0; p < 8; p++) /* d000-d7ff characters */
        cpu->rd_page[0xd0 + p] = cpu->wr_page[0xd0 + p] = gal.ram + 0x1000 + p * 256;
    for (int p = 0; p < 4; p++) /* d800-dbff playfield */
        cpu->rd_page[0xd8 + p] = cpu->wr_page[0xd8 + p] = gal.ram + 0x2000 + p * 256;
    /* cc00-cc7f sprites: the page holds 256 bytes, the buffer is sized for it */
    cpu->rd_page[0xcc] = cpu->wr_page[0xcc] = gal.ram + 0x2400;
    cpu->read = m_read;
    cpu->write = m_write;
    cpu->in = io_in;
    cpu->out = io_out;

    cpu = &gal.cpu[1];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[p] = g42.rom_sound + p * 256;
    for (int p = 0; p < 8; p++) /* 4000-47ff RAM */
        cpu->rd_page[0x40 + p] = cpu->wr_page[0x40 + p] = gal.ram + 0x1800 + p * 256;
    cpu->read = s_read;
    cpu->write = s_write;
    cpu->in = io_in;
    cpu->out = io_out;
    return 1;
}

static void g42_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    g42.palette = 0;
    g42.scroll = 0;
    g42.sound_latch = 0;
    g42.ay_addr[0] = g42.ay_addr[1] = 0;
    g42.sub_cpu_reset = 0;
    g42.last_coin = 0;
    for (int c = 0; c < 2; c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    select_bank(0);
    gal_ay_reset();
    gal.game_started = 1; /* as in Galagino */
}

/* Both CPUs spend most of a frame in a loop waiting for their next interrupt,
 * and emulating that loop costs as much as real work. Interrupts are only
 * raised between z80_run() calls, and with none pending a CPU in its wait loop
 * stays there, so those cycles are skipped instead of run:
 *   - the main CPU's task scheduler (0087-009a) scans the 16 task slots at
 *     efc0, every fourth byte, for one of 20h or more. Once a scan comes up
 *     empty only an interrupt handler can fill a slot: nothing else writes
 *     the main CPU's RAM.
 *   - the sound CPU waits in EI / JR $-1 (0064-0065).
 * The set is matched by CRC, so these addresses are fixed. R is not kept up to
 * date; neither program reads it. */
static int GAL_HOT(main_idle)(const z80_t *cpu)
{
    if (cpu->irq_state || cpu->pc.w < 0x0087 || cpu->pc.w > 0x009a || cpu->sp.w != 0xefbe)
        return 0;
    for (int i = 0; i < 16; i++)
        if (gal.ram[0x0fc0 + 4 * i] >= 0x20)
            return 0;
    return 1;
}

static int GAL_HOT(sound_idle)(const z80_t *cpu)
{
    return !cpu->irq_state && (cpu->pc.w == 0x0064 || cpu->pc.w == 0x0065);
}

static void GAL_HOT(run_cpu)(z80_t *cpu, int cycles, int idle)
{
    if (!idle)
    {
        z80_run(cpu, cycles);
        return;
    }
    /* as if the loop had run for exactly `cycles` */
    cpu->icount += cycles;
    if (cpu->icount > 0)
        cpu->icount = 0;
}

static void GAL_HOT(g42_run_slice)(int q)
{
    for (int i = 0; i < STEPS_PER_QUARTER; i++)
    {
        const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
        run_cpu(&gal.cpu[0], (step + 1) * MAIN_CYCLES_PER_FRAME / steps - step * MAIN_CYCLES_PER_FRAME / steps,
                main_idle(&gal.cpu[0]));
        if (!g42.sub_cpu_reset)
            run_cpu(&gal.cpu[1], (step + 1) * SOUND_CYCLES_PER_FRAME / steps - step * SOUND_CYCLES_PER_FRAME / steps,
                    sound_idle(&gal.cpu[1]));
    }

    /* the main CPU twice a frame: RST 10h and RST 08h */
    if (q == 1)
        z80_hold_irq(&gal.cpu[0], 0xd7);
    else if (q == 3)
        z80_hold_irq(&gal.cpu[0], 0xcf);
    /* the sound CPU four times; it updates the AYs once per interrupt */
    if (!g42.sub_cpu_reset)
        z80_hold_irq(&gal.cpu[1], 0xff);
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(g42_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 32 && gal.active_sprites < GAL_MAX_SPRITES - 4; idx++)
    {
        const uint8_t *base = gal.ram + 0x2400 + 4 * (31 - idx);
        if (!base[3] || !base[2])
            continue;

        gal_sprite_t spr;
        /* 512 sprites: the ninth code bit goes in flags bit 0 */
        spr.code = (uint8_t)((base[0] & 0x7f) + 4 * (base[1] & 0x20));
        spr.color = base[1] & 0x0f;
        spr.x = base[2] - 16;
        spr.y = 256 - (base[3] - 0x10 * (base[1] & 0x10));
        spr.flags = (base[0] & 0x80) >> 7;
        if (idx >= 8 && idx < 16)
            spr.flags |= 0x80; /* left half of the screen only */
        if (idx < 8)
            spr.flags |= 0x40; /* right half of the screen only */

        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
        {
            gal.sprite[gal.active_sprites++] = spr;
            /* attribute bits 7,6: double and quadruple height (width upright) */
            if (base[1] & 0xc0)
            {
                const int copies = (base[1] & 0x80) ? 3 : 1;
                for (int c = 0; c < copies; c++)
                {
                    spr.x += 16;
                    spr.code += 1;
                    if (spr.x >= GAL_W)
                        break;
                    gal.sprite[gal.active_sprites++] = spr;
                }
            }
        }
    }
}

/* A row of a playfield tile, 16 pixels at 4 bits (3 used), the first one in
 * the low bits of lo; written to d[0], d[step] ... d[15 * step]. */
static inline __attribute__((always_inline)) void put_tile_row(uint16_t *d, const int step, uint32_t lo, uint32_t hi,
                                                               const uint16_t *colors)
{
#pragma GCC unroll 8
    for (int p = 0; p < 8; p++, lo >>= 4)
        d[step * p] = colors[lo & 7];
#pragma GCC unroll 8
    for (int p = 8; p < 16; p++, hi >>= 4)
        d[step * p] = colors[hi & 7];
}

/* One line of the playfield: 14 tiles of 16 pixels, opaque. */
static void GAL_HOT(bg_line)(const gal_band_t *b, int y)
{
    /* blit_bgtile_row_1942: the first line of a row comes from the scroll
     * register; up to 8 lines come from one row of tiles, the rest from the
     * next one */
    const int row = y / 8 + 30, j = y & 7;
    const int line = (-8 * row + g42.scroll) & 511;
    const int yoffset = (16 - line) & 15;
    const int lines2draw = (yoffset > 8) ? 16 - yoffset : 8;
    int addr, trow;
    if (j < lines2draw)
    {
        addr = 0x2000 + 32 * (((line - 1) & 511) / 16) + 1;
        trow = yoffset + j;
    }
    else
    {
        addr = 0x2000 + 32 * (((line - 17) & 511) / 16) + 1;
        trow = j - lines2draw;
    }

    /* dst[x] is pixel x of the picture */
    uint16_t *dst = b->buf + (y - b->y0) * b->w - b->x0;
    const uint16_t *cmap = g42.cmap_tiles + (g42.palette & 15) * 32 * 8;
    const int x0 = b->x0, x1 = b->x0 + b->w;
    for (int col = x0 >> 4; col <= (x1 - 1) >> 4; col++)
    {
        const uint8_t attr = gal.ram[addr + col + 16];
        const uint16_t *colors = cmap + 8 * (attr & 31);
        const int chr = gal.ram[addr + col] + ((attr & 0x80) << 1);
        /* attribute bit 5 flips the tile in y, bit 6 in x */
        const uint32_t *r = g42.tiles + 32 * chr + 2 * ((attr & 0x20) ? 15 - trow : trow);
        const uint32_t lo = r[0], hi = r[1];
        uint16_t *d = dst + 16 * col;
        if (16 * col >= x0 && 16 * col + 16 <= x1)
        {
            /* the whole tile: the band is the full width when upright */
            if (attr & 0x40)
                put_tile_row(d + 15, -1, lo, hi, colors);
            else
                put_tile_row(d, 1, lo, hi, colors);
            continue;
        }
        const int xs = (16 * col > x0) ? 16 * col : x0, xe = (16 * col + 16 < x1) ? 16 * col + 16 : x1;
        for (int x = xs; x < xe; x++)
        {
            const int p = (attr & 0x40) ? 15 - (x & 15) : (x & 15);
            d[x & 15] = colors[((p < 8 ? lo : hi) >> (4 * (p & 7))) & 7];
        }
    }
}

/* A 16x16 sprite at 4 bits per pixel, pen 15 transparent, clipped to the
 * band and to [clip_x0, clip_x1). */
static void GAL_HOT(draw_sprite4)(const gal_band_t *b, const gal_sprite_t *spr, int clip_x0, int clip_x1)
{
    const uint32_t *rows = g42.sprites + 32 * (spr->code + 256 * (spr->flags & 1));
    const uint16_t *colors = g42.cmap_sprite + 16 * (spr->color & 15);
    int ys = spr->y, ye = spr->y + 16, xs = spr->x, xe = spr->x + 16;
    if (ys < b->y0)
        ys = b->y0;
    if (ye > b->y0 + b->h)
        ye = b->y0 + b->h;
    if (xs < b->x0)
        xs = b->x0;
    if (xe > b->x0 + b->w)
        xe = b->x0 + b->w;
    if (xs < clip_x0)
        xs = clip_x0;
    if (xe > clip_x1)
        xe = clip_x1;
    for (int y = ys; y < ye; y++)
    {
        const uint32_t *r = rows + 2 * (y - spr->y);
        const uint64_t pix = ((uint64_t)r[1] << 32) | r[0];
        uint16_t *dst = b->buf + (y - b->y0) * b->w - b->x0;
        for (int x = xs; x < xe; x++)
        {
            const int c = (pix >> (4 * (x - spr->x))) & 15;
            if (c != 15)
                dst[x] = colors[c];
        }
    }
}

static void GAL_HOT(g42_render_band)(const gal_band_t *b)
{
    for (int y = b->y0; y < b->y0 + b->h; y++)
        bg_line(b, y);

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        int x0 = 0, x1 = GAL_W;
        if (spr->flags & 0x80) /* left half only */
            x1 = 113;
        if (spr->flags & 0x40) /* right half only */
            x0 = 113;
        draw_sprite4(b, spr, x0, x1);
    }

    /* the characters, the screen turned 180 degrees from the Namco boards */
    const int row_first = b->y0 / 8, row_last = (b->y0 + b->h - 1) / 8;
    for (int row = row_first; row <= row_last; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[35 - row][27 - col];
            int chr = gal.ram[0x1000 + addr];
            if (chr == 0x30) /* empty */
                continue;
            const uint8_t attr = gal.ram[0x1400 + addr];
            if (attr & 0x80)
                chr += 256;
            gal_draw_tile(b, 8 * col, 8 * row, g42.chars + 8 * chr, g42.cmap_chars + 4 * (attr & 63));
        }
    }
}

static void GAL_HOT(g42_sound)(int16_t *out, int n)
{
    gal_ay_render(2, AY_CLOCK, AY_VOL, out, n);
}

const gal_game_ops_t gal_1942_ops = {
    .init = g42_init,
    .reset = g42_reset,
    .run_slice = g42_run_slice,
    .prepare_frame = g42_prepare_frame,
    .render_band = g42_render_band,
    .sound = g42_sound,
    .view_top = 16,
    .view_h = 256,
    .needs_psram = 1,
    .nderived = 2,
    .derived_src = {6, 7},
    .derived_size = {512 * 16 * 2 * 4, 512 * 16 * 2 * 4},
    .derive = g42_derive,
};

#ifdef GAL_CONV_CHECK
int gal_tables_1942(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"_1942_rom_cpu1", g42.rom_main, 0x8000};
    t[n++] = (gal_table_t){"_1942_rom_cpu1_b0", g42.bank[0], 0x4000};
    t[n++] = (gal_table_t){"_1942_rom_cpu1_b1", g42.bank[1], 0x2000};
    t[n++] = (gal_table_t){"_1942_rom_cpu1_b2", g42.bank[2], 0x4000};
    t[n++] = (gal_table_t){"_1942_rom_cpu2", g42.rom_sound, 0x4000};
    t[n++] = (gal_table_t){"sr_02_f2", g42.chars, 512 * 8 * 2};
    t[n++] = (gal_table_t){"sr_08_a1", g42.tiles, 512 * 16 * 2 * 4};
    t[n++] = (gal_table_t){"_1942_sprites", g42.sprites, 512 * 16 * 2 * 4};
    t[n++] = (gal_table_t){"_1942_colormap_chars", g42.cmap_chars, 64 * 4 * 2};
    t[n++] = (gal_table_t){"_1942_colormap_sprites", g42.cmap_sprite, 16 * 16 * 2};
    t[n++] = (gal_table_t){"_1942_colormap_tiles", g42.cmap_tiles, 16 * 32 * 8 * 2};
    return n;
}
#endif
