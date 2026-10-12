/*
 * Circus Charlie (Konami, 1984), after MAME's konami/circusc.cpp and
 * circusc_v.cpp.
 *
 * A KONAMI-1 (a 6809 that decrypts its opcodes) at 1.54 MHz, its IRQ at the
 * vertical blank while enabled; a Z80 at 3.58 MHz for the sound, with two
 * SN76496 and an 8-bit DAC (the RC filters it switches are left out).
 * 256x224 shown (MAME ROT90: the upright picture is 224x256, drawn at y
 * 16..271). One layer of 8x8 tiles of four bits per pixel, its columns 10..31
 * scrolled vertically, the tiles with attribute bit 4 clear drawn in front of
 * the 64 sprites of 16x16. Large: the tiles and sprites are read from PSRAM
 * or the flash cache.
 */
#include "gal_internal.h"

#include "m6809.h"

#define CYCLES_PER_FRAME 25344     /* 1.536 MHz at 60.6 Hz */
#define SND_CYCLES_PER_FRAME 59068 /* 3.58 MHz */
#define SN_CLOCK 1789772
#define STEPS 16
#define VIEW_TOP 16
#define TILE_BYTES 32 /* 8x8, four bits */
#define SPR_BYTES 128 /* 16x16 */
#define NSPRITES 384
#define DAC_SLOTS 256 /* per quarter frame */

/* Galagino's RAM buffer: the main CPU's 2000-3fff, then the sound CPU's RAM */
#define RAM_COLOR 0x1000 /* 3000-33ff */
#define RAM_VIDEO 0x1400 /* 3400-37ff */
#define RAM_SPR2 0x1800  /* 3800-38ff */
#define RAM_SPR 0x1900   /* 3900-39ff */
#define RAM_SOUND 0x2000 /* the sound CPU's 4000-43ff */

static struct
{
    m6809_state *cpu;
    uint8_t *rom, *rom_sound;
    const uint32_t *tiles, *sprites; /* [512][8], [384][32] */
    uint16_t *tile_colors, *spr_colors; /* [16][16] each */
    uint16_t spr_mask[16];
    uint8_t irq_enable, spritebank, scroll;
    uint8_t latch, sn_latch;
    gal_sn_t *sn;
    /* the DAC: the value written in each slot of the quarter frame */
    uint16_t *dac_slot;
    uint8_t dac;
    int32_t run_start, quarter_pos;
    uint64_t snd_cycles;
} cc;

static const gal_layout_t chr_layout = {
    8, 8, 4, {0, 1, 2, 3}, {0, 4, 8, 12, 16, 20, 24, 28}, {0, 32, 64, 96, 128, 160, 192, 224}, 32 * 8};
static const gal_layout_t spr_layout = {
    16, 16, 4, {0, 1, 2, 3},
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60},
    {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960},
    128 * 8};

/* the tables the flash cache keeps: tiles, sprites */
static void cc_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    const int eb = i ? SPR_BYTES : TILE_BYTES;
    gal_conv_4bpp(src, i ? &spr_layout : &chr_layout, ofs / eb, len / eb, GAL_ROT90, (uint32_t *)out);
}

/* --- Main CPU --------------------------------------------------------------------- */

/* KONAMI-1: an opcode is stored XORed by a mask chosen by address bits 1 and 3 */
static uint8_t cc_read_opcode(m6809_state *s, uint16_t addr)
{
    static const uint8_t mask[4] = {0x22, 0x82, 0x28, 0x88};
    const uint8_t v = s->rd_page[addr >> 8] ? s->rd_page[addr >> 8][addr & 0xff] : s->read(s, addr);
    return addr >= 0x6000 ? v ^ mask[((addr >> 1) & 1) | ((addr >> 2) & 2)] : v;
}

static uint8_t cc_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    switch (addr & 0xfc00)
    {
    case 0x1000:
        switch (addr & 3)
        {
        case 0: /* IN0, active low */
            if (keys & GAL_BTN_COIN)
                v &= ~0x01;
            if (keys & GAL_BTN_START)
                v &= ~0x08;
            if (keys & GAL_BTN_START2)
                v &= ~0x10;
            return v;
        case 1: /* IN1: left and right, jump */
            if (keys & GAL_BTN_LEFT)
                v &= ~0x01;
            if (keys & GAL_BTN_RIGHT)
                v &= ~0x02;
            if (keys & GAL_BTN_FIRE)
                v &= ~0x10;
            return v;
        default: /* the second player, nothing */
            return 0xff;
        }
    case 0x1400:
        return 0xff; /* DSW1: 1C1C */
    case 0x1800:
        return 0x4b; /* DSW2, MAME's defaults: 3 lives, upright, bonus at 20000/70000, normal, demo sounds */
    default:
        return 0;
    }
}

static void cc_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    switch (addr & 0xfc00)
    {
    case 0x0000: /* the latch: bit addr & 7 */
        switch (addr & 7)
        {
        case 1:
            cc.irq_enable = value & 1;
            if (!cc.irq_enable)
                s->irq_pending = 0;
            break;
        case 5:
            cc.spritebank = value & 1;
            break;
        default:
            break; /* 0 the flip, 2 the mute, 3 and 4 the coin counters */
        }
        break;
    case 0x0800:
        cc.latch = value;
        break;
    case 0x0c00:
        z80_hold_irq(&gal.cpu[0], 0xff);
        break;
    case 0x1c00:
        cc.scroll = value;
        break;
    default:
        break; /* 0400 the watchdog */
    }
}

/* --- Sound CPU -------------------------------------------------------------------- */

static uint8_t cc_snd_read(z80_t *cpu, uint16_t addr)
{
    switch (addr & 0xe000)
    {
    case 0x6000:
        return cc.latch;
    case 0x8000:
    {
        /* a counter of the CPU clock / 512 on D1-D4 */
        const uint64_t now = cc.snd_cycles + (uint64_t)(cc.run_start - cpu->icount);
        return (uint8_t)((now >> 9) & 0x1e);
    }
    default:
        return 0xff;
    }
}

static void cc_snd_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xe000) != 0xa000)
        return;
    switch (addr & 7)
    {
    case 0:
        cc.sn_latch = value;
        break;
    case 1:
        gal_sn_write(&cc.sn[0], cc.sn_latch);
        break;
    case 2:
        gal_sn_write(&cc.sn[1], cc.sn_latch);
        break;
    case 3:
    {
        /* the slot of the quarter frame this write falls in */
        const int32_t pos = cc.quarter_pos + (cc.run_start - cpu->icount);
        int slot = (int)((int64_t)pos * DAC_SLOTS / (SND_CYCLES_PER_FRAME / 4));
        if (slot >= DAC_SLOTS)
            slot = DAC_SLOTS - 1;
        cc.dac_slot[slot] = value;
        break;
    }
    default:
        break; /* 4: the filters */
    }
}

/* --- Set-up ----------------------------------------------------------------------- */

/* MAME's circusc palette: resistors of 1K, 470 and 220 ohm, no pull-down */
static void cc_palette(const uint8_t *prom, uint16_t *pal)
{
    const double rg[3] = {1000, 470, 220}, bl[2] = {470, 220};
    double s_rg = 0, s_b = 0;
    for (int k = 0; k < 3; k++)
        s_rg += 1 / rg[k];
    for (int k = 0; k < 2; k++)
        s_b += 1 / bl[k];
    for (int i = 0; i < 32; i++)
    {
        const uint8_t c = prom[i];
        double r = 0, g = 0, b = 0;
        for (int k = 0; k < 3; k++)
        {
            r += ((c >> k) & 1) / rg[k];
            g += ((c >> (3 + k)) & 1) / rg[k];
        }
        for (int k = 0; k < 2; k++)
            b += ((c >> (6 + k)) & 1) / bl[k];
        const int ri = (int)(255 * r / s_rg + 0.5), gi = (int)(255 * g / s_rg + 0.5), bi = (int)(255 * b / s_b + 0.5);
        pal[i] = gal_color((uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3)));
    }
}

static int cc_init(uint8_t *const *regions)
{
    memset(&cc, 0, sizeof(cc));
    /* the programs to SRAM */
    cc.rom = (uint8_t *)gal_fast_alloc(0xa000);
    cc.rom_sound = (uint8_t *)gal_fast_alloc(0x4000);
    cc.cpu = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    cc.tile_colors = (uint16_t *)gal_fast_alloc(256 * 2);
    cc.spr_colors = (uint16_t *)gal_fast_alloc(256 * 2);
    cc.sn = (gal_sn_t *)gal_fast_alloc(2 * sizeof(gal_sn_t));
    cc.dac_slot = (uint16_t *)gal_fast_alloc(DAC_SLOTS * 2);
    if (!cc.rom || !cc.rom_sound || !cc.cpu || !cc.tile_colors || !cc.spr_colors || !cc.sn || !cc.dac_slot)
        return 0;
    memcpy(cc.rom, regions[0], 0xa000);
    memcpy(cc.rom_sound, regions[1], 0x4000);

    if (regions[GAL_MAX_REGIONS])
    {
        cc.tiles = (const uint32_t *)regions[GAL_MAX_REGIONS];
        cc.sprites = (const uint32_t *)regions[GAL_MAX_REGIONS + 1];
    }
    else
    {
        uint32_t *tiles = (uint32_t *)gal_alloc(512 * TILE_BYTES);
        uint32_t *sprites = (uint32_t *)gal_alloc(NSPRITES * SPR_BYTES);
        if (!tiles || !sprites)
            return 0;
        gal_conv_4bpp(regions[2], &chr_layout, 0, 512, GAL_ROT90, tiles);
        gal_conv_4bpp(regions[3], &spr_layout, 0, NSPRITES, GAL_ROT90, sprites);
        cc.tiles = tiles;
        cc.sprites = sprites;
    }

    /* characters through the first lookup PROM into the second 16 colours,
     * sprites through the second into the first 16; a sprite is transparent
     * where its lookup gives colour 0 */
    uint16_t pal[32];
    cc_palette(regions[4], pal);
    for (int i = 0; i < 256; i++)
    {
        cc.tile_colors[i] = pal[(regions[5][i] & 0x0f) | 0x10];
        cc.spr_colors[i] = pal[regions[6][i] & 0x0f];
        if (!(regions[6][i] & 0x0f))
            cc.spr_mask[i >> 4] |= (uint16_t)(1u << (i & 15));
    }

    m6809_state *c = cc.cpu;
    memset(c, 0, sizeof(*c));
    for (int p = 0x20; p < 0x40; p++) /* 0000-1fff: I/O; 2000-3fff: RAM */
        c->rd_page[p] = c->wr_page[p] = gal.ram + 256 * (p - 0x20);
    for (int p = 0x40; p < 0x60; p++)
    {
        c->rd_page[p] = gal_zeros;
        c->wr_page[p] = gal_ignored;
    }
    for (int p = 0x60; p < 0x100; p++)
    {
        c->rd_page[p] = cc.rom + 256 * (p - 0x60);
        c->wr_page[p] = gal_ignored;
    }
    c->read = cc_read;
    c->write = cc_write;
    c->read_opcode = cc_read_opcode;

    z80_t *z = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        z->rd_page[p] = cc.rom_sound + 256 * p;
    for (int p = 0x40; p < 0x60; p++) /* 4000-43ff, mirrored to 5fff */
        z->rd_page[p] = z->wr_page[p] = gal.ram + RAM_SOUND + 256 * (p & 3);
    z->read = cc_snd_read;
    z->write = cc_snd_write;
    return 1;
}

static void cc_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    cc.irq_enable = cc.spritebank = cc.scroll = cc.latch = cc.sn_latch = 0;
    cc.dac = 0x80;
    for (int i = 0; i < DAC_SLOTS; i++)
        cc.dac_slot[i] = 0xffff;
    cc.snd_cycles = 0;
    m6809_reset(cc.cpu);
    cc.cpu->icount = 0;
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    gal_sn_init(&cc.sn[0], 0, SN_CLOCK);
    gal_sn_init(&cc.sn[1], 0, SN_CLOCK);
}

static void cc_run_slice(int q)
{
    cc.quarter_pos = 0;
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        m6809_run(cc.cpu, (step + 1) * CYCLES_PER_FRAME / STEPS - step * CYCLES_PER_FRAME / STEPS);
        z80_t *z = &gal.cpu[0];
        const int len = (step + 1) * SND_CYCLES_PER_FRAME / STEPS - step * SND_CYCLES_PER_FRAME / STEPS;
        cc.run_start = z->icount + len;
        z80_run(z, len);
        const int32_t ran = cc.run_start - z->icount;
        cc.snd_cycles += (uint32_t)ran;
        cc.quarter_pos += ran;
    }
    if (q == 3 && cc.irq_enable)
        m6809_irq(cc.cpu);
}

static void cc_sound(int16_t *out, int n)
{
    gal_sn_render(cc.sn, 2, 192, out, n);
    /* the DAC, a quarter of a chip's full scale in MAME's mix */
    for (int i = 0; i < n; i++)
    {
        const uint16_t w = cc.dac_slot[i * DAC_SLOTS / n];
        if (w != 0xffff)
            cc.dac = (uint8_t)w;
        const int32_t v = out[i] + ((int)cc.dac - 0x80) * 48;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    for (int i = 0; i < DAC_SLOTS; i++)
        cc.dac_slot[i] = 0xffff;
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (239 - y,
 * x + 16), so an s x s element at (x, y) has its upright corner at
 * (240 - s - y, x + 16); its flips in x and y are flips in y and x upright */
#define UP_X(y, s) (240 - (s) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void cc_prepare_frame(void)
{
    gal.active_sprites = 0;
    const uint8_t *sr = gal.ram + (cc.spritebank ? RAM_SPR : RAM_SPR2);
    /* MAME's draw_sprites: the later on top */
    for (int offs = 0; offs < 0x100; offs += 4)
    {
        gal_sprite_t *o = &gal.sprite[gal.active_sprites];
        const int code = (sr[offs] + 8 * (sr[offs + 1] & 0x20)) % NSPRITES;
        /* code, 9 bits, in code and the top bit of flags */
        o->code = (uint8_t)code;
        o->color = sr[offs + 1] & 0x0f;
        o->flags = (uint8_t)(((sr[offs + 1] & 0x80) ? 1 : 0) | ((sr[offs + 1] & 0x40) ? 2 : 0) | ((code >> 8) << 7));
        o->x = (int16_t)UP_X(sr[offs + 3], 16);
        o->y = (int16_t)UP_Y(sr[offs + 2]);
        gal.active_sprites++;
    }
}

/* the tiles of one priority (attribute bit 4), columns 10..31 scrolled */
static void draw_tiles(const gal_band_t *b, int category)
{
    const uint8_t *vram = gal.ram + RAM_VIDEO, *cram = gal.ram + RAM_COLOR;
    int rx0 = b->y0 - VIEW_TOP, rx1 = b->y0 + b->h - 1 - VIEW_TOP;
    if (rx0 < 0)
        rx0 = 0;
    if (rx1 > 255)
        rx1 = 255;
    for (int col = rx0 >> 3; col <= rx1 >> 3; col++)
    {
        const int scroll = col >= 10 ? cc.scroll : 0;
        for (int row = 0; row < 32; row++)
        {
            /* the tile's raster y, 0..255 after the scroll */
            const int y = (8 * row - scroll) & 0xff;
            const int ux = UP_X(y, 8);
            if (ux + 8 <= b->x0 || ux >= b->x0 + b->w)
                continue;
            const int i = row * 32 + col;
            const uint8_t attr = cram[i];
            if (((attr >> 4) & 1) != category)
                continue;
            const int code = vram[i] + ((attr & 0x20) << 3);
            const uint32_t *rows = cc.tiles + 8 * code;
            const uint16_t *colors = cc.tile_colors + 16 * (attr & 0x0f);
            const int fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
            gal_draw_4bpp(b, ux, UP_Y(8 * col), 8, rows, fx, fy, colors, 1);
        }
    }
}

static void cc_render_band(const gal_band_t *b)
{
    gal_fill_band(b, 0);
    draw_tiles(b, 1);
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const int code = o->code | ((o->flags >> 7) << 8);
        gal_draw_4bpp_mask(b, o->x, o->y, 16, cc.sprites + 32 * code, o->flags & 1, (o->flags & 2) != 0,
                           cc.spr_colors + 16 * o->color, cc.spr_mask[o->color]);
    }
    draw_tiles(b, 0);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it; the last six are
 * the top score's digits on the screen, which the game draws only at boot. */
static const gal_hiscore_region_t cc_hiscore[] = {
    {0x2160, 0x32, 0x01, 0xfe}, {0x20a6, 0x03, 0x01, 0x30}, {0x35a7, 0x01, 0x00, 0x00},
    {0x35c7, 0x01, 0x03, 0x03}, {0x35e7, 0x01, 0x08, 0x08}, {0x3607, 0x01, 0x09, 0x09},
    {0x3627, 0x01, 0x01, 0x01}, {0x3647, 0x01, 0x10, 0x10},
};

static uint8_t cc_hs_read(uint16_t addr)
{
    return gal.ram[(addr - 0x2000) & 0x1fff];
}

static void cc_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[(addr - 0x2000) & 0x1fff] = value;
}

const gal_game_ops_t gal_circusc_ops = {
    .init = cc_init,
    .reset = cc_reset,
    .run_slice = cc_run_slice,
    .prepare_frame = cc_prepare_frame,
    .render_band = cc_render_band,
    .sound = cc_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .large = 1,
    .nderived = 2,
    .derived_src = {2, 3},
    .derived_size = {512 * TILE_BYTES, NSPRITES * SPR_BYTES},
    .derive = cc_derive,
    .hiscore = cc_hiscore,
    .nhiscore = sizeof(cc_hiscore) / sizeof(cc_hiscore[0]),
    .hs_read = cc_hs_read,
    .hs_write = cc_hs_write,
};
