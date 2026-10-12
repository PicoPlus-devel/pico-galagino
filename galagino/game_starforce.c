/*
 * Star Force (Tehkan, 1984), after MAME's tecmo/senjyo.cpp.
 *
 * A Z80 at 4 MHz, its IRQ at the vertical blank until the game acknowledges
 * it. A sound Z80 at 2 MHz with three SN76489A, a Z80 PIO that hands it the
 * main CPU's commands with an interrupt, and a Z80 CTC: its timers drive the
 * music, and channel 2 clocks a 16-step waveform PROM into a DAC. 256x224
 * shown (MAME ROT90: the upright picture is 224x256, drawn at y 16..271).
 *
 * Behind everything a pattern of stripes; three layers of 16x16 tiles (256x512,
 * each scrolled in x and y), a layer of characters scrolled per column, and
 * 32 sprites of 16x16 or 32x32 in four priorities between the layers. All
 * three bits per pixel; the palette is RAM, 512 colours of one byte.
 *
 * The game does not fit in SRAM: its programs and characters go to SRAM, the
 * converted tiles and sprites (144 KB) to PSRAM, or come from the flash cache.
 */
#include "gal_internal.h"

#define MAIN_CYCLES_PER_FRAME 67584 /* 4 MHz at 59.19 Hz */
#define SND_CYCLES_PER_FRAME 33792  /* 2 MHz */
#define SND_STEPS 16
#define VIEW_TOP 16

/* Galagino's RAM buffer */
#define RAM_MAIN 0x0000   /* 8000-8fff */
#define RAM_FG 0x1000     /* 9000-97ff: characters, then their colours */
#define RAM_SPRITE 0x1800 /* 9800-987f */
#define RAM_PAL 0x1900    /* 9c00-9dff */
#define RAM_SCROLL 0x1b00 /* 9e00-9e3f */
#define RAM_RADAR 0x1c00  /* b800-bbff */
#define RAM_SOUND 0x2000  /* the sound CPU's 4000-43ff */

#define FG_BYTES (8 * 4)    /* an 8x8 element */
#define BIG_BYTES (16 * 8)  /* a 16x16 one */

static struct
{
    uint8_t *rom, *rom_sound, *bgram; /* bgram: a000-b7ff, bg3, bg2, bg1 */
    const uint8_t *dac_prom;
    uint32_t *fg;
    const uint32_t *bg[3], *spr;
    uint16_t *colors; /* [512], in the arena */
    uint8_t *stripe; /* [256], the background pen of each raster column */
    uint8_t stripes_on;
    uint8_t latch;
    gal_ctc_t ctc;
    uint8_t pio_vector, pio_irq_enable, pio_pending, pio_want_mask;
    uint8_t dac_volume, dac_enable, dac_step;
    uint32_t dac_phase;
    gal_sn_t sn[3];
} sf;


/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t fg_layout = {
    8, 8, 3, {0, 0x1000 * 8, 0x2000 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

/* the tiles of the three layers: 0x2000 (bg1, bg2) or 0x1000 (bg3) per plane */
#define BIG_LAYOUT(plane)                                                                  \
    {                                                                                      \
        16, 16, 3, {0, (plane) * 8, (plane) * 16},                                         \
            {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71},                      \
            {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184}, 32 * 8 \
    }
static const gal_layout_t big_layout[4] = {BIG_LAYOUT(0x2000), BIG_LAYOUT(0x2000), BIG_LAYOUT(0x1000),
                                           BIG_LAYOUT(0x4000)}; /* bg1, bg2, bg3, sprites */
static const int big_count[4] = {256, 256, 128, 512};

/* the tables the flash cache keeps: the three layers' tiles, the sprites */
static void sf_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    gal_conv_4bpp(src, &big_layout[i], ofs / BIG_BYTES, len / BIG_BYTES, GAL_ROT90, (uint32_t *)out);
}

/* --- Main CPU ---------------------------------------------------------------------- */

static uint8_t sf_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0;
    switch (addr)
    {
    case 0xd000: /* P1, active high */
        if (keys & GAL_BTN_RIGHT)
            v |= 0x01;
        if (keys & GAL_BTN_LEFT)
            v |= 0x02;
        if (keys & GAL_BTN_UP)
            v |= 0x04;
        if (keys & GAL_BTN_DOWN)
            v |= 0x08;
        if (keys & GAL_BTN_FIRE)
            v |= 0x10;
        return v;
    case 0xd002: /* SYSTEM */
        if (keys & GAL_BTN_COIN)
            v |= 0x01;
        if (keys & GAL_BTN_START)
            v |= 0x04;
        if (keys & GAL_BTN_START2)
            v |= 0x08;
        return v;
    case 0xd004:
        return 0xc0; /* MAME's defaults: 1C1C, 3 lives, upright, demo sounds */
    default:
        return 0; /* d005: bonus at 50k, 200k and 500k, normal */
    }
}

static void sf_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    switch (addr)
    {
    case 0xd002: /* the IRQ is acknowledged */
        z80_set_irq_line(cpu, 0);
        break;
    case 0xd004:
        /* the command; the latch strobes the PIO */
        sf.latch = value;
        if (sf.pio_irq_enable)
            sf.pio_pending = 1;
        break;
    default:
        break; /* d000: the flip of a cocktail cabinet */
    }
}

/* --- Sound CPU --------------------------------------------------------------------- */

static uint8_t sf_snd_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    (void)addr;
    return 0;
}

static void sf_snd_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    switch (addr & 0xf000)
    {
    case 0x8000:
        gal_sn_write(&sf.sn[0], value);
        break;
    case 0x9000:
        gal_sn_write(&sf.sn[1], value);
        break;
    case 0xa000:
        gal_sn_write(&sf.sn[2], value);
        break;
    case 0xd000:
        sf.dac_volume = value & 0x0f;
        break;
    case 0xe000:
        sf.dac_enable = value & 1;
        break;
    default:
        break;
    }
}

/* the PIO (00-03: A data, A control, B data, B control) and the CTC (08-0b) */
static uint8_t sf_snd_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    const int p = port & 0xff;
    if (p >= 8 && p < 12)
        return gal_ctc_read(&sf.ctc, p - 8);
    if (p == 0)
        return sf.latch;
    return 0xff;
}

static void sf_snd_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    const int p = port & 0xff;
    if (p >= 8 && p < 12)
    {
        gal_ctc_write(&sf.ctc, p - 8, value);
        return;
    }
    if (p != 1)
        return; /* port B is not used */
    /* port A's control: a mask after an interrupt control word that asks for
     * one, the vector, the interrupt control word (bit 7 enables), or the
     * enable alone (x3) */
    if (sf.pio_want_mask)
        sf.pio_want_mask = 0;
    else if (!(value & 1))
        sf.pio_vector = value;
    else if ((value & 0x0f) == 0x07)
    {
        sf.pio_irq_enable = value >> 7;
        sf.pio_want_mask = (value & 0x10) != 0;
    }
    else if ((value & 0x0f) == 0x03)
        sf.pio_irq_enable = value >> 7;
}

/* the CTC first, then the PIO, when the sound CPU can take one */
static void snd_interrupt(void)
{
    z80_t *cpu = &gal.cpu[1];
    if (cpu->irq_state)
        return;
    const int v = gal_ctc_take(&sf.ctc);
    if (v >= 0)
        z80_hold_irq(cpu, (uint8_t)v);
    else if (sf.pio_pending)
    {
        sf.pio_pending = 0;
        z80_hold_irq(cpu, sf.pio_vector);
    }
}

/* --- Set-up ----------------------------------------------------------------------- */

static int sf_init(uint8_t *const *regions)
{
    memset(&sf, 0, sizeof(sf));
    /* the programs, the characters and the layers' RAM to SRAM */
    sf.rom = (uint8_t *)gal_fast_alloc(0x8000);
    sf.rom_sound = (uint8_t *)gal_fast_alloc(0x2000);
    sf.bgram = (uint8_t *)gal_fast_alloc(0x1800);
    sf.fg = (uint32_t *)gal_fast_alloc(512 * FG_BYTES);
    sf.colors = (uint16_t *)gal_fast_alloc(512 * 2);
    sf.stripe = (uint8_t *)gal_fast_alloc(256);
    if (!sf.rom || !sf.rom_sound || !sf.bgram || !sf.fg || !sf.colors || !sf.stripe)
        return 0;
    memcpy(sf.rom, regions[0], 0x8000);
    memcpy(sf.rom_sound, regions[1], 0x2000);
    sf.dac_prom = regions[7];
    gal_conv_4bpp(regions[2], &fg_layout, 0, 512, GAL_ROT90, sf.fg);

    if (regions[GAL_MAX_REGIONS])
    {
        for (int k = 0; k < 3; k++)
            sf.bg[k] = (const uint32_t *)regions[GAL_MAX_REGIONS + k];
        sf.spr = (const uint32_t *)regions[GAL_MAX_REGIONS + 3];
    }
    else
    {
        for (int k = 0; k < 4; k++)
        {
            uint32_t *t = (uint32_t *)gal_alloc(big_count[k] * BIG_BYTES);
            if (!t)
                return 0;
            gal_conv_4bpp(regions[3 + k], &big_layout[k], 0, big_count[k], GAL_ROT90, t);
            if (k < 3)
                sf.bg[k] = t;
            else
                sf.spr = t;
        }
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x80; p++)
        cpu->rd_page[p] = sf.rom + 256 * p;
    for (int p = 0; p < 0x18; p++)
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = gal.ram + 256 * p; /* 8000-97ff */
    cpu->rd_page[0x98] = cpu->wr_page[0x98] = gal.ram + RAM_SPRITE;
    cpu->rd_page[0x9c] = cpu->wr_page[0x9c] = gal.ram + RAM_PAL;
    cpu->rd_page[0x9d] = cpu->wr_page[0x9d] = gal.ram + RAM_PAL + 0x100;
    cpu->rd_page[0x9e] = cpu->wr_page[0x9e] = gal.ram + RAM_SCROLL;
    for (int p = 0; p < 0x18; p++)
        cpu->rd_page[0xa0 + p] = cpu->wr_page[0xa0 + p] = sf.bgram + 256 * p;
    for (int p = 0; p < 4; p++)
        cpu->rd_page[0xb8 + p] = cpu->wr_page[0xb8 + p] = gal.ram + RAM_RADAR + 256 * p;
    for (int p = 0xe0; p < 0x100; p++)
    {
        cpu->rd_page[p] = gal_zeros;
        cpu->wr_page[p] = gal_ignored;
    }
    cpu->read = sf_read;
    cpu->write = sf_write;

    z80_t *snd = &gal.cpu[1];
    for (int p = 0; p < 0x20; p++)
        snd->rd_page[p] = sf.rom_sound + 256 * p;
    for (int p = 0; p < 4; p++)
        snd->rd_page[0x40 + p] = snd->wr_page[0x40 + p] = gal.ram + RAM_SOUND + 256 * p;
    snd->read = sf_snd_read;
    snd->write = sf_snd_write;
    snd->in = sf_snd_in;
    snd->out = sf_snd_out;
    return 1;
}

static void sf_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(sf.bgram, 0, 0x1800);
    memset(&sf.ctc, 0, sizeof(sf.ctc));
    sf.ctc.chain = 0x01; /* channel 0's zero count clocks channel 1 */
    sf.latch = 0;
    sf.pio_vector = sf.pio_irq_enable = sf.pio_pending = sf.pio_want_mask = 0;
    sf.dac_volume = sf.dac_enable = sf.dac_step = 0;
    sf.dac_phase = 0;
    for (int k = 0; k < 3; k++)
        gal_sn_init(&sf.sn[k], 0, 2000000); /* SN76489A: the SN76496's noise */
    for (int k = 0; k < 2; k++)
    {
        z80_init(&gal.cpu[k]);
        z80_reset(&gal.cpu[k]);
    }
}

static void sf_run_slice(int q)
{
    for (int i = 0; i < SND_STEPS / 4; i++)
    {
        const int step = q * (SND_STEPS / 4) + i;
        const int main_cycles =
            (step + 1) * MAIN_CYCLES_PER_FRAME / SND_STEPS - step * MAIN_CYCLES_PER_FRAME / SND_STEPS;
        const int snd_cycles = (step + 1) * SND_CYCLES_PER_FRAME / SND_STEPS - step * SND_CYCLES_PER_FRAME / SND_STEPS;
        z80_run(&gal.cpu[0], main_cycles);
        snd_interrupt();
        z80_run(&gal.cpu[1], snd_cycles);
        gal_ctc_clock(&sf.ctc, snd_cycles);
    }
    if (q == 3)
        z80_set_irq_line(&gal.cpu[0], 1); /* MAME's irq0_line_assert */
}

static void sf_sound(int16_t *out, int n)
{
    gal_sn_render(sf.sn, 3, 192, out, n);
    /* the DAC: the waveform PROM stepped by CTC channel 2 */
    const int32_t period = gal_ctc_period(&sf.ctc, 2);
    if (!period || !sf.dac_enable || !sf.dac_volume)
        return;
    const uint32_t step = (uint32_t)(((uint64_t)2000000 << 16) / ((uint64_t)period * gal.samplerate));
    for (int i = 0; i < n; i++)
    {
        sf.dac_phase += step;
        sf.dac_step = (uint8_t)((sf.dac_step + (sf.dac_phase >> 16)) & 0x0f);
        sf.dac_phase &= 0xffff;
        const int v = ((int)sf.dac_prom[sf.dac_step] - 128) * sf.dac_volume / 15 * 4;
        const int s = out[i] + v;
        out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90, as Bomb Jack: the raster's pixel (x, y) is the upright pixel
 * (239 - y, x + 16) */
#define UP_X(y, s) (240 - (s) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void sf_prepare_frame(void)
{
    const uint8_t *pal = gal.ram + RAM_PAL;
    for (int i = 0; i < 512; i++)
    {
        /* IIBBGGRR */
        const uint8_t v = pal[i], in = (v >> 6) & 3;
        const int r = (v << 2) & 0x0c, g = v & 0x0c, b = (v >> 2) & 0x0c;
        const int R = r ? (r | in) * 17 : 0, G = g ? (g | in) * 17 : 0, B = b ? (b | in) * 17 : 0;
        sf.colors[i] = gal_color((uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)));
    }

    /* MAME's draw_bgbitmap: vertical stripes of pens 384.., each
     * (width + 1) / 16 columns wide; none when the width is ff */
    const uint8_t width = (uint8_t)(gal.ram[RAM_SCROLL + 0x27] + 1);
    sf.stripes_on = width != 0;
    int pen = 0, count = 0;
    for (int x = 0; x < 256; x++)
    {
        sf.stripe[x] = (uint8_t)pen;
        count += 0x10;
        if (width && count >= width)
        {
            pen = (pen + 1) & 0x0f;
            count -= width;
        }
    }

    /* the sprites, drawn by priority in pf_render_band */
    gal.active_sprites = 0;
    const uint8_t *spr = gal.ram + RAM_SPRITE;
    for (int offs = 0x80 - 4; offs >= 0; offs -= 4)
    {
        const uint8_t *s = spr + offs;
        const int big = (s[0] & 0xc0) == 0xc0;
        const int size = big ? 32 : 16;
        const int x = s[3], y = (big ? 224 : 240) - s[2];
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        o->code = big ? (s[0] & 0x7f) : s[0];
        /* color: the colour, and the priority in bits 4-5 */
        o->color = s[1] & 0x37;
        /* flags: 1 flip x, 2 flip y (upright), 4 large */
        o->flags = (uint8_t)(((s[1] & 0x80) ? 1 : 0) | ((s[1] & 0x40) ? 2 : 0) | (big ? 4 : 0));
        o->x = (int16_t)UP_X(y, size);
        o->y = (int16_t)UP_Y(x);
    }
}

static void draw_sprites(const gal_band_t *b, int priority)
{
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        if (((o->color >> 4) & 3) != priority)
            continue;
        const uint16_t *colors = sf.colors + 320 + 8 * (o->color & 7);
        const int fx = o->flags & 1, fy = (o->flags & 2) != 0;
        if (!(o->flags & 4))
        {
            gal_draw_4bpp(b, o->x, o->y, 16, sf.spr + o->code * (BIG_BYTES / 4), fx, fy, colors, 0);
            continue;
        }
        /* a 32x32 sprite is the 16x16 ones 4n..4n+3, raster quadrants
         * (0,0), (1,0), (0,1), (1,1) */
        for (int k = 0; k < 4; k++)
        {
            int qx = k & 1, qy = k >> 1;
            if (fy)
                qx ^= 1;
            if (fx)
                qy ^= 1;
            gal_draw_4bpp(b, o->x + 16 * (1 - qy), o->y + 16 * qx, 16, sf.spr + (4 * o->code + k) * (BIG_BYTES / 4),
                          fx, fy, colors, 0);
        }
    }
}

/* the raster rectangle behind the band */
typedef struct
{
    int x0, x1, y0, y1;
} raster_t;

static raster_t raster_of(const gal_band_t *b)
{
    raster_t r = {b->y0 - VIEW_TOP, b->y0 + b->h - 1 - VIEW_TOP, 239 - (b->x0 + b->w - 1), 239 - b->x0};
    if (r.x0 < 0)
        r.x0 = 0;
    if (r.x1 > 255)
        r.x1 = 255;
    return r;
}

/* layer k (0 bg1, 1 bg2, 2 bg3): 16x32 tiles of 16x16, pen 0 transparent */
static void draw_layer(const gal_band_t *b, int k)
{
    /* the scroll registers: bg1 at 9e30, bg2 at 9e28 (Star Force: bg1's),
     * bg3 at 9e20 */
    static const uint8_t reg[3] = {0x30, 0x30, 0x20};
    const uint8_t *sc = gal.ram + RAM_SCROLL + reg[k];
    const int sx = sc[5], sy = sc[0] | (sc[1] << 8);
    const uint8_t *ram = sf.bgram + 0x800 * (2 - k);
    const raster_t r = raster_of(b);
    for (int y = r.y0 - ((r.y0 + sy) & 15); y <= r.y1; y += 16)
    {
        const int row = ((y + sy) & 511) >> 4;
        for (int x = r.x0 - ((r.x0 + sx) & 15); x <= r.x1; x += 16)
        {
            const uint8_t code = ram[row * 16 + (((x + sx) & 255) >> 4)];
            int color = code >> 5;
            if (k == 0) /* bitswap<3>(color, 1, 0, 2) */
                color = ((color & 2) << 1) | ((color & 1) << 1) | ((color >> 2) & 1);
            gal_draw_4bpp(b, UP_X(y, 16), UP_Y(x), 16, sf.bg[k] + code * (BIG_BYTES / 4), 0, 0,
                          sf.colors + 64 * (k + 1) + 8 * color, 0);
        }
    }
}

/* the characters, each raster column of tiles scrolled in y on its own */
static void draw_fg(const gal_band_t *b)
{
    const uint8_t *vram = gal.ram + RAM_FG, *cram = vram + 0x400, *scroll = gal.ram + RAM_SCROLL;
    const raster_t r = raster_of(b);
    for (int col = r.x0 >> 3; col <= r.x1 >> 3; col++)
    {
        const int sy = scroll[col];
        for (int y = r.y0 - ((r.y0 + sy) & 7); y <= r.y1; y += 8)
        {
            const int i = (((y + sy) & 255) >> 3) * 32 + col;
            const uint8_t attr = cram[i];
            const int code = vram[i] + ((attr & 0x10) << 4);
            gal_draw_4bpp(b, UP_X(y, 8), UP_Y(8 * col), 8, sf.fg + code * (FG_BYTES / 4), (attr & 0x80) != 0, 0,
                          sf.colors + 8 * (attr & 7), 0);
        }
    }
}

static void sf_render_band(const gal_band_t *b)
{
    /* the stripes run along raster x: one colour per upright row */
    for (int j = 0; j < b->h; j++)
    {
        const int x = b->y0 + j - VIEW_TOP;
        const uint16_t c = (x < 0 || x > 255) ? 0 : sf.colors[sf.stripes_on ? 384 + sf.stripe[x] : 0];
        uint16_t *row = b->buf + j * b->w;
        for (int i = 0; i < b->w; i++)
            row[i] = c;
    }
    draw_sprites(b, 0);
    draw_layer(b, 2);
    draw_sprites(b, 1);
    draw_layer(b, 1);
    draw_sprites(b, 2);
    draw_layer(b, 0);
    draw_sprites(b, 3);
    draw_fg(b);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t sf_hiscore[] = {
    {0x8038, 0x70, 0x00, 0x00}, /* the table */
    {0x8348, 0x04, 0x00, 0x00}, /* the top score */
    /* video RAM: the top score shown */
    {0x9261, 1, 0x23, 0x23}, {0x9241, 1, 0x23, 0x23}, {0x9221, 1, 0x23, 0x23}, {0x9201, 1, 0x1d, 0x1d},
    {0x91e1, 1, 0x18, 0x18}, {0x91c1, 1, 0x21, 0x21}, {0x91a1, 1, 0x18, 0x18}, {0x9181, 1, 0x18, 0x18},
};

const gal_game_ops_t gal_starforc_ops = {
    .init = sf_init,
    .reset = sf_reset,
    .run_slice = sf_run_slice,
    .prepare_frame = sf_prepare_frame,
    .render_band = sf_render_band,
    .sound = sf_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .large = 1,
    .nderived = 4,
    .derived_src = {3, 4, 5, 6},
    .derived_size = {256 * BIG_BYTES, 256 * BIG_BYTES, 128 * BIG_BYTES, 512 * BIG_BYTES},
    .derive = sf_derive,
    .hiscore = sf_hiscore,
    .nhiscore = sizeof(sf_hiscore) / sizeof(sf_hiscore[0]),
};
