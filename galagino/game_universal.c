/*
 * Universal's boards: Lady Bug (1981) and Mr. Do! (1982), the games of
 * GalaginoPlus on them, after MAME's universal/ladybug.cpp and mrdo.cpp.
 *
 * One Z80 (4 MHz, Mr. Do! 4.1 MHz), two SN76489 at the same clock, and a
 * 240x192 raster turned by MAME's ROT270 into a 192x240 upright picture,
 * drawn at x 16..207 and y 24..263 of the 224x288 one.
 *
 *   Lady Bug   no frame interrupt: the game polls the vertical blank on IN1.
 *              A coin in the left slot is an NMI. One tile layer with a
 *              scroll per row (not used by the game), sprites of 16x16 and
 *              8x8 in 14 blocks of 16.
 *   Mr. Do!    an IRQ every frame. Two tile layers, the back one scrolled,
 *              tiles opaque by an attribute bit; 64 sprites of 16x16. A PAL
 *              at 9803 answers with the ROM byte at HL (MAME's workaround).
 */
#include "gal_internal.h"

#define LB_CYCLES_PER_FRAME 66539 /* 4 MHz at 60.11 Hz */
#define DO_CYCLES_PER_FRAME 68400 /* 4.1 MHz at 59.94 Hz */

/* the raster's 262 lines from the first visible one: 192 shown, 70 blank */
#define VISIBLE_LINES 192
#define TOTAL_LINES 262

/* the picture's place in the 224x288 upright one */
#define PIC_X0 16
#define PIC_X1 (PIC_X0 + 192)
#define VIEW_TOP 24
#define VIEW_H 240

typedef enum
{
    UB_LADYBUG,
    UB_MRDO,
} ub_var_t;

/* Galagino's RAM buffer */
#define LB_RAM 0x0000    /* 6000-6fff */
#define LB_SPR 0x1000    /* 7000-73ff, written only */
#define LB_BG 0x1400     /* d000-d7ff: codes, then attributes */
#define DO_BG 0x0000     /* 8000-87ff: attributes, then codes */
#define DO_FG 0x0800     /* 8800-8fff */
#define DO_SPR 0x1000    /* 9000-90ff, written only */
#define DO_RAM 0x1100    /* e000-efff */

static struct
{
    ub_var_t var;
    int cycles_per_frame;
    uint8_t *rom;
    uint16_t *tiles[2];   /* [512][8]; Mr. Do!: front, back */
    uint32_t *sprites;    /* [128][16] */
    uint16_t *small;      /* Lady Bug's 8x8 sprites, [512][8] */
    uint16_t *tile_color; /* [colours][4] */
    uint16_t *spr_color;  /* [16][4] */
    uint16_t back;        /* the colour behind everything */
    uint8_t dsw0, dsw1;
    uint8_t scroll_x, scroll_y; /* Mr. Do!'s back layer */
    gal_sn_t sn[2];
    int run_base, run_len;
} ub;


/* --- Inputs -------------------------------------------------------------------- */

/* the joystick, active low: left, down, right, up */
static uint8_t ub_stick(void)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    if (keys & GAL_BTN_LEFT)
        v &= ~0x01;
    if (keys & GAL_BTN_DOWN)
        v &= ~0x02;
    if (keys & GAL_BTN_RIGHT)
        v &= ~0x04;
    if (keys & GAL_BTN_UP)
        v &= ~0x08;
    return v;
}

static uint8_t ub_starts(uint8_t v)
{
    if (gal.buttons & GAL_BTN_START)
        v &= ~0x20;
    if (gal.buttons & GAL_BTN_START2)
        v &= ~0x40;
    return v;
}

static int in_vblank(const z80_t *cpu)
{
    const int c = ub.run_base + (ub.run_len - cpu->icount);
    return c >= ub.cycles_per_frame * VISIBLE_LINES / TOTAL_LINES;
}

/* --- Memory -------------------------------------------------------------------- */

static uint8_t lb_read(z80_t *cpu, uint16_t addr)
{
    switch (addr)
    {
    case 0x9000:
        return ub_starts(ub_stick());
    case 0x9001:
        /* the upright cabinet's controls once more, and the vertical blank on
         * bit 6 (active low) and bit 7 (active high) */
        return (ub_stick() & 0x3f) | (in_vblank(cpu) ? 0x80 : 0x40);
    case 0x9002:
        return ub.dsw0;
    case 0x9003:
        return ub.dsw1;
    case 0xe000:
        return 0xff;
    default:
        return 0;
    }
}

static void lb_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if ((addr & 0xf000) == 0xb000)
        gal_sn_write(&ub.sn[0], value);
    else if ((addr & 0xf000) == 0xc000)
        gal_sn_write(&ub.sn[1], value);
    /* a000-a007: the flip of a cocktail cabinet */
}

static uint8_t do_read(z80_t *cpu, uint16_t addr)
{
    switch (addr)
    {
    case 0x9803:
        return ub.rom[cpu->hl.w & 0x7fff];
    case 0xa000:
        return ub_starts(ub_stick() & ((gal.buttons & GAL_BTN_FIRE) ? ~0x10 : 0xff));
    case 0xa001:
        return (gal.buttons & GAL_BTN_COIN) ? 0xbf : 0xff;
    case 0xa002:
        return ub.dsw0;
    case 0xa003:
        return ub.dsw1;
    default:
        return 0;
    }
}

static void do_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr == 0x9801)
        gal_sn_write(&ub.sn[0], value);
    else if (addr == 0x9802)
        gal_sn_write(&ub.sn[1], value);
    else if (addr >= 0xf800)
        ub.scroll_y = value;
    else if (addr >= 0xf000)
        ub.scroll_x = value;
    /* 9800: the flip of a cocktail cabinet and a priority Mr. Do! leaves */
}

static void map(z80_t *cpu, int page, int pages, uint8_t *mem, int writable)
{
    for (int p = 0; p < pages; p++)
    {
        cpu->rd_page[page + p] = mem + 256 * p;
        cpu->wr_page[page + p] = writable ? mem + 256 * p : NULL;
    }
}

/* --- Colours ------------------------------------------------------------------- */

static uint16_t rgb(int r, int g, int b)
{
    return gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}

static uint8_t rev4(uint8_t v)
{
    return (uint8_t)(((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3));
}

/* MAME's ladybug_palette: 32 colours of two inverted bits per gun (470 and
 * 220 ohm); characters take lookup entry (pen << 3) | colour, sprites a
 * nibble of the lookup PROM, its bits reversed. */
static void lb_palette(const uint8_t *prom, const uint8_t *lookup)
{
    static const uint8_t level[4] = {0, 81, 174, 255};
    uint16_t pal[32];
    for (int i = 0; i < 32; i++)
    {
        const uint8_t c = (uint8_t)~prom[i];
        pal[i] = rgb(level[(c & 1) | ((c >> 4) & 2)], level[((c >> 2) & 1) | ((c >> 5) & 2)],
                     level[((c >> 4) & 1) | ((c >> 6) & 2)]);
    }
    ub.back = pal[0];
    for (int c = 0; c < 8; c++)
        for (int p = 0; p < 4; p++)
            ub.tile_color[4 * c + p] = pal[(p << 3) | c];
    for (int c = 0; c < 16; c++)
        for (int p = 0; p < 4; p++)
        {
            const uint8_t v = lookup[4 * (c & 7) + p];
            ub.spr_color[4 * c + p] = pal[rev4((c < 8) ? (v & 0x0f) : (v >> 4))];
        }
}

/* MAME's mrdo palette_init: 256 colours from two PROMs through diodes and
 * resistors; tiles take them directly, sprites through the lookup PROM. */
static void do_palette(const uint8_t *prom, const uint8_t *lookup)
{
    static const float res[4] = {150, 120, 100, 75};
    const float pull = 220, potadjust = 0.7f;
    float pot[16];
    int weight[16];
    for (int i = 15; i >= 0; i--)
    {
        float par = 0;
        for (int k = 0; k < 4; k++)
            if (i & (1 << k))
                par += 1.0f / res[k];
        pot[i] = par ? pull / (pull + 1 / par) - potadjust : 0;
        weight[i] = (int)(0xff * pot[i] / pot[15]);
        if (weight[i] < 0)
            weight[i] = 0;
    }
    uint16_t pal[256];
    for (int i = 0; i < 256; i++)
    {
        const uint8_t lo = prom[((i >> 3) & 0x1c) + (i & 3) + 0x20], hi = prom[(i & 0x1c) + (i & 3)];
        pal[i] = rgb(weight[(lo & 3) | ((hi & 3) << 2)], weight[((lo >> 2) & 3) | (hi & 0x0c)],
                     weight[((lo >> 4) & 3) | (((hi >> 4) & 3) << 2)]);
    }
    ub.back = pal[0];
    for (int i = 0; i < 256; i++)
        ub.tile_color[i] = pal[i];
    for (int i = 0; i < 64; i++)
    {
        uint8_t e = lookup[i & 0x1f];
        e = (i & 0x20) ? (e >> 4) : (e & 0x0f);
        ub.spr_color[i] = pal[e + ((e & 0x0c) << 3)];
    }
}

/* --- Graphics -------------------------------------------------------------------- */

/* Both games' characters: 512, a bitplane per half of the ROMs. */
static const gal_layout_t ub_chars = {
    8, 8, 2, {0, 512 * 8 * 8}, {7, 6, 5, 4, 3, 2, 1, 0}, {0, 8, 16, 24, 32, 40, 48, 56}, 64};

static const gal_layout_t lb_sprites = {
    16, 16, 2, {1, 0},
    {0, 2, 4, 6, 8, 10, 12, 14, 128, 130, 132, 134, 136, 138, 140, 142},
    {23 * 16, 22 * 16, 21 * 16, 20 * 16, 19 * 16, 18 * 16, 17 * 16, 16 * 16,
     7 * 16, 6 * 16, 5 * 16, 4 * 16, 3 * 16, 2 * 16, 1 * 16, 0 * 16},
    64 * 8};

static const gal_layout_t lb_small = {
    8, 8, 2, {1, 0}, {0, 2, 4, 6, 8, 10, 12, 14}, {7 * 16, 6 * 16, 5 * 16, 4 * 16, 3 * 16, 2 * 16, 1 * 16, 0}, 16 * 8};

static const gal_layout_t do_sprites = {
    16, 16, 2, {4, 0},
    {3, 2, 1, 0, 11, 10, 9, 8, 19, 18, 17, 16, 27, 26, 25, 24},
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480},
    64 * 8};

/* --- Set-up ---------------------------------------------------------------------- */

static int ub_alloc(int layers, int tile_colors, int small)
{
    for (int k = 0; k < layers; k++)
        if (!(ub.tiles[k] = (uint16_t *)gal_alloc(512 * 8 * 2)))
            return 0;
    ub.sprites = (uint32_t *)gal_alloc(128 * 16 * 4);
    ub.small = small ? (uint16_t *)gal_alloc(512 * 8 * 2) : NULL;
    ub.tile_color = (uint16_t *)gal_alloc(tile_colors * 2);
    ub.spr_color = (uint16_t *)gal_alloc(64 * 2);
    return ub.sprites && (ub.small || !small) && ub.tile_color && ub.spr_color;
}

static int lb_init(uint8_t *const *regions)
{
    memset(&ub, 0, sizeof(ub));
    ub.var = UB_LADYBUG;
    ub.cycles_per_frame = LB_CYCLES_PER_FRAME;
    ub.rom = regions[0];
    if (!ub_alloc(1, 32, 1))
        return 0;
    gal_conv_tiles8(regions[1], &ub_chars, 512, GAL_ROT270, ub.tiles[0]);
    gal_conv_sprites16(regions[2], &lb_sprites, 128, GAL_ROT270, ub.sprites);
    gal_conv_tiles8(regions[2], &lb_small, 512, GAL_ROT270, ub.small);
    lb_palette(regions[3], regions[4]);
    ub.dsw0 = 0xdf; /* MAME's defaults: easy, 10 letters, upright, 3 lives */
    ub.dsw1 = 0xff; /* 1C1C */

    z80_t *cpu = &gal.cpu[0];
    map(cpu, 0x00, 0x60, ub.rom, 0);
    map(cpu, 0x60, 0x10, gal.ram + LB_RAM, 1);
    for (int p = 0; p < 4; p++)
    {
        cpu->rd_page[0x70 + p] = gal_zeros;
        cpu->wr_page[0x70 + p] = gal.ram + LB_SPR + 256 * p;
    }
    for (int p = 0x80; p < 0x90; p++)
    {
        cpu->rd_page[p] = gal_zeros;
        cpu->wr_page[p] = gal_ignored;
    }
    map(cpu, 0xd0, 0x08, gal.ram + LB_BG, 1);
    cpu->read = lb_read;
    cpu->write = lb_write;
    return 1;
}

static int do_init(uint8_t *const *regions)
{
    memset(&ub, 0, sizeof(ub));
    ub.var = UB_MRDO;
    ub.cycles_per_frame = DO_CYCLES_PER_FRAME;
    ub.rom = regions[0];
    if (!ub_alloc(2, 256, 0))
        return 0;
    gal_conv_tiles8(regions[1], &ub_chars, 512, GAL_ROT270, ub.tiles[0]);
    gal_conv_tiles8(regions[2], &ub_chars, 512, GAL_ROT270, ub.tiles[1]);
    gal_conv_sprites16(regions[3], &do_sprites, 128, GAL_ROT270, ub.sprites);
    do_palette(regions[4], regions[5]);
    ub.dsw0 = 0xdf; /* MAME's defaults: easy, upright, 3 lives */
    ub.dsw1 = 0xff; /* 1C1C */

    z80_t *cpu = &gal.cpu[0];
    map(cpu, 0x00, 0x80, ub.rom, 0);
    map(cpu, 0x80, 0x08, gal.ram + DO_BG, 1);
    map(cpu, 0x88, 0x08, gal.ram + DO_FG, 1);
    cpu->rd_page[0x90] = gal_zeros;
    cpu->wr_page[0x90] = gal.ram + DO_SPR;
    map(cpu, 0xe0, 0x10, gal.ram + DO_RAM, 1);
    cpu->read = do_read;
    cpu->write = do_write;
    return 1;
}

static void ub_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    ub.scroll_x = ub.scroll_y = 0;
    const uint32_t clock = (ub.var == UB_MRDO) ? 4100000 : 4000000;
    for (int k = 0; k < 2; k++)
        gal_sn_init(&ub.sn[k], 1, clock);
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
}

static void ub_run_slice(int q)
{
    z80_t *cpu = &gal.cpu[0];
    if (q == 0 && ub.var == UB_LADYBUG)
        z80_set_nmi_line(cpu, (gal.buttons & GAL_BTN_COIN) != 0); /* the left coin slot */
    ub.run_base = q * ub.cycles_per_frame / 4;
    ub.run_len = (q + 1) * ub.cycles_per_frame / 4 - ub.run_base;
    z80_run(cpu, ub.run_len);
    if (q == 3 && ub.var == UB_MRDO)
        z80_hold_irq(cpu, 0xff);
}

static void ub_sound(int16_t *out, int n)
{
    gal_sn_render(ub.sn, 2, 192, out, n);
}

/* --- Video --------------------------------------------------------------------- */

/* The raster turned upright: its pixel (x, y) is the upright pixel
 * (y - 16, 271 - x), so an s x s element at (x, y) has its upright corner at
 * (y - 16, 272 - s - x). */
#define UP_X(y) ((y) - 16)
#define UP_Y(x, s) (272 - (s) - (x))

/* an 8x8 element with flips; pen 0 is transparent */
static void draw8(const gal_band_t *b, int px, int py, const uint16_t *rows, int flipx, int flipy,
                  const uint16_t *colors)
{
    int ys = py, ye = py + 8, xs = px, xe = px + 8;
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
    uint16_t *row = b->buf + (ys - b->y0) * b->w + (xs - b->x0);
    const int skip = 2 * (xs - px), cols = xe - xs;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        const int r = y - py;
        uint32_t pix = rows[flipy ? 7 - r : r];
        if (flipx)
            pix = gal_rev2(pix) >> 16;
        pix >>= skip;
        for (int c = 0; c < cols && pix; c++, pix >>= 2)
            if (pix & 3)
                row[c] = colors[pix & 3];
    }
}

static void ub_prepare_frame(void)
{
    gal.active_sprites = 0;
    if (ub.var == UB_LADYBUG)
    {
        /* MAME's draw_sprites: 14 blocks of 16, each from its last sprite in
         * use back to its first, the blocks from the bottom of the RAM up */
        const uint8_t *spr = gal.ram + LB_SPR;
        for (int offs = 0x380; offs >= 0x80; offs -= 0x40)
        {
            int i = 0;
            while (i < 0x40 && spr[offs + i])
                i += 4;
            while (i > 0 && gal.active_sprites < GAL_MAX_SPRITES)
            {
                i -= 4;
                const uint8_t *s = spr + offs + i;
                if (!(s[0] & 0x80))
                    continue;
                gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
                const int big = (s[0] & 0x40) != 0;
                const int x = s[3], y = (offs >> 2) | (s[0] & 0x0f);
                const int code = s[1] | ((s[2] & 0x10) << 4);
                /* flags: 1 flip x, 2 flip y (upright), 4 16x16 */
                o->flags = (uint8_t)(((s[0] & 0x10) ? 1 : 0) | ((s[0] & 0x20) ? 2 : 0) | (big ? 4 : 0));
                o->color = s[2] & 0x0f;
                if (big)
                {
                    o->code = (uint8_t)(code >> 2);
                    o->x = (int16_t)UP_X(y - 8);
                    o->y = (int16_t)UP_Y(x, 16);
                }
                else
                {
                    /* 9 bits: the top one in flags */
                    o->code = (uint8_t)code;
                    o->flags |= (code >> 8) << 3;
                    o->x = (int16_t)UP_X(y);
                    o->y = (int16_t)UP_Y(x, 8);
                }
            }
        }
    }
    else
    {
        /* from the last sprite to the first */
        const uint8_t *spr = gal.ram + DO_SPR;
        for (int offs = 0xfc; offs >= 0; offs -= 4)
        {
            const uint8_t *s = spr + offs;
            if (!s[1])
                continue;
            gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
            o->code = s[0];
            o->color = s[2] & 0x0f;
            o->flags = (uint8_t)(((s[2] & 0x20) ? 1 : 0) | ((s[2] & 0x10) ? 2 : 0) | 4);
            o->x = (int16_t)UP_X(256 - s[1]);
            o->y = (int16_t)UP_Y(s[3], 16);
        }
    }
}

/* The raster rectangle behind an upright band: lines y0..y1, x0..x1. */
typedef struct
{
    int x0, x1, y0, y1;
} raster_t;

static raster_t raster_of(const gal_band_t *b)
{
    raster_t r;
    r.y0 = b->x0 + 16;
    r.y1 = b->x0 + b->w - 1 + 16;
    r.x0 = 271 - (b->y0 + b->h - 1);
    r.x1 = 271 - b->y0;
    return r;
}

/* Lady Bug's layer: 32x32 tiles, each raster row of tiles scrolled by its own
 * register (MAME: bg_ram[(row & 3) << 5 | row >> 2]). */
static void lb_layer(const gal_band_t *b)
{
    const uint8_t *bg = gal.ram + LB_BG;
    const raster_t r = raster_of(b);
    for (int row = r.y0 >> 3; row <= r.y1 >> 3; row++)
    {
        const int s = bg[((row & 3) << 5) | (row >> 2)];
        for (int x = r.x0 - ((r.x0 + s) & 7); x <= r.x1; x += 8)
        {
            const int i = 32 * row + (((x + s) & 255) >> 3);
            const uint8_t attr = bg[0x400 + i];
            const int code = bg[i] | ((attr & 0x08) << 5);
            gal_draw_tile(b, UP_X(8 * row), UP_Y(x, 8), ub.tiles[0] + 8 * code, ub.tile_color + 4 * (attr & 7));
        }
    }
}

/* A Mr. Do! layer: 32x32 tiles scrolled as a whole; attribute bit 6 makes a
 * tile opaque. */
static void do_layer(const gal_band_t *b, const uint8_t *ram, const uint16_t *tiles, int sx, int sy)
{
    const raster_t r = raster_of(b);
    for (int y = r.y0 - ((r.y0 + sy) & 7); y <= r.y1; y += 8)
    {
        const int row = ((y + sy) & 255) >> 3;
        for (int x = r.x0 - ((r.x0 + sx) & 7); x <= r.x1; x += 8)
        {
            const int i = 32 * row + (((x + sx) & 255) >> 3);
            const uint8_t attr = ram[i];
            const int code = ram[0x400 + i] | ((attr & 0x80) << 1);
            const uint16_t *colors = ub.tile_color + 4 * (attr & 0x3f);
            if (attr & 0x40)
                gal_draw_tile_opaque(b, UP_X(y), UP_Y(x, 8), tiles + 8 * code, colors);
            else
                gal_draw_tile(b, UP_X(y), UP_Y(x, 8), tiles + 8 * code, colors);
        }
    }
}

static void ub_render_band(const gal_band_t *b)
{
    if (b->x0 >= PIC_X1 || b->x0 + b->w <= PIC_X0)
    {
        gal_fill_band(b, 0);
        return;
    }
    gal_fill_band(b, ub.back);
    if (ub.var == UB_LADYBUG)
        lb_layer(b);
    else
    {
        do_layer(b, gal.ram + DO_BG, ub.tiles[1], ub.scroll_x, ub.scroll_y);
        do_layer(b, gal.ram + DO_FG, ub.tiles[0], 0, 0);
    }
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const uint16_t *colors = ub.spr_color + 4 * o->color;
        if (o->flags & 4)
            gal_draw_sprite_fx(b, o->x, o->y, ub.sprites + 16 * o->code, o->flags & 1, o->flags & 2, colors, 1);
        else
            draw8(b, o->x, o->y, ub.small + 8 * (o->code | ((o->flags & 8) << 5)), o->flags & 1, o->flags & 2,
                  colors);
    }
    /* black left and right of the picture */
    for (int y = 0; y < b->h; y++)
    {
        uint16_t *row = b->buf + y * b->w;
        for (int x = b->x0; x < PIC_X0; x++)
            row[x - b->x0] = 0;
        for (int x = PIC_X1; x < b->x0 + b->w; x++)
            row[x - b->x0] = 0;
    }
}

/* --- The games --------------------------------------------------------------------- */

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t lb_hiscore[] = {
    {0x6073, 0x1b, 0x01, 0x00}, /* the table */
    {0xd380, 0x75, 0xff, 0xff}, /* video RAM: the scores shown */
};
static const gal_hiscore_region_t do_hiscore[] = {
    {0xe017, 0x64, 0x01, 0x00}, /* the table */
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
#define UB_OPS(init_fn, hs)                                                                         \
    {                                                                                              \
        .init = init_fn, .reset = ub_reset, .run_slice = ub_run_slice, .prepare_frame = ub_prepare_frame, \
        .render_band = ub_render_band, .sound = ub_sound, .view_top = VIEW_TOP, .view_h = VIEW_H,  \
        .hiscore = hs, .nhiscore = N(hs),                                                          \
    }

const gal_game_ops_t gal_ladybug_ops = UB_OPS(lb_init, lb_hiscore);
const gal_game_ops_t gal_mrdo_ops = UB_OPS(do_init, do_hiscore);
