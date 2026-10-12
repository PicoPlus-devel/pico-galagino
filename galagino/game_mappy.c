/*
 * Namco's Mappy (1983) and The Tower of Druaga (1984), after MAME's
 * namco/mappy.cpp, mappy_v.cpp and namcoio.cpp.
 *
 * Two 6809s at 1.54 MHz: the main CPU, and the sub CPU that plays the eight
 * voices of the 15XX through the RAM they share. A latch of eight bits, which
 * either CPU writes, holds their IRQ enables, the sound enable and the resets
 * of the sub CPU and the custom I/O chips (58XX, and 56XX for Druaga's DIP
 * switches, gal_namcoio.c), which run their command after each vertical
 * blank.
 *
 * 288x224 (MAME ROT90: the full upright 224x288 picture). A tilemap of 36x60
 * 8x8 tiles of two bits, the middle 32 columns scrolled vertically, the tiles
 * with attribute bit 6 drawn again in front of the sprites (transparent where
 * their colour is black); 64 sprites of 16x16 to 32x32, four bits per pixel,
 * transparent where their lookup gives colour 15.
 */
#include "gal_internal.h"

#include "m6809.h"

#define CYCLES_PER_FRAME 25344 /* 1.536 MHz at 60.6 Hz */
#define STEPS 32

/* The RAM, in the arena (larger than Galagino's buffer): the main CPU's
 * 0000-27ff, then the shared RAM */
#define RAM_VIDEO 0x0000  /* 0000-07ff the codes, 0800-0fff the attributes */
#define RAM_SPR1 0x1780   /* code, colour */
#define RAM_SPR2 0x1f80   /* y, x */
#define RAM_SPR3 0x2780   /* flags, x high bit and disable */
#define RAM_SHARED 0x2800 /* 4000-43ff, the 15XX's registers first */
#define RAM_SIZE 0x2c00

static struct
{
    uint8_t druaga;
    m6809_state *cpu, *sub;
    uint8_t *rom, *rom_sub;
    uint16_t *tiles;    /* [256][8] */
    const uint32_t *sprites; /* [128][32] */
    uint16_t *tile_colors; /* [64][4] */
    uint8_t *tile_mask;    /* [64]: the pens drawn black, transparent in front */
    uint16_t *spr_colors;  /* [16 or 64][16] */
    uint16_t *spr_mask;    /* [16 or 64] */
    int8_t *waves;
    uint8_t *ram;
    uint8_t main_irq, sub_irq, sound_enable, io_reset, sub_reset, scroll, mux;
    gal_namcoio_t io[2];
} mp;

/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t chr_layout = {
    8, 8, 2, {0, 4}, {64, 65, 66, 67, 0, 1, 2, 3}, {0, 8, 16, 24, 32, 40, 48, 56}, 16 * 8};
/* MAME's, on the two ROMs interleaved a byte at a time; here on the ROMs one
 * after the other */
static const gal_layout_t spr_layout = {
    16, 16, 4, {0, 4, 0x2000 * 8, 0x2000 * 8 + 4},
    {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195},
    {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312},
    64 * 8};

/* --- The custom I/O chips (MAME's namcoio.cpp) ----------------------------------- */

/* The chips' four inputs, active low: chip 0 the coins, the joystick, the
 * second player, the buttons; chip 1 the DIP switches */
static uint8_t io_in(int chip, int port)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0x0f;
    if (chip == 0)
    {
        switch (port)
        {
        case 0:
            if (keys & GAL_BTN_COIN)
                v &= ~0x01;
            break;
        case 1:
            if (mp.druaga && (keys & GAL_BTN_UP))
                v &= ~0x01;
            if (keys & GAL_BTN_RIGHT)
                v &= ~0x02;
            if (mp.druaga && (keys & GAL_BTN_DOWN))
                v &= ~0x04;
            if (keys & GAL_BTN_LEFT)
                v &= ~0x08;
            break;
        case 3:
            if (keys & GAL_BTN_FIRE)
                v &= ~0x01;
            if (keys & GAL_BTN_START)
                v &= ~0x04;
            if (keys & GAL_BTN_START2)
                v &= ~0x08;
            break;
        default:
            break;
        }
        return v;
    }
    /* MAME's defaults: Mappy all off; Druaga 3 lives */
    const uint8_t dsw1 = mp.druaga ? 0xfc : 0xff, dsw2 = 0xff, dsw0 = 0x0f;
    switch (port)
    {
    case 0:
        return (mp.mux ? dsw2 >> 4 : dsw2) & 0x0f;
    case 1:
        return dsw1 & 0x0f;
    case 2:
        return dsw1 >> 4;
    default:
        return dsw0;
    }
}

static uint8_t io_in0(int port)
{
    return io_in(0, port);
}

static uint8_t io_in1(int port)
{
    return io_in(1, port);
}

static void io_mux(int value)
{
    mp.mux = value & 1;
}

static void io_reset_chips(void)
{
    for (int k = 0; k < 2; k++)
        gal_namcoio_reset(&mp.io[k]);
}

/* --- CPUs ------------------------------------------------------------------------ */

/* the latch: bit (addr >> 1) & 7 is addr & 1 */
static void mp_latch(uint16_t addr)
{
    const uint8_t v = addr & 1;
    switch ((addr >> 1) & 7)
    {
    case 0:
        mp.sub_irq = v;
        if (!v)
            mp.sub->irq_pending = 0;
        break;
    case 1:
        mp.main_irq = v;
        if (!v)
            mp.cpu->irq_pending = 0;
        break;
    case 3:
        mp.sound_enable = v;
        break;
    case 4:
        if (!v && !mp.io_reset)
            io_reset_chips();
        mp.io_reset = !v;
        break;
    case 5:
        if (v && mp.sub_reset)
        {
            m6809_reset(mp.sub);
            mp.sub->icount = 0;
        }
        mp.sub_reset = !v;
        break;
    default:
        break; /* 2 the flip */
    }
}

static uint8_t mp_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    if ((addr & 0xffe0) == 0x4800)
        return 0xf0 | mp.io[(addr >> 4) & 1].ram[addr & 0x0f];
    return 0xff;
}

static void mp_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    (void)s;
    if ((addr & 0xf800) == 0x3800)
        mp.scroll = (uint8_t)(addr >> 3);
    else if ((addr & 0xffe0) == 0x4800)
        mp.io[(addr >> 4) & 1].ram[addr & 0x0f] = value & 0x0f;
    else if ((addr & 0xfff0) == 0x5000)
        mp_latch(addr);
    /* 8000: the watchdog */
}

static uint8_t mp_sub_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    (void)addr;
    return 0xff;
}

static void mp_sub_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    (void)s;
    (void)value;
    if ((addr & 0xfff0) == 0x2000)
        mp_latch(addr);
}

/* --- Set-up ----------------------------------------------------------------------- */

/* MAME's mappy palette: resistors of 1K, 470 and 220 ohm, no pull-down */
static void mp_palette(const uint8_t *prom, uint16_t *pal)
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

static void map_unused(m6809_state *c, int from, int to)
{
    for (int p = from; p < to; p++)
    {
        c->rd_page[p] = gal_zeros;
        c->wr_page[p] = gal_ignored;
    }
}

/* the table the flash cache keeps for Druaga: the sprites */
static void mp_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    (void)i;
    gal_conv_4bpp(src, &spr_layout, ofs / 128, len / 128, GAL_ROT90, (uint32_t *)out);
}

/* Druaga is large: its programs are copied to SRAM, its sprites read from
 * PSRAM or the flash cache. */
static int mp_init_common(uint8_t *const *regions, int druaga)
{
    memset(&mp, 0, sizeof(mp));
    mp.druaga = (uint8_t)druaga;
    const int rom_size = druaga ? 0x8000 : 0x6000, ngroups = druaga ? 64 : 16;
    if (druaga)
    {
        mp.rom = (uint8_t *)gal_fast_alloc(rom_size);
        mp.rom_sub = (uint8_t *)gal_fast_alloc(0x2000);
        if (!mp.rom || !mp.rom_sub)
            return 0;
        memcpy(mp.rom, regions[0], rom_size);
        memcpy(mp.rom_sub, regions[1], 0x2000);
    }
    else
    {
        mp.rom = regions[0];
        mp.rom_sub = regions[1];
    }
    mp.cpu = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    mp.sub = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    mp.tiles = (uint16_t *)gal_fast_alloc(256 * 8 * 2);
    mp.tile_colors = (uint16_t *)gal_fast_alloc(64 * 4 * 2);
    mp.tile_mask = (uint8_t *)gal_fast_alloc(64);
    mp.spr_colors = (uint16_t *)gal_fast_alloc(ngroups * 16 * 2);
    mp.spr_mask = (uint16_t *)gal_fast_alloc(ngroups * 2);
    mp.waves = (int8_t *)gal_fast_alloc(256);
    mp.io[0].in = io_in0;
    mp.io[1].in = io_in1;
    mp.io[1].out = io_mux;
    mp.ram = (uint8_t *)gal_fast_alloc(RAM_SIZE);
    if (!mp.ram || !mp.cpu || !mp.sub || !mp.tiles || !mp.tile_colors || !mp.tile_mask || !mp.spr_colors ||
        !mp.spr_mask || !mp.waves)
        return 0;
    /* the character ROM is read inverted (MAME's ROMREGION_INVERT) */
    gal_conv_tiles8(regions[2], &chr_layout, 256, GAL_ROT90, mp.tiles);
    for (int i = 0; i < 256 * 8; i++)
        mp.tiles[i] ^= 0xffff;
    if (regions[GAL_MAX_REGIONS])
        mp.sprites = (const uint32_t *)regions[GAL_MAX_REGIONS];
    else
    {
        uint32_t *sprites = (uint32_t *)gal_alloc(128 * 128);
        if (!sprites)
            return 0;
        gal_conv_4bpp(regions[3], &spr_layout, 0, 128, GAL_ROT90, sprites);
        mp.sprites = sprites;
    }
    gal_conv_waves(regions[7], 8, mp.waves);

    /* characters through the first lookup PROM into the second 16 colours,
     * sprites through the second into the first 16 */
    uint16_t pal[32];
    mp_palette(regions[4], pal);
    for (int i = 0; i < 64 * 4; i++)
    {
        const uint8_t lut = regions[5][i] & 0x0f;
        mp.tile_colors[i] = pal[0x10 | lut];
        if (lut == 0x0f)
            mp.tile_mask[i >> 2] |= (uint8_t)(1u << (i & 3));
    }
    memset(mp.spr_mask, 0, ngroups * 2);
    for (int i = 0; i < ngroups * 16; i++)
    {
        const uint8_t lut = regions[6][i] & 0x0f;
        mp.spr_colors[i] = pal[lut];
        if (lut == 0x0f)
            mp.spr_mask[i >> 4] |= (uint16_t)(1u << (i & 15));
    }

    m6809_state *c = mp.cpu;
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 0x28; p++) /* the video RAM and the RAM */
        c->rd_page[p] = c->wr_page[p] = mp.ram + 256 * p;
    map_unused(c, 0x28, 0x100);
    for (int p = 0x38; p < 0x40; p++) /* the scroll */
        c->wr_page[p] = NULL;
    for (int p = 0; p < 4; p++)
        c->rd_page[0x40 + p] = c->wr_page[0x40 + p] = mp.ram + RAM_SHARED + 256 * p;
    c->rd_page[0x48] = NULL; /* the I/O chips */
    c->wr_page[0x48] = NULL;
    c->wr_page[0x50] = NULL; /* the latch */
    for (int p = 0; p < rom_size / 256; p++)
        c->rd_page[0x100 - rom_size / 256 + p] = mp.rom + 256 * p;
    c->read = mp_read;
    c->write = mp_write;

    m6809_state *s = mp.sub;
    memset(s, 0, sizeof(*s));
    map_unused(s, 0, 0x100);
    for (int p = 0; p < 4; p++)
        s->rd_page[p] = s->wr_page[p] = mp.ram + RAM_SHARED + 256 * p;
    s->wr_page[0x20] = NULL; /* the latch */
    for (int p = 0; p < 0x20; p++)
        s->rd_page[0xe0 + p] = mp.rom_sub + 256 * p;
    s->read = mp_sub_read;
    s->write = mp_sub_write;
    return 1;
}

static int mappy_init(uint8_t *const *regions)
{
    return mp_init_common(regions, 0);
}

static int druaga_init(uint8_t *const *regions)
{
    return mp_init_common(regions, 1);
}

static void mp_reset(void)
{
    memset(mp.ram, 0, RAM_SIZE);
    mp.main_irq = mp.sub_irq = mp.sound_enable = mp.scroll = mp.mux = 0;
    /* the latch is clear at power-on: the sub CPU and the I/O chips held in
     * reset */
    mp.sub_reset = mp.io_reset = 1;
    io_reset_chips();
    m6809_reset(mp.cpu);
    mp.cpu->icount = 0;
    m6809_reset(mp.sub);
    mp.sub->icount = 0;
    gal_wsg15_reset();
}

static void mp_run_slice(int q)
{
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        const int len = (step + 1) * CYCLES_PER_FRAME / STEPS - step * CYCLES_PER_FRAME / STEPS;
        m6809_run(mp.cpu, len);
        if (!mp.sub_reset)
            m6809_run(mp.sub, len);
    }
    if (q == 3)
    {
        if (mp.main_irq)
            m6809_irq(mp.cpu);
        if (mp.sub_irq && !mp.sub_reset)
            m6809_irq(mp.sub);
        if (!mp.io_reset)
        {
            gal_namcoio_run_58xx(&mp.io[0]);
            if (mp.druaga)
                gal_namcoio_run_56xx(&mp.io[1]);
            else
                gal_namcoio_run_58xx(&mp.io[1]);
        }
    }
}

static void mp_sound(int16_t *out, int n)
{
    gal_wsg15_render(mp.ram + RAM_SHARED, mp.waves, mp.sound_enable, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (223 - y, x), so
 * an s x s element at (x, y) has its upright corner at (224 - s - y, x); its
 * flips in x and y are flips in y and x upright */
#define UP_X(y, s) (224 - (s) - (y))

static void mp_prepare_frame(void)
{
    static const uint8_t gfx_offs[2][2] = {{0, 1}, {2, 3}};
    const uint8_t *s1 = mp.ram + RAM_SPR1, *s2 = mp.ram + RAM_SPR2, *s3 = mp.ram + RAM_SPR3;
    const int cmask = mp.druaga ? 0x3f : 0x0f;
    gal.active_sprites = 0;
    /* MAME's mappy_draw_sprites: the later on top */
    for (int offs = 0; offs < 0x80; offs += 2)
    {
        if (s3[offs + 1] & 2)
            continue;
        int code = s1[offs];
        const int color = s1[offs + 1] & cmask;
        const int flipx = s3[offs] & 1, flipy = (s3[offs] >> 1) & 1;
        const int sizex = (s3[offs] >> 2) & 1, sizey = (s3[offs] >> 3) & 1;
        const int sx = s2[offs + 1] + 0x100 * (s3[offs + 1] & 1) - 40;
        int sy = 256 - s2[offs] + 1;
        code &= ~sizex;
        code &= ~(sizey << 1);
        sy -= 16 * sizey;
        sy = (sy & 0xff) - 32;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++)
            {
                if (gal.active_sprites >= GAL_MAX_SPRITES)
                    return;
                gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
                o->code = (uint8_t)((code + gfx_offs[y ^ (sizey & flipy)][x ^ (sizex & flipx)]) & 0x7f);
                o->color = (uint8_t)color;
                /* flags: 1 flip x, 2 flip y (upright) */
                o->flags = (uint8_t)(flipy | (flipx << 1));
                o->x = (int16_t)UP_X(sy + 16 * y, 16);
                o->y = (int16_t)(sx + 16 * x);
            }
    }
}

/* a tile in front of the sprites: its black pens left out */
static void draw_tile_front(const gal_band_t *b, int px, int py, const uint16_t *tile, const uint16_t *colors,
                            uint8_t transparent)
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
        uint32_t pix = (uint32_t)tile[y - py] >> skip;
        for (int c = 0; c < cols; c++, pix >>= 2)
            if (!((transparent >> (pix & 3)) & 1))
                row[c] = colors[pix & 3];
    }
}

static void draw_tile(const gal_band_t *b, int idx, int px, int py, int front)
{
    const uint8_t attr = mp.ram[RAM_VIDEO + 0x800 + idx];
    const uint16_t *tile = mp.tiles + 8 * mp.ram[RAM_VIDEO + idx];
    const uint16_t *colors = mp.tile_colors + 4 * (attr & 0x3f);
    if (!front)
        gal_draw_tile_opaque(b, px, py, tile, colors);
    else if (attr & 0x40)
        draw_tile_front(b, px, py, tile, colors, mp.tile_mask[attr & 0x3f]);
}

/* MAME's mappy_tilemap_scan: columns 0, 1, 34 and 35 hold the score, the
 * rest is a 32x60 map scrolled vertically */
static void draw_tiles(const gal_band_t *b, int front)
{
    const int c0 = b->y0 >> 3, c1 = (b->y0 + b->h - 1) >> 3;
    for (int c = c0; c <= c1 && c < 36; c++)
    {
        const int col = c - 2;
        if (col & 0x20)
        {
            for (int r = 0; r < 28; r++)
            {
                const int x = 216 - 8 * r;
                if (x + 8 <= b->x0 || x >= b->x0 + b->w)
                    continue;
                draw_tile(b, ((r + 2) & 0x0f) + (r & 0x10) + ((col & 3) << 5) + 0x780, x, 8 * c, front);
            }
            continue;
        }
        const int r0 = mp.scroll >> 3, fine = mp.scroll & 7;
        for (int k = 0; k < 29; k++)
        {
            const int x = 216 - 8 * k + fine;
            if (x + 8 <= b->x0 || x >= b->x0 + b->w)
                continue;
            int r = r0 + k;
            if (r >= 60)
                r -= 60;
            draw_tile(b, (r << 5) + col, x, 8 * c, front);
        }
    }
}

static void mp_render_band(const gal_band_t *b)
{
    draw_tiles(b, 0);
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        gal_draw_4bpp_mask(b, o->x, o->y, 16, mp.sprites + 32 * o->code, o->flags & 1, (o->flags & 2) != 0,
                           mp.spr_colors + 16 * o->color, mp.spr_mask[o->color]);
    }
    draw_tiles(b, 1);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it; Mappy's last two
 * are the top score on the screen. */
static const gal_hiscore_region_t mappy_hiscore[] = {
    {0x1460, 0x28, 0x00, 0x41},
    {0x1385, 0x03, 0x00, 0x00},
    {0x07ed, 0x06, 0x00, 0x20},
    {0x07e0, 0x02, 0x00, 0x02},
};

static const gal_hiscore_region_t druaga_hiscore[] = {
    {0x102a, 0x32, 0x00, 0x45},
    {0x100b, 0x03, 0x00, 0x00},
};

static uint8_t mp_hs_read(uint16_t addr)
{
    return mp.ram[addr];
}

static void mp_hs_write(uint16_t addr, uint8_t value)
{
    mp.ram[addr] = value;
}

const gal_game_ops_t gal_mappy_ops = {
    .init = mappy_init,
    .reset = mp_reset,
    .run_slice = mp_run_slice,
    .prepare_frame = mp_prepare_frame,
    .render_band = mp_render_band,
    .sound = mp_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .hiscore = mappy_hiscore,
    .nhiscore = sizeof(mappy_hiscore) / sizeof(mappy_hiscore[0]),
    .hs_read = mp_hs_read,
    .hs_write = mp_hs_write,
};

const gal_game_ops_t gal_todruaga_ops = {
    .init = druaga_init,
    .reset = mp_reset,
    .run_slice = mp_run_slice,
    .prepare_frame = mp_prepare_frame,
    .render_band = mp_render_band,
    .sound = mp_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .large = 1,
    .nderived = 1,
    .derived_src = {3},
    .derived_size = {128 * 128},
    .derive = mp_derive,
    .hiscore = druaga_hiscore,
    .nhiscore = sizeof(druaga_hiscore) / sizeof(druaga_hiscore[0]),
    .hs_read = mp_hs_read,
    .hs_write = mp_hs_write,
};
