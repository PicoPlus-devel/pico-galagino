/*
 * Konami's Time Pilot (1982) and Pooyan (1982), after MAME's konami/timeplt.cpp
 * and pooyan.cpp, on the sound board of konami_snd.c.
 *
 * A Z80 at 3.07 MHz, its NMI at the vertical blank while enabled; a latch of
 * eight bits for the NMI enable, the sound CPU's interrupt, the video enable
 * and the coin counters. 256x224 shown (MAME ROT90: the upright picture is
 * 224x256, drawn at y 16..271). One layer of 8x8 tiles, each drawn behind or
 * in front of the 24 sprites of 16x16; two bits per pixel, colours through
 * lookup PROMs into 32 colours.
 *
 * Pooyan: the same, but 256 tiles and 64 sprites of four bits per pixel, the
 * tiles always behind; its sprites are transparent where their lookup gives
 * colour 0.
 *
 * Roc'n Rope (1983, rocnrope.cpp): Pooyan's video with 512 tiles and 256
 * sprites, turned the other way (MAME ROT270), on a KONAMI-1 (a 6809 that
 * decrypts its opcodes) at 1.54 MHz whose IRQ vectors are written by the
 * program. Large: the tiles and sprites are read from PSRAM or the flash
 * cache. B is the second button (the flash).
 */
#include "gal_internal.h"

#include "konami_snd.h"
#include "m6809.h"

#define CYCLES_PER_FRAME 51200 /* 3.072 MHz at 60 Hz */
#define STEPS 16
#define VIEW_TOP 16

/* Galagino's RAM buffer */
#define RAM_COLOR 0x0000  /* a000-a3ff */
#define RAM_VIDEO 0x0400  /* a400-a7ff */
#define RAM_WORK 0x0800   /* a800-afff */
#define RAM_SPR0 0x1000   /* b000-b0ff */
#define RAM_SPR1 0x1100   /* b400-b4ff */
#define RAM_SOUND 0x1400  /* the sound CPU's 3000-33ff */

static struct
{
    uint8_t pooyan;
    int cycles_per_frame;
    uint8_t *rom;
    uint32_t *tiles4, *sprites4;   /* Pooyan: [256][8], [64][32] */
    uint16_t spr_mask[16];         /* Pooyan: the transparent pens of each colour */
    uint16_t *tiles;   /* [512][8] */
    uint32_t *sprites; /* [256][16] */
    uint16_t *tile_colors, *spr_colors; /* [32][4], [64][4] */
    uint8_t nmi_enable, video_enable;
    int run_base, run_len;
    /* Roc'n Rope */
    uint8_t rocnrope;
    m6809_state *m6809;
    uint8_t *vectors; /* fff2-fffd, written at 8182-818d */
    const uint32_t *rr_tiles, *rr_sprites; /* [512][8], [256][32] */
} tp;

/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t chr_layout = {
    8, 8, 2, {4, 0}, {0, 1, 2, 3, 64, 65, 66, 67}, {0, 8, 16, 24, 32, 40, 48, 56}, 16 * 8};

/* Pooyan: four planes, two in each half of the ROMs */
static const gal_layout_t py_chr_layout = {
    8, 8, 4, {0x1000 * 8 + 4, 0x1000 * 8, 4, 0}, {0, 1, 2, 3, 64, 65, 66, 67}, {0, 8, 16, 24, 32, 40, 48, 56}, 16 * 8};
static const gal_layout_t py_spr_layout = {
    16, 16, 4, {0x1000 * 8 + 4, 0x1000 * 8, 4, 0},
    {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195},
    {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312},
    64 * 8};

static const gal_layout_t spr_layout = {
    16, 16, 2, {4, 0},
    {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195},
    {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312},
    64 * 8};

/* --- Main CPU ---------------------------------------------------------------------- */

static uint8_t tp_read(z80_t *cpu, uint16_t addr)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    if ((addr & 0xf300) == 0xc000)
    {
        /* the raster line */
        const int c = tp.run_base + (tp.run_len - cpu->icount);
        return (uint8_t)(c * 256 / CYCLES_PER_FRAME);
    }
    if ((addr & 0xf300) == 0xc200)
        return 0x4b; /* DSW1, MAME's defaults: 3 lives, upright, bonus at 10000/50000, normal */
    if ((addr & 0xf300) != 0xc300)
        return 0xff;
    switch (addr & 0x60)
    {
    case 0x00: /* IN0, active low */
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        return v;
    case 0x20: /* IN1 */
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
    case 0x40: /* IN2: the second player of a cocktail cabinet */
        return 0xff;
    default: /* DSW0: 1C1C */
        return 0xff;
    }
}

static void tp_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xf300) == 0xc000)
        konami_snd_command(value);
    else if ((addr & 0xf3f0) == 0xc300)
    {
        /* the latch: bit (addr >> 1) & 7 */
        const int bit = value & 1;
        switch ((addr >> 1) & 7)
        {
        case 0:
            tp.nmi_enable = (uint8_t)bit;
            if (!bit)
                z80_set_nmi_line(cpu, 0);
            break;
        case 2:
            konami_snd_trigger(bit);
            break;
        case 4:
            tp.video_enable = (uint8_t)bit;
            break;
        default:
            break; /* 1 the flip, 3 the mute, 5 and 6 the coin counters */
        }
    }
    /* c200: the watchdog */
}

/* Pooyan's I/O: inputs at a000-a0e0, the command at a100, the latch at a180 */
static uint8_t py_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    switch (addr & 0xa1e0)
    {
    case 0xa000:
        return 0x7b; /* DSW1, MAME's defaults: 3 lives, upright, bonus at 50000/80000 */
    case 0xa080: /* IN0, active low */
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        return v;
    case 0xa0a0: /* IN1 */
        if (keys & GAL_BTN_UP)
            v &= ~0x04;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        return v;
    default: /* IN2 (the cocktail cabinet's second player), DSW0: 1C1C */
        return 0xff;
    }
}

static void py_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xa180) == 0xa100)
        konami_snd_command(value);
    else if ((addr & 0xa180) == 0xa180)
    {
        const int bit = value & 1;
        switch (addr & 7)
        {
        case 0:
            tp.nmi_enable = (uint8_t)bit;
            if (!bit)
                z80_set_nmi_line(cpu, 0);
            break;
        case 1:
            konami_snd_trigger(bit);
            break;
        default:
            break; /* 2 the mute, 3 and 4 the coin counters, 7 the flip */
        }
    }
    /* a000: the watchdog */
}

/* --- Set-up ----------------------------------------------------------------------- */

static uint16_t tp_rgb(const uint8_t *prom, int i)
{
    const uint8_t lo = prom[i], hi = prom[i + 32];
    static const uint8_t w[5] = {0x19, 0x24, 0x35, 0x40, 0x4d};
    int r = 0, g = 0, b = 0;
    for (int k = 0; k < 5; k++)
    {
        r += ((hi >> (1 + k)) & 1) * w[k];
        b += ((lo >> (3 + k)) & 1) * w[k];
    }
    g = ((hi >> 6) & 1) * w[0] + ((hi >> 7) & 1) * w[1] + (lo & 1) * w[2] + ((lo >> 1) & 1) * w[3] + ((lo >> 2) & 1) * w[4];
    return gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}

/* MAME's pooyan palette: resistors of 1K, 470 and 220 ohm against 1K */
static void py_palette(const uint8_t *prom, uint16_t *pal)
{
    const double rg[3] = {1000, 470, 220}, bl[2] = {470, 220};
    double s_rg = 0, s_b = 0;
    for (int k = 0; k < 3; k++)
        s_rg += 1 / rg[k];
    for (int k = 0; k < 2; k++)
        s_b += 1 / bl[k];
    const double out_rg = s_rg / (s_rg + 0.001), out_b = s_b / (s_b + 0.001);
    const double scale = 255 / (out_rg > out_b ? out_rg : out_b);
    double wrg[3], wb[2];
    for (int k = 0; k < 3; k++)
        wrg[k] = scale * (1 / rg[k]) / (s_rg + 0.001);
    for (int k = 0; k < 2; k++)
        wb[k] = scale * (1 / bl[k]) / (s_b + 0.001);
    for (int i = 0; i < 32; i++)
    {
        const uint8_t c = prom[i];
        const int r = (int)(wrg[0] * (c & 1) + wrg[1] * ((c >> 1) & 1) + wrg[2] * ((c >> 2) & 1) + 0.5);
        const int g = (int)(wrg[0] * ((c >> 3) & 1) + wrg[1] * ((c >> 4) & 1) + wrg[2] * ((c >> 5) & 1) + 0.5);
        const int b = (int)(wb[0] * ((c >> 6) & 1) + wb[1] * ((c >> 7) & 1) + 0.5);
        pal[i] = gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }
}

static int py_init(uint8_t *const *regions)
{
    memset(&tp, 0, sizeof(tp));
    tp.pooyan = 1;
    tp.cycles_per_frame = 50688; /* 3.072 MHz at 60.6 Hz */
    tp.rom = regions[0];
    tp.tiles4 = (uint32_t *)gal_alloc(256 * 8 * 4);
    tp.sprites4 = (uint32_t *)gal_alloc(64 * 32 * 4);
    tp.tile_colors = (uint16_t *)gal_alloc(16 * 16 * 2);
    tp.spr_colors = (uint16_t *)gal_alloc(16 * 16 * 2);
    if (!tp.tiles4 || !tp.sprites4 || !tp.tile_colors || !tp.spr_colors)
        return 0;
    gal_conv_4bpp(regions[2], &py_chr_layout, 0, 256, GAL_ROT90, tp.tiles4);
    gal_conv_4bpp(regions[3], &py_spr_layout, 0, 64, GAL_ROT90, tp.sprites4);
    uint16_t pal[32];
    py_palette(regions[4], pal);
    for (int i = 0; i < 256; i++)
    {
        tp.tile_colors[i] = pal[(regions[6][i] & 0x0f) | 0x10];
        tp.spr_colors[i] = pal[regions[5][i] & 0x0f];
        if (!(regions[5][i] & 0x0f))
            tp.spr_mask[i >> 4] |= (uint16_t)(1u << (i & 15));
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x80; p++)
        cpu->rd_page[p] = tp.rom + 256 * p;
    for (int p = 0; p < 0x10; p++) /* 8000-8fff */
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = gal.ram + 256 * p;
    static const uint8_t m[4] = {0x00, 0x01, 0x02, 0x03};
    for (int k = 0; k < 4; k++) /* 9000 and 9400, mirrored over 9b00 */
    {
        cpu->rd_page[0x90 + m[k]] = cpu->wr_page[0x90 + m[k]] = gal.ram + RAM_SPR0;
        cpu->rd_page[0x98 + m[k]] = cpu->wr_page[0x98 + m[k]] = gal.ram + RAM_SPR0;
        cpu->rd_page[0x94 + m[k]] = cpu->wr_page[0x94 + m[k]] = gal.ram + RAM_SPR1;
        cpu->rd_page[0x9c + m[k]] = cpu->wr_page[0x9c + m[k]] = gal.ram + RAM_SPR1;
    }
    cpu->read = py_read;
    cpu->write = py_write;
    konami_snd_init(regions[1], 0x2000, RAM_SOUND);
    return 1;
}

static int tp_init(uint8_t *const *regions)
{
    memset(&tp, 0, sizeof(tp));
    tp.cycles_per_frame = CYCLES_PER_FRAME;
    tp.rom = regions[0];
    tp.tiles = (uint16_t *)gal_alloc(512 * 8 * 2);
    tp.sprites = (uint32_t *)gal_alloc(256 * 16 * 4);
    tp.tile_colors = (uint16_t *)gal_alloc(32 * 4 * 2);
    tp.spr_colors = (uint16_t *)gal_alloc(64 * 4 * 2);
    if (!tp.tiles || !tp.sprites || !tp.tile_colors || !tp.spr_colors)
        return 0;
    gal_conv_tiles8(regions[2], &chr_layout, 512, GAL_ROT90, tp.tiles);
    gal_conv_sprites16(regions[3], &spr_layout, 256, GAL_ROT90, tp.sprites);

    /* MAME's timeplt palette: 32 colours of 5+5+5 bits from two PROMs;
     * sprites through one lookup PROM, characters through the other into
     * the second 16 */
    uint16_t pal[32];
    for (int i = 0; i < 32; i++)
        pal[i] = tp_rgb(regions[4], i);
    for (int i = 0; i < 64 * 4; i++)
        tp.spr_colors[i] = pal[regions[5][i] & 0x0f];
    for (int i = 0; i < 32 * 4; i++)
        tp.tile_colors[i] = pal[(regions[6][i] & 0x0f) + 0x10];

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x60; p++)
        cpu->rd_page[p] = tp.rom + 256 * p;
    for (int p = 0; p < 0x10; p++) /* a000-afff */
        cpu->rd_page[0xa0 + p] = cpu->wr_page[0xa0 + p] = gal.ram + 256 * p;
    for (int m = 0; m < 0x0c; m += 0x08) /* b000 and b400, mirrored at b800 and bc00 */
    {
        cpu->rd_page[0xb0 + m] = cpu->wr_page[0xb0 + m] = gal.ram + RAM_SPR0;
        cpu->rd_page[0xb4 + m] = cpu->wr_page[0xb4 + m] = gal.ram + RAM_SPR1;
    }
    cpu->read = tp_read;
    cpu->write = tp_write;
    konami_snd_init(regions[1], 0x1000, RAM_SOUND);
    return 1;
}

/* --- Roc'n Rope ------------------------------------------------------------------- */

#define RR_CYCLES_PER_FRAME 25344 /* 1.536 MHz at 60.6 Hz */
#define RR_TILE_BYTES 32          /* 8x8, four bits */
#define RR_SPR_BYTES 128          /* 16x16 */

/* Galagino's RAM buffer: the RAM at 4000-5fff, then the sound CPU's */
#define RR_RAM_SPR1 0x0000  /* 4000-402f */
#define RR_RAM_SPR0 0x0400  /* 4400-442f */
#define RR_RAM_COLOR 0x0800 /* 4800-4bff */
#define RR_RAM_VIDEO 0x0c00 /* 4c00-4fff */
#define RR_RAM_SOUND 0x2000 /* the sound CPU's 3000-33ff */

static const gal_layout_t rr_chr_layout = {
    8, 8, 4, {0x2000 * 8 + 4, 0x2000 * 8, 4, 0}, {0, 1, 2, 3, 64, 65, 66, 67}, {0, 8, 16, 24, 32, 40, 48, 56}, 16 * 8};
static const gal_layout_t rr_spr_layout = {
    16, 16, 4, {0x4000 * 8 + 4, 0x4000 * 8, 4, 0},
    {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195},
    {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312},
    64 * 8};

/* the tables the flash cache keeps: tiles, sprites */
static void rr_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    const int eb = i ? RR_SPR_BYTES : RR_TILE_BYTES;
    gal_conv_4bpp(src, i ? &rr_spr_layout : &rr_chr_layout, ofs / eb, len / eb, GAL_ROT270, (uint32_t *)out);
}

/* KONAMI-1: an opcode is stored XORed by a mask chosen by address bits 1 and 3 */
static uint8_t rr_read_opcode(m6809_state *s, uint16_t addr)
{
    static const uint8_t mask[4] = {0x22, 0x82, 0x28, 0x88};
    const uint8_t v = s->rd_page[addr >> 8] ? s->rd_page[addr >> 8][addr & 0xff] : s->read(s, addr);
    return addr >= 0x6000 ? v ^ mask[((addr >> 1) & 1) | ((addr >> 2) & 2)] : v;
}

static uint8_t rr_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    switch (addr)
    {
    case 0x3000:
        return 0x5b; /* DSW2, MAME's defaults: 3 lives, upright, difficulty 5 of 16, demo sounds */
    case 0x3080: /* IN0, active low */
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        return v;
    case 0x3081: /* IN1 */
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
        if (keys & GAL_BTN_EXTRA)
            v &= ~0x20;
        return v;
    case 0x3100:
        return 0x96; /* DSW3, MAME's defaults: bonus at 20000 and every 60000 */
    default: /* 3082 the second player, 3083 DSW1 (1C1C) */
        if (addr >= 0xfff2 && addr <= 0xfffd)
            return tp.vectors[addr - 0xfff2];
        return addr >= 0x6000 ? tp.rom[addr - 0x6000] : 0xff;
    }
}

static void rr_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    if ((addr & 0xfff8) == 0x8080)
    {
        /* the latch: bit addr & 7 */
        const int bit = value & 1;
        switch (addr & 7)
        {
        case 1:
            konami_snd_trigger(bit);
            break;
        case 7:
            tp.nmi_enable = (uint8_t)bit;
            if (!bit)
                s->irq_pending = 0;
            break;
        default:
            break; /* 0 the flip, 2 the mute, 3 and 4 the coin counters */
        }
    }
    else if (addr == 0x8100)
        konami_snd_command(value);
    else if (addr >= 0x8182 && addr <= 0x818d)
        tp.vectors[addr - 0x8182] = value;
    /* 8000: the watchdog */
}

static int rr_init(uint8_t *const *regions)
{
    memset(&tp, 0, sizeof(tp));
    tp.rocnrope = 1;
    tp.cycles_per_frame = RR_CYCLES_PER_FRAME;
    /* the programs to SRAM */
    tp.rom = (uint8_t *)gal_fast_alloc(0xa000);
    uint8_t *snd = (uint8_t *)gal_fast_alloc(0x2000);
    tp.m6809 = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    tp.vectors = (uint8_t *)gal_fast_alloc(16);
    tp.tile_colors = (uint16_t *)gal_fast_alloc(16 * 16 * 2);
    tp.spr_colors = (uint16_t *)gal_fast_alloc(16 * 16 * 2);
    if (!tp.rom || !snd || !tp.m6809 || !tp.vectors || !tp.tile_colors || !tp.spr_colors)
        return 0;
    memcpy(tp.rom, regions[0], 0xa000);
    memcpy(snd, regions[1], 0x2000);
    /* MAME's init_rocnrope: one opcode fixed */
    tp.rom[0x703d - 0x6000] = 0x98 ^ 0x22;

    if (regions[GAL_MAX_REGIONS])
    {
        tp.rr_tiles = (const uint32_t *)regions[GAL_MAX_REGIONS];
        tp.rr_sprites = (const uint32_t *)regions[GAL_MAX_REGIONS + 1];
    }
    else
    {
        uint32_t *tiles = (uint32_t *)gal_alloc(512 * RR_TILE_BYTES);
        uint32_t *sprites = (uint32_t *)gal_alloc(256 * RR_SPR_BYTES);
        if (!tiles || !sprites)
            return 0;
        gal_conv_4bpp(regions[2], &rr_chr_layout, 0, 512, GAL_ROT270, tiles);
        gal_conv_4bpp(regions[3], &rr_spr_layout, 0, 256, GAL_ROT270, sprites);
        tp.rr_tiles = tiles;
        tp.rr_sprites = sprites;
    }

    /* MAME's palette: Pooyan's; sprites through the first lookup PROM,
     * characters through the second, both into the 16 colours of the first
     * half */
    uint16_t pal[32];
    py_palette(regions[4], pal);
    for (int i = 0; i < 256; i++)
    {
        tp.spr_colors[i] = pal[regions[5][i] & 0x0f];
        tp.tile_colors[i] = pal[regions[6][i] & 0x0f];
        if (!(regions[5][i] & 0x0f))
            tp.spr_mask[i >> 4] |= (uint16_t)(1u << (i & 15));
    }

    m6809_state *c = tp.m6809;
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 0x30; p++) /* nothing at 0000-2fff; the inputs at 3000-31ff */
    {
        c->rd_page[p] = gal_zeros;
        c->wr_page[p] = gal_ignored;
    }
    for (int p = 0; p < 0x20; p++) /* 4000-5fff */
        c->rd_page[0x40 + p] = c->wr_page[0x40 + p] = gal.ram + 256 * p;
    for (int p = 0; p < 0x9f; p++) /* the ROM, written to at 8000-81ff (I/O); fff2-fffd the vectors */
        c->rd_page[0x60 + p] = tp.rom + 256 * p;
    c->read = rr_read;
    c->write = rr_write;
    c->read_opcode = rr_read_opcode;
    konami_snd_init(snd, 0x2000, RR_RAM_SOUND);
    return 1;
}

static void tp_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    tp.nmi_enable = 0;
    tp.video_enable = tp.pooyan || tp.rocnrope; /* only Time Pilot has a video enable */
    if (tp.rocnrope)
    {
        memset(tp.vectors, 0, 16);
        m6809_reset(tp.m6809);
        tp.m6809->icount = 0;
    }
    else
    {
        z80_init(&gal.cpu[0]);
        z80_reset(&gal.cpu[0]);
    }
    konami_snd_reset();
}

static void tp_run_slice(int q)
{
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        tp.run_base = step * tp.cycles_per_frame / STEPS;
        tp.run_len = (step + 1) * tp.cycles_per_frame / STEPS - tp.run_base;
        if (tp.rocnrope)
            m6809_run(tp.m6809, tp.run_len);
        else
            z80_run(&gal.cpu[0], tp.run_len);
        konami_snd_run((step + 1) * KONAMI_SND_CYCLES_PER_FRAME / STEPS - step * KONAMI_SND_CYCLES_PER_FRAME / STEPS);
    }
    if (q == 3 && tp.nmi_enable)
    {
        if (tp.rocnrope)
            m6809_irq(tp.m6809);
        else
            z80_set_nmi_line(&gal.cpu[0], 1);
    }
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (239 - y,
 * x + 16), so an s x s element at (x, y) has its upright corner at
 * (240 - s - y, x + 16); its flips in x and y are flips in y and x upright */
#define UP_X(y, s) (240 - (s) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

/* Roc'n Rope, MAME ROT270: the raster's pixel (x, y) is the upright pixel
 * (y - 16, 271 - x); its flips in x and y are flips in y and x upright */
#define RR_UP_X(y) ((y) - 16)
#define RR_UP_Y(x, s) (272 - (s) - (x))

static void rr_prepare_frame(void)
{
    gal.active_sprites = 0;
    const uint8_t *s0 = gal.ram + RR_RAM_SPR0, *s1 = gal.ram + RR_RAM_SPR1;
    /* MAME's rocnrope draw_sprites: the later on top */
    for (int offs = 0x2e; offs >= 0; offs -= 2)
    {
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        o->code = s0[offs + 1];
        o->color = s1[offs] & 0x0f;
        o->flags = (uint8_t)(((~s1[offs] & 0x80) ? 1 : 0) | ((s1[offs] & 0x40) ? 2 : 0));
        o->x = (int16_t)RR_UP_X(s1[offs + 1]);
        o->y = (int16_t)RR_UP_Y(240 - s0[offs], 16);
    }
}

static void rr_render_band(const gal_band_t *b)
{
    const uint8_t *vram = gal.ram + RR_RAM_VIDEO, *cram = gal.ram + RR_RAM_COLOR;
    int ry0 = b->x0 + 16, ry1 = b->x0 + b->w - 1 + 16;
    int rx0 = 271 - (b->y0 + b->h - 1), rx1 = 271 - b->y0;
    if (rx0 < 0)
        rx0 = 0;
    if (rx1 > 255)
        rx1 = 255;
    if (rx0 > rx1)
    {
        gal_fill_band(b, 0);
        return;
    }
    if (ry0 < 16 || ry1 > 239 || b->y0 < VIEW_TOP || b->y0 + b->h > VIEW_TOP + 256)
        gal_fill_band(b, 0);
    for (int row = ry0 >> 3; row <= ry1 >> 3; row++)
        for (int col = rx0 >> 3; col <= rx1 >> 3; col++)
        {
            const int i = row * 32 + col;
            const uint8_t attr = cram[i];
            const int code = vram[i] + 2 * (attr & 0x80);
            gal_draw_4bpp(b, RR_UP_X(8 * row), RR_UP_Y(8 * col, 8), 8, tp.rr_tiles + 8 * code, (attr & 0x20) != 0,
                          (attr & 0x40) != 0, tp.tile_colors + 16 * (attr & 0x0f), 1);
        }
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        gal_draw_4bpp_mask(b, o->x, o->y, 16, tp.rr_sprites + 32 * o->code, o->flags & 1, (o->flags & 2) != 0,
                           tp.spr_colors + 16 * o->color, tp.spr_mask[o->color]);
    }
}

static void tp_prepare_frame(void)
{
    gal.active_sprites = 0;
    const uint8_t *s0 = gal.ram + RAM_SPR0, *s1 = gal.ram + RAM_SPR1;
    if (tp.pooyan)
    {
        /* MAME's pooyan draw_sprites: the later on top */
        for (int offs = 0x10; offs < 0x40; offs += 2)
        {
            gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
            const int x = s0[offs], y = 240 - s1[offs + 1];
            o->code = s0[offs + 1] & 0x3f;
            o->color = s1[offs] & 0x0f;
            o->flags = (uint8_t)(((s1[offs] & 0x80) ? 1 : 0) | ((~s1[offs] & 0x40) ? 2 : 0));
            o->x = (int16_t)UP_X(y, 16);
            o->y = (int16_t)UP_Y(x);
        }
        return;
    }
    for (int offs = 0x3e; offs >= 0x10; offs -= 2)
    {
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        const int x = s0[offs], y = 241 - s1[offs + 1];
        o->code = s0[offs + 1];
        o->color = s1[offs] & 0x3f;
        /* flags: 1 flip x, 2 flip y (upright) */
        o->flags = (uint8_t)(((s1[offs] & 0x80) ? 1 : 0) | ((~s1[offs] & 0x40) ? 2 : 0));
        o->x = (int16_t)UP_X(y, 16);
        o->y = (int16_t)UP_Y(x);
    }
}

/* the tiles of one priority (attribute bit 4) */
static void draw_tiles(const gal_band_t *b, int category)
{
    const uint8_t *vram = gal.ram + RAM_VIDEO, *cram = gal.ram + RAM_COLOR;
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
            /* raster flips (bit 6 x, bit 7 y) are upright flips in y and x */
            const int fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
            if (tp.pooyan)
            {
                gal_draw_4bpp(b, UP_X(8 * row, 8), UP_Y(8 * col), 8, tp.tiles4 + 8 * vram[i], fx, fy,
                              tp.tile_colors + 16 * (attr & 0x0f), 1);
                continue;
            }
            if (((attr >> 4) & 1) != category)
                continue;
            const int code = vram[i] + 8 * (attr & 0x20);
            const uint16_t *rows = tp.tiles + 8 * code;
            const uint16_t *colors = tp.tile_colors + 4 * (attr & 0x1f);
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

static void tp_render_band(const gal_band_t *b)
{
    gal_fill_band(b, 0);
    if (!tp.video_enable)
        return;
    draw_tiles(b, 0);
    if (tp.pooyan)
    {
        for (int s = 0; s < gal.active_sprites; s++)
        {
            const gal_sprite_t *o = &gal.sprite[s];
            gal_draw_4bpp_mask(b, o->x, o->y, 16, tp.sprites4 + 32 * o->code, o->flags & 1, (o->flags & 2) != 0,
                               tp.spr_colors + 16 * o->color, tp.spr_mask[o->color]);
        }
        return;
    }
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        gal_draw_sprite_fx(b, o->x, o->y, tp.sprites + 16 * o->code, o->flags & 1, o->flags & 2,
                           tp.spr_colors + 4 * o->color, 1);
    }
    draw_tiles(b, 1);
}

static void tp_sound(int16_t *out, int n)
{
    konami_snd_render(out, n);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t tp_hiscore[] = {
    {0xab08, 0x28, 0x00, 0xf1},
    {0xa98b, 0x03, 0x00, 0x01},
};

static const gal_hiscore_region_t py_hiscore[] = {
    {0x89c0, 0x1e, 0x00, 0x00},
    {0x8a00, 0x1e, 0x00, 0x01},
    {0x8e00, 0x1e, 0x00, 0x00},
    {0x88a8, 3, 0x00, 0x01}, /* the top score */
};

static const gal_hiscore_region_t rr_hiscore[] = {
    {0x5160, 0x40, 0x01, 0x00},
    {0x50a6, 3, 0x01, 0x00}, /* the top score */
};

static uint8_t rr_hs_read(uint16_t addr)
{
    return gal.ram[(addr - 0x4000) & 0x1fff];
}

static void rr_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[(addr - 0x4000) & 0x1fff] = value;
}

const gal_game_ops_t gal_rocnrope_ops = {
    .init = rr_init,
    .reset = tp_reset,
    .run_slice = tp_run_slice,
    .prepare_frame = rr_prepare_frame,
    .render_band = rr_render_band,
    .sound = tp_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .extra_button = 1,
    .large = 1,
    .nderived = 2,
    .derived_src = {2, 3},
    .derived_size = {512 * RR_TILE_BYTES, 256 * RR_SPR_BYTES},
    .derive = rr_derive,
    .hiscore = rr_hiscore,
    .nhiscore = sizeof(rr_hiscore) / sizeof(rr_hiscore[0]),
    .hs_read = rr_hs_read,
    .hs_write = rr_hs_write,
};

const gal_game_ops_t gal_pooyan_ops = {
    .init = py_init,
    .reset = tp_reset,
    .run_slice = tp_run_slice,
    .prepare_frame = tp_prepare_frame,
    .render_band = tp_render_band,
    .sound = tp_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .hiscore = py_hiscore,
    .nhiscore = sizeof(py_hiscore) / sizeof(py_hiscore[0]),
};

const gal_game_ops_t gal_timeplt_ops = {
    .init = tp_init,
    .reset = tp_reset,
    .run_slice = tp_run_slice,
    .prepare_frame = tp_prepare_frame,
    .render_band = tp_render_band,
    .sound = tp_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .hiscore = tp_hiscore,
    .nhiscore = sizeof(tp_hiscore) / sizeof(tp_hiscore[0]),
};
