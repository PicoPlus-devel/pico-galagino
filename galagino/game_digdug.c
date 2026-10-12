/*
 * Dig Dug (Namco, 1982), from upstream Galagino's digdug.h and the Dig Dug
 * part of emulation.c.
 *
 * The Galaga board: three Z80s at 3.072 MHz sharing 8000-9fff (Galagino's RAM
 * buffer), each with its own ROM. The Namco 06xx/51xx/53xx chips are emulated
 * at a high level as Galagino does: a command other than 0x10 gives the main
 * CPU an NMI every 30 steps until it is replaced.
 *
 * The picture has two layers: a playfield of 8x8 tiles taken from a map ROM
 * (four playfields, chosen by the video latch) and 1-bit characters over it.
 *
 * As in game_galaga.c, a step is 40 cycles of each CPU, 1268 steps a frame,
 * and the interrupts follow MAME.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#define STEPS_PER_FRAME 1268
#define CYCLES_PER_STEP 40
#define NAMCO_NMI_DELAY 30 /* steps; 10 results in errors */

static struct
{
    const uint8_t *rom[3];
    const uint8_t *playfield; /* dd1.10b, four maps of 0x400 */
    uint16_t *pf_tiles;       /* [256][8] */
    uint16_t *chars;          /* [128][8] */
    uint32_t *sprites;        /* [256 + 4][16] */
    uint32_t *sprites_x;      /* the same flipped in x */
    uint16_t *cmap_tiles;     /* [64][4] */
    uint16_t *cmap_sprite;    /* [64][4] */
    uint16_t colors[32];      /* the palette, for the characters */
    int8_t *waves;            /* [16][32] */

    uint8_t namco_command;
    uint8_t namco_mode;
    uint8_t namco_nmi_counter;
    uint8_t namco_credit;
    uint16_t keymask_d[3];

    uint8_t irq_enable[3];
    uint8_t sub_cpu_reset;
    uint8_t video_latch;
} dd;

/* --- The 06xx/51xx (emulation.c namco_read_dd / namco_write_dd) ------------ */

static void namco_write_dd(uint16_t addr, uint8_t value)
{
    if (addr & 0x100)
    {
        dd.namco_command = value;
        if (value == 0xa1)
            dd.namco_mode = 1;
        if (value == 0xc1 || value == 0xe1)
            dd.namco_mode = 0;
        /* any command other than 0x10 triggers an NMI to the main CPU after ~50us */
        dd.namco_nmi_counter = (value != 0x10) ? NAMCO_NMI_DELAY : 0;
    }
}

static uint8_t namco_read_dd(uint16_t addr)
{
    if (addr & 0x100)
        return dd.namco_command;

    const uint16_t keymask = gal.buttons;
    dd.keymask_d[2] = dd.keymask_d[1];
    dd.keymask_d[1] = dd.keymask_d[0];
    dd.keymask_d[0] = keymask;

    /* rising edge, prolonged for two calls */
    const uint16_t keymask_p = (keymask ^ dd.keymask_d[2]) & keymask;

    if ((dd.keymask_d[0] & GAL_BTN_COIN) && !(dd.keymask_d[1] & GAL_BTN_COIN) && dd.namco_credit < 99)
        dd.namco_credit++;
    /* this decrease of credit actually triggers the game start */
    if ((dd.keymask_d[0] & GAL_BTN_START) && !(dd.keymask_d[1] & GAL_BTN_START) && dd.namco_credit > 0)
        dd.namco_credit--;

    if (dd.namco_command == 0x71)
    {
        if (dd.namco_mode)
        {
            if ((addr & 15) == 0)
            {
                uint8_t v = 0x00;
                if (keymask & GAL_BTN_COIN)
                    v |= 0x01;
                if (keymask & GAL_BTN_START)
                    v |= 0x10;
                return (uint8_t)~v;
            }
            if ((addr & 15) == 1)
            {
                uint8_t v = 0x00;
                if (keymask & GAL_BTN_UP)
                    v |= 0x01;
                if (keymask & GAL_BTN_DOWN)
                    v |= 0x04;
                if (keymask & GAL_BTN_LEFT)
                    v |= 0x08;
                if (keymask & GAL_BTN_RIGHT)
                    v |= 0x02;
                if (dd.keymask_d[1] & GAL_BTN_FIRE)
                    v |= 0x20;
                if (keymask_p & GAL_BTN_FIRE)
                    v |= 0x10;
                return (uint8_t)~v;
            }
            if ((addr & 15) == 2)
                return 0xff;
        }
        else
        {
            if ((addr & 15) == 0)
                return (uint8_t)(16 * (dd.namco_credit / 10) + dd.namco_credit % 10);
            if ((addr & 15) == 1)
            {
                uint8_t v = 0x00;
                /* first player controls */
                if (keymask & GAL_BTN_FIRE)
                    v |= 0x20;
                if (keymask_p & GAL_BTN_FIRE)
                    v |= 0x10;
                if (keymask & GAL_BTN_UP)
                    v |= 0x0f;
                else if (keymask & GAL_BTN_RIGHT)
                    v |= 0x0d;
                else if (keymask & GAL_BTN_DOWN)
                    v |= 0x0b;
                else if (keymask & GAL_BTN_LEFT)
                    v |= 0x09;
                else
                    v |= 0x07; /* nothing */
                return (uint8_t)~v;
            }
            if ((addr & 15) == 2)
                return 0xf8; /* no second player controls */
        }
    }
    else if (dd.namco_command == 0xd2)
    {
        if ((addr & 15) == 0)
            return (uint8_t)~DIGDUG_DIP_A; /* DSW0 */
        if ((addr & 15) == 1)
            return (uint8_t)~DIGDUG_DIP_B; /* DSW1 */
    }
    else if (dd.namco_command == 0xb1)
    {
        return ((addr & 15) <= 2) ? 0x00 : 0xff;
    }
    return 0xff;
}

/* --- Memory map ------------------------------------------------------------ */

static uint8_t GAL_HOT(dd_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if ((addr & 0xfe00) == 0x7000) /* 7100 namco_06xx ctrl, 7000 data */
        return namco_read_dd(addr & 0x1ff);
    if ((addr & 0xffc0) == 0xb800) /* EAROM */
        return 0x00;
    return 0xff;
}

static void GAL_HOT(dd_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if ((addr & 0xff00) == 0x8300) /* RAM, through here only for this page */
    {
        /* 'U' (46) of "1UP" in the top left corner: the game has booted */
        if (addr == 0x8000 + 985 && (value & 0x7f) == 46)
            gal.game_started = 1;
        gal.ram[addr - 0x8000] = value;
        return;
    }

    if ((addr & 0xffe0) == 0x6800)
    {
        gal.soundregs[addr - 0x6800] = value & 0x0f;
        return;
    }

    if ((addr & 0xfff8) == 0x6820)
    {
        if ((addr & 0x0c) == 0x00)
        {
            const int bit = addr & 3;
            if (bit < 2)
            {
                dd.irq_enable[bit] = value & 1;
                if (!(value & 1))
                    z80_set_irq_line(&gal.cpu[bit], 0);
            }
            else if (bit == 2)
                dd.irq_enable[2] = value & 1;
            else
            {
                dd.sub_cpu_reset = !(value & 1);
                if (dd.sub_cpu_reset)
                {
                    /* this also resets the 51xx */
                    dd.namco_command = 0x00;
                    dd.namco_mode = 0;
                    dd.namco_nmi_counter = 0;
                    z80_reset(&gal.cpu[1]);
                    z80_reset(&gal.cpu[2]);
                }
            }
        }
        return;
    }

    if ((addr & 0xfe00) == 0x7000)
    {
        namco_write_dd(addr & 0x1ff, value);
        return;
    }

    if ((addr & 0xfff8) == 0xa000) /* video latch */
    {
        if (value & 1)
            dd.video_latch |= (uint8_t)(1 << (addr & 7));
        else
            dd.video_latch &= (uint8_t)~(1 << (addr & 7));
    }
}

static uint8_t dd_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void dd_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* tileconv.py parse_chr_dd: 1 bit per pixel, drawn with pen 3 */
static void dd_conv_chars(const uint8_t *rom, uint16_t *out)
{
    for (int chr = 0; chr < 128; chr++)
    {
        const uint8_t *data = rom + 8 * chr;
        for (int y = 0; y < 8; y++)
        {
            uint16_t val = 0;
            for (int x = 0; x < 8; x++)
                val = (uint16_t)((val >> 2) | (((data[7 - x] & (1 << y)) ? 3 : 0) << 14));
            *out++ = val;
        }
    }
}

static int dd_init(uint8_t *const *regions)
{
    for (int c = 0; c < 3; c++)
        dd.rom[c] = regions[c];
    dd.playfield = regions[4];
    dd.pf_tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    dd.chars = (uint16_t *)gal_alloc(128 * 8 * 2);
    dd.sprites = (uint32_t *)gal_alloc((256 + 4) * 16 * 4);
    dd.sprites_x = (uint32_t *)gal_alloc((256 + 4) * 16 * 4);
    dd.cmap_tiles = (uint16_t *)gal_alloc(64 * 4 * 2);
    dd.cmap_sprite = (uint16_t *)gal_alloc(64 * 4 * 2);
    dd.waves = (int8_t *)gal_alloc(16 * 32);
    if (!dd.pf_tiles || !dd.chars || !dd.sprites || !dd.sprites_x || !dd.cmap_tiles || !dd.cmap_sprite || !dd.waves)
        return 0;

    dd_conv_chars(regions[3], dd.chars);
    gal_conv_chars_namco(regions[5], 256, dd.pf_tiles);
    memset(dd.sprites + 256 * 16, 0, 4 * 16 * 4);
    memset(dd.sprites_x + 256 * 16, 0, 4 * 16 * 4);
    gal_conv_sprites_namco(regions[6], 256, 0, dd.sprites, dd.sprites_x);

    /* cmapconv.py: the playfield uses palette entries 0-15, sprites 16-31 */
    for (int i = 0; i < 32; i++)
        dd.colors[i] = gal_color(gal_prom_bgr233(regions[7][i]));
    for (int i = 0; i < 64 * 4; i++)
    {
        dd.cmap_sprite[i] = dd.colors[16 + (regions[8][i] & 0x0f)];
        dd.cmap_tiles[i] = dd.colors[regions[9][i] & 0x0f];
    }
    gal_conv_waves(regions[10], 16, dd.waves);

    for (int c = 0; c < 3; c++)
    {
        z80_t *cpu = &gal.cpu[c];
        const int rom_pages = (c == 0) ? 0x40 : (c == 1) ? 0x20 : 0x10;
        for (int p = 0; p < rom_pages; p++)
            cpu->rd_page[p] = dd.rom[c] + p * 256;
        for (int p = 0; p < 0x20; p++)
        {
            uint8_t *ram = gal.ram + p * 256;
            cpu->rd_page[0x80 + p] = ram;
            if (p != 0x03) /* 8300-83ff is watched for the end of the boot, see dd_write */
                cpu->wr_page[0x80 + p] = ram;
        }
        cpu->read = dd_read;
        cpu->write = dd_write;
        cpu->in = dd_in;
        cpu->out = dd_out;
    }
    return 1;
}

static void dd_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    dd.namco_command = 0;
    dd.namco_mode = 0;
    dd.namco_nmi_counter = 0;
    dd.namco_credit = 0;
    memset(dd.keymask_d, 0, sizeof(dd.keymask_d));
    memset(dd.irq_enable, 0, sizeof(dd.irq_enable));
    dd.sub_cpu_reset = 1;
    dd.video_latch = 0;
    gal.game_started = 0;
    for (int c = 0; c < 3; c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    gal_wsg_reset();
}

static void GAL_HOT(dd_run_slice)(int q)
{
    const int first = q * STEPS_PER_FRAME / 4, last = (q + 1) * STEPS_PER_FRAME / 4;
    for (int i = first; i < last; i++)
    {
        z80_run(&gal.cpu[0], CYCLES_PER_STEP);
        if (!dd.sub_cpu_reset)
        {
            z80_run(&gal.cpu[1], CYCLES_PER_STEP);
            z80_run(&gal.cpu[2], CYCLES_PER_STEP);
        }

        /* the 06xx NMI of the main CPU */
        if (dd.namco_nmi_counter && !--dd.namco_nmi_counter)
        {
            z80_set_nmi_line(&gal.cpu[0], 1);
            z80_set_nmi_line(&gal.cpu[0], 0);
            dd.namco_nmi_counter = NAMCO_NMI_DELAY;
        }
    }

    /* CPU3 NMI twice a frame */
    if ((q == 1 || q == 3) && !dd.sub_cpu_reset && !dd.irq_enable[2])
    {
        z80_set_nmi_line(&gal.cpu[2], 1);
        z80_set_nmi_line(&gal.cpu[2], 0);
    }

    if (q == 3)
    {
        /* VBLANK */
        if (dd.irq_enable[0])
            z80_set_irq_line(&gal.cpu[0], 1);
        if (!dd.sub_cpu_reset && dd.irq_enable[1])
            z80_set_irq_line(&gal.cpu[1], 1);
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(dd_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 64 && gal.active_sprites < GAL_MAX_SPRITES - 4; idx++)
    {
        const uint8_t *base = gal.ram + 2 * idx;
        if (base[0x1b80 + 1] & 2) /* not visible */
            continue;

        gal_sprite_t spr;
        spr.code = base[0x0b80];
        spr.color = base[0x0b80 + 1] & 0x3f;
        spr.flags = base[0x1b80];
        /* the position on the upright screen */
        spr.x = base[0x1380] - 16;
        spr.y = base[0x1380 + 1] - 40 + 1;
        if (spr.y < 16)
            spr.y += 256;
        if (base[0x0b80] & 0x80) /* large sprite */
        {
            spr.flags |= 0x0c;
            spr.code = (uint8_t)((spr.code & 0xc0) | ((spr.code & ~0xc0) << 2));
        }

        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            /* for horizontally doubled sprites, this one becomes the code + 2 part */
            if (spr.flags & 0x08)
                gal.sprite[gal.active_sprites].code += 2;
            gal.active_sprites++;
        }

        /* horizontally doubled: a copy right of it */
        if ((spr.flags & 0x08) && spr.y > -16 && spr.y < GAL_H && spr.x + 16 > -16 && spr.x + 16 < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            gal.sprite[gal.active_sprites].x += 16;
            gal.active_sprites++;
            /* flipped in x: the halves swap */
            if (spr.flags & 2)
            {
                const uint8_t tmp = gal.sprite[gal.active_sprites - 1].code;
                gal.sprite[gal.active_sprites - 1].code = gal.sprite[gal.active_sprites - 2].code;
                gal.sprite[gal.active_sprites - 2].code = tmp;
            }
        }

        /* vertically doubled: a copy below it */
        if ((spr.flags & 0x04) && spr.y + 16 > -16 && spr.y + 16 < GAL_H && spr.x > -16 && spr.x < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            gal.sprite[gal.active_sprites].code += 3;
            gal.sprite[gal.active_sprites].y += 16;
            gal.active_sprites++;
        }

        /* doubled both ways: a copy right of and below it */
        if ((spr.flags & 0x0c) == 0x0c && spr.y + 16 > -16 && spr.y + 16 < GAL_H && spr.x + 16 > -16 &&
            spr.x + 16 < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            gal.sprite[gal.active_sprites].code += 1;
            gal.sprite[gal.active_sprites].x += 16;
            gal.sprite[gal.active_sprites].y += 16;
            gal.active_sprites++;
            /* flipped in x: the bottom halves swap */
            if (spr.flags & 2)
            {
                const uint8_t tmp = gal.sprite[gal.active_sprites - 1].code;
                gal.sprite[gal.active_sprites - 1].code = gal.sprite[gal.active_sprites - 2].code;
                gal.sprite[gal.active_sprites - 2].code = tmp;
            }
        }
    }
}

/* One 8x8 cell: the playfield tile, opaque unless the latch switches the
 * playfield off, and the character over it. */
static inline void dd_draw_cell(const gal_band_t *b, int row, int col)
{
    const unsigned addr = tileaddr[row][col];

    /* the playfield ROM holds four playfields: 0 normal, 1 cross pattern,
     * 2 funny logo, 3 monocolour */
    uint8_t chr = dd.playfield[addr + (dd.video_latch & 3) * 0x400];
    const uint16_t *pf = dd.pf_tiles + 8 * chr;
    /* the colour PROM holds 4 * 16 colour groups */
    const uint16_t *pf_colors = dd.cmap_tiles + 4 * ((dd.video_latch & 0x30) + (chr >> 4));

    chr = gal.ram[addr];
    const uint16_t *fg = dd.chars + 8 * (chr & 0x7f);
    uint16_t fgcol = dd.colors[((chr >> 4) & 0x0e) | ((chr >> 3) & 2)];
    if (dd.video_latch & 4) /* never used in Dig Dug */
        fgcol = dd.colors[chr & 15];
    const int pf_on = !(dd.video_latch & 8);

    const int px = 8 * col, py = 8 * row;
    int ys = py, ye = py + 8, xs = px, xe = px + 8;
    if (ys < b->y0)
        ys = b->y0;
    if (ye > b->y0 + b->h)
        ye = b->y0 + b->h;
    if (xs < b->x0)
        xs = b->x0;
    if (xe > b->x0 + b->w)
        xe = b->x0 + b->w;
    uint16_t *dst = b->buf + (ys - b->y0) * b->w + (xs - b->x0);
    for (int y = ys; y < ye; y++, dst += b->w)
    {
        uint32_t bg_pix = (uint32_t)pf[y - py] >> (2 * (xs - px));
        uint32_t fg_pix = (uint32_t)fg[y - py] >> (2 * (xs - px));
        for (int c = 0; c < xe - xs; c++, bg_pix >>= 2, fg_pix >>= 2)
        {
            if (pf_on)
                dst[c] = pf_colors[bg_pix & 3];
            if (fg_pix & 3)
                dst[c] = fgcol;
        }
    }
}

static void GAL_HOT(dd_render_band)(const gal_band_t *b)
{
    gal_fill_band(b, 0);

    for (int row = b->y0 / 8; row <= (b->y0 + b->h - 1) / 8; row++)
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
            dd_draw_cell(b, row, col);

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint16_t *colors = dd.cmap_sprite + 4 * (spr->color & 63);
        if (colors[0] != 0)
            continue; /* not a valid colormap entry */
        const uint32_t *rows = ((spr->flags & 2) ? dd.sprites_x : dd.sprites) + 16 * spr->code;
        gal_draw_sprite(b, spr->x, spr->y, rows, spr->flags & 1, colors);
    }
}

static void GAL_HOT(dd_sound)(int16_t *out, int n)
{
    gal_wsg_render(dd.waves, 0x0f, out, n);
}


/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t dd_hiscore[] = {
    {0x89a0, 0x25, 0x01, 0x01},
};

const gal_game_ops_t gal_digdug_ops = {
    .init = dd_init,
    .reset = dd_reset,
    .run_slice = dd_run_slice,
    .prepare_frame = dd_prepare_frame,
    .render_band = dd_render_band,
    .sound = dd_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .hiscore = dd_hiscore,
    .nhiscore = sizeof(dd_hiscore) / sizeof(dd_hiscore[0]),
};

#ifdef GAL_CONV_CHECK
int gal_tables_digdug(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"digdug_rom_cpu1", dd.rom[0], 0x4000};
    t[n++] = (gal_table_t){"digdug_rom_cpu2", dd.rom[1], 0x2000};
    t[n++] = (gal_table_t){"digdug_rom_cpu3", dd.rom[2], 0x1000};
    t[n++] = (gal_table_t){"digdug_playfield", dd.playfield, 0x1000};
    t[n++] = (gal_table_t){"dd1_9", dd.chars, 128 * 8 * 2};
    t[n++] = (gal_table_t){"dd1_11", dd.pf_tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"digdug_sprites", dd.sprites, 256 * 16 * 4};
    t[n++] = (gal_table_t){"digdug_sprites_x", dd.sprites_x, 256 * 16 * 4};
    t[n++] = (gal_table_t){"digdug_colormap_tiles", dd.cmap_tiles, 64 * 4 * 2};
    t[n++] = (gal_table_t){"digdug_colormap_sprites", dd.cmap_sprite, 64 * 4 * 2};
    t[n++] = (gal_table_t){"digdug_colormaps", dd.colors, sizeof(dd.colors)};
    t[n++] = (gal_table_t){"digdug_wavetable", dd.waves, 16 * 32};
    return n;
}
#endif
