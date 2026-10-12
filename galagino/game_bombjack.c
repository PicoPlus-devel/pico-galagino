/*
 * Bomb Jack (Tehkan, 1984), after MAME's tecmo/bombjack.cpp.
 *
 * A Z80 at 4 MHz and a sound Z80 at 3 MHz with three AY-3-8910 at 1.5 MHz,
 * 264 lines at 59.19 Hz, 224 of them shown (MAME ROT90: the upright picture
 * is 224x256, drawn at y 16..271). The main CPU's NMI comes at the vertical
 * blank while the game allows it; the sound CPU's every frame, and it takes
 * the main CPU's commands from a latch.
 *
 * Graphics of three bits per pixel: a background picture of 16x16 tiles from
 * a map ROM, a character layer, and 24 sprites of 16x16 or 32x32 (four 16x16
 * ones; a pair of registers says which sprites are large). The palette is
 * RAM, 128 colours of 12 bits.
 *
 * The game does not fit in SRAM: its program goes to SRAM, the converted
 * graphics (80 KB) to PSRAM, or come from the flash cache.
 *
 * At power-on the game checks its ROMs and RAMs for about 20 seconds (the
 * screen with the colour bars) and only then takes coins. The check reads the
 * sprite RAM back, so it is readable here.
 */
#include "gal_internal.h"

#define MAIN_CYCLES_PER_FRAME 67584 /* 4 MHz at 59.19 Hz */
#define SND_CYCLES_PER_FRAME 50688  /* 3 MHz */
#define VIEW_TOP 16

/* Galagino's RAM buffer */
#define RAM_MAIN 0x0000   /* 8000-8fff */
#define RAM_VIDEO 0x1000  /* 9000-93ff */
#define RAM_COLOR 0x1400  /* 9400-97ff */
#define RAM_SPRITE 0x1800 /* 9800-987f, written only, mirrored to 99ff */
#define RAM_PAL 0x1900    /* 9c00-9cff, mirrored at 9d00 */
#define RAM_SOUND 0x1a00  /* the sound CPU's 4000-47ff */

#define FG_BYTES (8 * 4)       /* an 8x8 element */
#define BIG_BYTES (16 * 2 * 4) /* a 16x16 one */

static struct
{
    uint8_t *rom, *rom_sound;
    const uint8_t *bgmap;
    const uint32_t *fg, *bg, *spr; /* gal_conv_4bpp */
    uint16_t *colors; /* [128], in the arena */
    uint8_t spritectrl[2];
    uint8_t bg_image;
    uint8_t nmi_on;
    uint8_t latch;
    uint8_t ay_addr[3];
    uint8_t wd_enabled, wd_count; /* the watchdog */
} bj;


/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t fg_layout = {
    8, 8, 3, {0, 0x1000 * 8, 0x2000 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

static const gal_layout_t big_layout = {
    16, 16, 3, {0, 0x2000 * 8, 0x4000 * 8},
    {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71},
    {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184},
    32 * 8};

/* the tables the flash cache keeps: characters, background tiles, sprites */
static void bj_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    const int eb = i ? BIG_BYTES : FG_BYTES;
    gal_conv_4bpp(src, i ? &big_layout : &fg_layout, ofs / eb, len / eb, GAL_ROT90, (uint32_t *)out);
}

/* --- Main CPU --------------------------------------------------------------------- */

static uint8_t bj_read(z80_t *cpu, uint16_t addr)
{
    if ((addr & 0xfe00) == 0x9800)
        return gal.ram[RAM_SPRITE + (addr & 0x7f)];
    if ((addr & 0xfe00) == 0x9a00)
        return bj.spritectrl[addr & 1];
    if ((addr & 0xf800) == 0xb000)
    {
        const uint16_t keys = gal.buttons;
        uint8_t v = 0;
        switch (addr & 7)
        {
        case 0: /* P1, active high */
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
        case 2: /* SYSTEM */
            v = 0x00;
            if (keys & GAL_BTN_COIN)
                v |= 0x01;
            if (keys & GAL_BTN_START)
                v |= 0x04;
            if (keys & GAL_BTN_START2)
                v |= 0x08;
            return v;
        case 4:
            return 0xc0; /* MAME's defaults: 1C1C, 3 lives, upright, demo sounds */
        case 3: /* the watchdog is kicked; nothing drives the bus */
            bj.wd_count = 0;
            return 0xff;
        case 5:
            return 0x50; /* bird speed and enemies: moderate */
        default:
            return 0;
        }
    }
    return 0;
}

static void bj_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    switch (addr & 0xfe00)
    {
    case 0x9800: /* sprite RAM, 128 bytes four times */
        gal.ram[RAM_SPRITE + (addr & 0x7f)] = value;
        return;
    case 0x9a00:
        bj.spritectrl[addr & 1] = value & 0x0f;
        return;
    case 0x9e00:
        bj.bg_image = value & 0x1f;
        return;
    default:
        break;
    }
    if ((addr & 0xf800) == 0xb000)
    {
        if ((addr & 7) == 0)
        {
            bj.nmi_on = value & 1;
            if (!bj.nmi_on)
                z80_set_nmi_line(cpu, 0);
        }
        else if ((addr & 7) == 3 && bj.wd_enabled != (value & 1))
        {
            bj.wd_enabled = value & 1;
            bj.wd_count = 0;
        }
        /* 4: the flip of a cocktail cabinet */
    }
    else if ((addr & 0xf800) == 0xb800)
    {
        bj.latch = value;
    }
}

/* --- Sound CPU --------------------------------------------------------------------- */

static uint8_t bj_snd_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if ((addr & 0x6000) == 0x6000)
    {
        const uint8_t v = bj.latch;
        bj.latch = 0;
        return v;
    }
    return 0;
}

static void bj_snd_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    (void)addr;
    (void)value;
}

/* the chips at ports 00, 10 and 80 (mirrored over 6e) */
static int ay_chip(uint16_t port)
{
    switch (port & 0x90)
    {
    case 0x00:
        return 0;
    case 0x10:
        return 1;
    case 0x80:
        return 2;
    default:
        return -1;
    }
}

static uint8_t bj_snd_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    const int k = ay_chip(port);
    if (k < 0 || !(port & 1))
        return 0xff;
    return bj.ay_addr[k] < 16 ? gal.soundregs[16 * k + bj.ay_addr[k]] : 0xff;
}

static void bj_snd_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    const int k = ay_chip(port);
    if (k < 0)
        return;
    if (!(port & 1))
        bj.ay_addr[k] = value & 0x0f;
    else
        gal.soundregs[16 * k + bj.ay_addr[k]] = value;
}

/* --- Set-up ----------------------------------------------------------------------- */

static int bj_init(uint8_t *const *regions)
{
    memset(&bj, 0, sizeof(bj));
    /* the programs to SRAM */
    bj.rom = (uint8_t *)gal_fast_alloc(0xa000);
    bj.rom_sound = (uint8_t *)gal_fast_alloc(0x2000);
    bj.colors = (uint16_t *)gal_fast_alloc(128 * 2);
    if (!bj.rom || !bj.rom_sound || !bj.colors)
        return 0;
    memcpy(bj.rom, regions[0], 0xa000);
    memcpy(bj.rom_sound, regions[1], 0x2000);
    bj.bgmap = regions[4];

    if (regions[GAL_MAX_REGIONS])
    {
        bj.fg = (const uint32_t *)regions[GAL_MAX_REGIONS];
        bj.bg = (const uint32_t *)regions[GAL_MAX_REGIONS + 1];
        bj.spr = (const uint32_t *)regions[GAL_MAX_REGIONS + 2];
    }
    else
    {
        uint32_t *fg = (uint32_t *)gal_alloc(512 * FG_BYTES);
        uint32_t *bg = (uint32_t *)gal_alloc(256 * BIG_BYTES);
        uint32_t *spr = (uint32_t *)gal_alloc(256 * BIG_BYTES);
        if (!fg || !bg || !spr)
            return 0;
        gal_conv_4bpp(regions[2], &fg_layout, 0, 512, GAL_ROT90, fg);
        gal_conv_4bpp(regions[3], &big_layout, 0, 256, GAL_ROT90, bg);
        gal_conv_4bpp(regions[5], &big_layout, 0, 256, GAL_ROT90, spr);
        bj.fg = fg;
        bj.bg = bg;
        bj.spr = spr;
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x80; p++)
        cpu->rd_page[p] = bj.rom + 256 * p;
    for (int p = 0; p < 0x20; p++)
        cpu->rd_page[0xc0 + p] = bj.rom + 0x8000 + 256 * p;
    for (int p = 0; p < 0x18; p++)
    {
        /* 8000-97ff: RAM, video and colour RAM */
        cpu->rd_page[0x80 + p] = gal.ram + 256 * p;
        cpu->wr_page[0x80 + p] = gal.ram + 256 * p;
    }
    cpu->rd_page[0x9c] = cpu->rd_page[0x9d] = gal.ram + RAM_PAL;
    cpu->wr_page[0x9c] = cpu->wr_page[0x9d] = gal.ram + RAM_PAL;
    cpu->rd_page[0x9e] = cpu->rd_page[0x9f] = gal_zeros;
    for (int p = 0xa0; p < 0xb0; p++)
    {
        cpu->rd_page[p] = gal_zeros;
        cpu->wr_page[p] = gal_ignored;
    }
    for (int p = 0xe0; p < 0x100; p++)
    {
        cpu->rd_page[p] = gal_zeros;
        cpu->wr_page[p] = gal_ignored;
    }
    cpu->read = bj_read;
    cpu->write = bj_write;

    z80_t *snd = &gal.cpu[1];
    for (int m = 0; m < 0x100; m += 0x80)
    {
        for (int p = 0; p < 0x20; p++)
        {
            snd->rd_page[m + p] = bj.rom_sound + 256 * p;
            snd->wr_page[m + p] = gal_ignored;
            snd->rd_page[m + 0x20 + p] = gal_zeros;
            snd->wr_page[m + 0x20 + p] = gal_ignored;
        }
        for (int p = 0; p < 0x20; p++)
        {
            /* 4000-47ff, mirrored to 5fff */
            snd->rd_page[m + 0x40 + p] = gal.ram + RAM_SOUND + 256 * (p & 7);
            snd->wr_page[m + 0x40 + p] = gal.ram + RAM_SOUND + 256 * (p & 7);
        }
    }
    snd->read = bj_snd_read;
    snd->write = bj_snd_write;
    snd->in = bj_snd_in;
    snd->out = bj_snd_out;
    return 1;
}

/* What the watchdog resets: the CPUs, the latch and the sound chips, not the
 * RAM. The game counts on it: after its power-on test it waits for an NMI it
 * has not enabled, until the watchdog restarts it. */
static void bj_soft_reset(void)
{
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    bj.latch = 0;
    bj.wd_enabled = 1; /* MAME: enabled at reset */
    bj.wd_count = 0;
    for (int k = 0; k < 2; k++)
    {
        z80_init(&gal.cpu[k]);
        z80_reset(&gal.cpu[k]);
    }
    gal_ay_reset();
}

static void bj_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    bj.spritectrl[0] = bj.spritectrl[1] = 0;
    bj.bg_image = 0;
    bj.nmi_on = 0;
    bj_soft_reset();
}

static void bj_run_slice(int q)
{
    if (q == 0)
        z80_set_nmi_line(&gal.cpu[1], 0); /* the vertical blank ends */
    z80_run(&gal.cpu[0], MAIN_CYCLES_PER_FRAME / 4);
    z80_run(&gal.cpu[1], SND_CYCLES_PER_FRAME / 4);
    if (q == 3)
    {
        if (bj.nmi_on)
            z80_set_nmi_line(&gal.cpu[0], 1);
        z80_set_nmi_line(&gal.cpu[1], 1);
        /* MAME: eight frames without a kick */
        if (bj.wd_enabled && ++bj.wd_count >= 8)
            bj_soft_reset();
    }
}

static void bj_sound(int16_t *out, int n)
{
    gal_ay_render(3, 1500000, 3, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (239 - y, x + 16),
 * so an s x s element at (x, y) has its upright corner at (240 - s - y, x + 16);
 * its flip in x is a flip in y of the upright picture and the other way round. */
#define UP_X(y, s) (240 - (s) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void bj_prepare_frame(void)
{
    const uint8_t *pal = gal.ram + RAM_PAL;
    for (int i = 0; i < 128; i++)
    {
        const unsigned v = pal[2 * i] | (pal[2 * i + 1] << 8);
        const unsigned r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
        bj.colors[i] = gal_color((uint16_t)(((r * 31 / 15) << 11) | ((g * 63 / 15) << 5) | (b * 31 / 15)));
    }

    /* MAME's draw_sprites: sprites 31 down to 8, the later on top */
    gal.active_sprites = 0;
    const uint8_t *spr = gal.ram + RAM_SPRITE;
    const int rev = bj.spritectrl[0] > bj.spritectrl[1];
    for (int sprite = 31; sprite > 31 - 24; sprite--)
    {
        const uint8_t *s = spr + 4 * sprite;
        const int index = sprite >> 1;
        const int large = index > bj.spritectrl[rev] && index <= bj.spritectrl[rev ^ 1];
        if (large && (sprite & 1))
            continue;
        const int size = large ? 32 : 16;
        const int x = s[3], y = (large ? 225 : 241) - s[2];
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        o->code = large ? (s[0] & 0x3f) : s[0];
        o->color = s[1] & 0x0f;
        /* flags: 1 flip x, 2 flip y (upright), 4 large */
        o->flags = (uint8_t)(((s[1] & 0x80) ? 1 : 0) | ((s[1] & 0x40) ? 2 : 0) | (large ? 4 : 0));
        o->x = (int16_t)UP_X(y, size);
        o->y = (int16_t)UP_Y(x);
    }
}

static void bj_render_band(const gal_band_t *b)
{
    /* the raster rectangle behind the band */
    const int ry0 = 239 - (b->x0 + b->w - 1), ry1 = 239 - b->x0;
    const int rx0 = b->y0 - VIEW_TOP, rx1 = b->y0 + b->h - 1 - VIEW_TOP;
    const int c0 = rx0 < 0 ? 0 : rx0, c1 = rx1 > 255 ? 255 : rx1;

    /* the background picture, or pen 0 */
    if (bj.bg_image & 0x10)
    {
        for (int r = ry0 >> 4; r <= ry1 >> 4; r++)
            for (int c = c0 >> 4; c <= c1 >> 4; c++)
            {
                const int i = ((r * 16 + c) | ((bj.bg_image & 0x0f) << 9)) & 0x0fff;
                const uint8_t code = bj.bgmap[i], attr = bj.bgmap[(i + 0x100) & 0x0fff];
                gal_draw_4bpp(b, UP_X(16 * r, 16), UP_Y(16 * c), 16, bj.bg + code * (BIG_BYTES / 4),
                              (attr & 0x80) != 0, 0, bj.colors + 8 * (attr & 0x0f), 1);
            }
    }
    else
        gal_fill_band(b, bj.colors[0]);

    /* the characters */
    const uint8_t *vram = gal.ram + RAM_VIDEO, *cram = gal.ram + RAM_COLOR;
    for (int r = ry0 >> 3; r <= ry1 >> 3; r++)
        for (int c = c0 >> 3; c <= c1 >> 3; c++)
        {
            const int i = r * 32 + c;
            const int code = vram[i] | ((cram[i] & 0x10) << 4);
            gal_draw_4bpp(b, UP_X(8 * r, 8), UP_Y(8 * c), 8, bj.fg + code * (FG_BYTES / 4), 0, 0,
                          bj.colors + 8 * (cram[i] & 0x0f), 0);
        }

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const uint16_t *colors = bj.colors + 8 * o->color;
        const int fx = o->flags & 1, fy = (o->flags & 2) != 0;
        if (!(o->flags & 4))
        {
            gal_draw_4bpp(b, o->x, o->y, 16, bj.spr + o->code * (BIG_BYTES / 4), fx, fy, colors, 0);
            continue;
        }
        /* a 32x32 sprite is the 16x16 ones 4n..4n+3: raster quadrants
         * (0,0), (1,0), (0,1), (1,1); upright, raster x runs down and raster
         * y to the left */
        for (int k = 0; k < 4; k++)
        {
            int qx = k & 1, qy = k >> 1; /* raster quadrant */
            if (fy)                      /* the raster's flip in x */
                qx ^= 1;
            if (fx) /* the raster's flip in y */
                qy ^= 1;
            gal_draw_4bpp(b, o->x + 16 * (1 - qy), o->y + 16 * qx, 16, bj.spr + (4 * o->code + k) * (BIG_BYTES / 4),
                          fx, fy, colors, 0);
        }
    }
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t bj_hiscore[] = {
    {0x905f, 1, 0x30, 0x30}, {0x907f, 1, 0x30, 0x30}, {0x909f, 1, 0x30, 0x30}, {0x90bf, 1, 0x30, 0x30},
    {0x90de, 3, 0x53, 0x24}, {0x90fe, 3, 0x2d, 0x24}, {0x911f, 1, 0x00, 0x00}, {0x913f, 1, 0x00, 0x00},
    {0x80e2, 4, 0x00, 0x00}, {0x8100, 0x96, 0x00, 0xff},
};

const gal_game_ops_t gal_bombjack_ops = {
    .init = bj_init,
    .reset = bj_reset,
    .run_slice = bj_run_slice,
    .prepare_frame = bj_prepare_frame,
    .render_band = bj_render_band,
    .sound = bj_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .large = 1,
    .nderived = 3,
    .derived_src = {2, 3, 5},
    .derived_size = {512 * FG_BYTES, 256 * BIG_BYTES, 256 * BIG_BYTES},
    .derive = bj_derive,
    .hiscore = bj_hiscore,
    .nhiscore = sizeof(bj_hiscore) / sizeof(bj_hiscore[0]),
};
