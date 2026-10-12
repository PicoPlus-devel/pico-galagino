/*
 * Galaga (Namco, 1981), from upstream Galagino's galaga.h.
 *
 * Three Z80s at 3.072 MHz share 8000-9fff (Galagino's RAM buffer). Each has
 * its own ROM at 0000. The Namco 06xx/51xx custom chips (coins, credits,
 * controls) are emulated at a high level as Galagino does; the 54xx explosion
 * is Galagino's recorded sample.
 *
 * Galagino runs the CPUs interleaved, four instructions each, 1250 times a
 * frame, and counts the 06xx timing in those steps. Here a step is 40 cycles
 * of each CPU, 1268 steps a 60.6 Hz frame (50688 cycles).
 *
 * Interrupts follow MAME's galaga.cpp: the VBLANK IRQs of CPU1 and CPU2 are
 * asserted while their latch bits are set and cleared by writing 0 to them;
 * CPU3 gets an NMI at lines 64 and 192 while its latch bit is 0.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#include "data/galaga_sample_boom.h"
#include "data/galaga_starseed.h"

#define STEPS_PER_FRAME 1268
#define CYCLES_PER_STEP 40
#define NUM_STARS 63

static struct
{
    const uint8_t *rom[3];
    uint16_t *tiles;       /* [256][8] */
    uint32_t *sprites;     /* [128 + 4][16], 4 blank spares for the doubled codes */
    uint32_t *sprites_x;   /* the same flipped in x */
    uint16_t *cmap_tiles;  /* [64][4] */
    uint16_t *cmap_sprite; /* [64][4] */
    int8_t *waves;         /* [8][32] */
    uint16_t star_color[4][NUM_STARS];

    /* the 06xx/51xx */
    uint8_t credit;
    uint8_t credit_mode;
    int namco_cnt;
    int namco_busy;
    uint8_t cs_ctrl;
    int nmi_cnt;
    int coincred_mode;
    uint16_t prev_mask;
    uint8_t fire_timer;

    uint8_t irq_enable[3];
    uint8_t sub_cpu_reset;
    uint8_t starcontrol;
    uint8_t stars_scroll_y;

    gal_voice_t boom;
} gg;

/* --- Memory map ------------------------------------------------------------ */

static uint8_t GAL_HOT(gg_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    /* the DIP switches, one bit of each per address */
    if ((addr & 0xfff8) == 0x6800)
    {
        const uint8_t dip_a = (GALAGA_DIPA & (0x80 >> (addr & 7))) ? 0 : 1;
        const uint8_t dip_b = (GALAGA_DIPB & (0x80 >> (addr & 7))) ? 0 : 2;
        return dip_a + dip_b;
    }

    /* 7100 namco_06xx ctrl, 7000 data */
    if ((addr & 0xfe00) == 0x7000)
    {
        if (addr & 0x100)
            return gg.namco_busy ? 0x00 : 0x10; /* cmd ack (game_ctrl.s L1024) */

        uint8_t retval = 0x00;
        if (gg.cs_ctrl & 1) /* bit 0: 51xx selected */
        {
            if (!gg.credit_mode)
            {
                /* galaga doesn't seem to use the button mappings in byte 1 and 2
                 * in non-credit mode, so that is not implemented */
                if (gg.namco_cnt > 2)
                    return 0xff;
                retval = 0xff;
            }
            else
            {
                /* byte 0 is credit in BCD, byte 1 and 2: ...FLURD */
                uint8_t mapb1[3] = {(uint8_t)(16 * (gg.credit / 10) + gg.credit % 10), 0xff, 0xff};
                const uint16_t keymask = gal.buttons;

                /* report directions directly */
                if (keymask & GAL_BTN_LEFT)
                    mapb1[1] &= ~0x08;
                if (keymask & GAL_BTN_UP)
                    mapb1[1] &= ~0x04;
                if (keymask & GAL_BTN_RIGHT)
                    mapb1[1] &= ~0x02;
                if (keymask & GAL_BTN_DOWN)
                    mapb1[1] &= ~0x01;

                /* report fire only when it was pressed */
                if ((keymask & GAL_BTN_FIRE) && !(gg.prev_mask & GAL_BTN_FIRE))
                {
                    mapb1[1] &= ~0x10;
                    gg.fire_timer = 1; /* 0 is too short for score enter, 5 is too long */
                }
                else if (gg.fire_timer)
                {
                    mapb1[1] &= ~0x10;
                    gg.fire_timer--;
                }

                if ((keymask & GAL_BTN_START) && !(gg.prev_mask & GAL_BTN_START) && gg.credit)
                    gg.credit -= 1;
                if ((keymask & GAL_BTN_START2) && !(gg.prev_mask & GAL_BTN_START2) && gg.credit >= 2)
                    gg.credit -= 2;
                if ((keymask & GAL_BTN_COIN) && !(gg.prev_mask & GAL_BTN_COIN) && gg.credit < 99)
                    gg.credit += 1;

                if (gg.namco_cnt > 2)
                    return 0xff;
                retval = mapb1[gg.namco_cnt];
                gg.prev_mask = keymask;
            }
            gg.namco_cnt++;
        }
        return retval;
    }
    return 0xff;
}

static void GAL_HOT(gg_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr < 0x4000)
        return; /* ROM */

    if ((addr & 0xff00) == 0x8200) /* RAM, through here only for this page */
    {
        gal.ram[addr - 0x8000] = value;
        if (value == 0 && addr == 0x8210) /* gg1-4.s L1725 */
            gal.game_started = 1;
        return;
    }

    /* namco 06xx */
    if ((addr & 0xf800) == 0x7000)
    {
        if (addr & 0x100) /* 7100, see task_man.s L450 */
        {
            gg.namco_cnt = 0;
            gg.cs_ctrl = value;
            gg.namco_busy = 5000; /* this delay is important for proper startup */
            if (value == 0xa8 && gal.game_started)
                gal_voice_start(&gg.boom, (const int8_t *)galaga_sample_boom, sizeof(galaga_sample_boom), 12000, 0);
        }
        else if (gg.cs_ctrl & 1) /* 7000 */
        {
            if (gg.coincred_mode)
            {
                gg.coincred_mode--;
                return;
            }
            switch (value)
            {
            case 1: /* coin and credit mode for both players follow (four bytes) */
                gg.coincred_mode = 4;
                break;
            case 2:
                gg.credit_mode = 1;
                break;
            case 3: /* disable joystick remapping */
            case 4: /* enable joystick remapping */
                break;
            case 5:
                gg.credit_mode = 0;
                break;
            }
            gg.namco_cnt++;
        }
        return;
    }

    if ((addr & 0xffe0) == 0x6800)
    {
        gal.soundregs[addr - 0x6800] = value & 0x0f;
        return;
    }

    if ((addr & 0xfffc) == 0x6820)
    {
        const int bit = addr & 3;
        if (bit < 2)
        {
            gg.irq_enable[bit] = value & 1;
            if (!(value & 1))
                z80_set_irq_line(&gal.cpu[bit], 0);
        }
        else if (bit == 2)
            gg.irq_enable[2] = value & 1;
        else
        {
            gg.sub_cpu_reset = !(value & 1);
            gg.credit_mode = 0; /* this also resets the 51xx */
            if (gg.sub_cpu_reset)
            {
                z80_reset(&gal.cpu[1]);
                z80_reset(&gal.cpu[2]);
            }
        }
        return;
    }

    if ((addr & 0xfff0) == 0xa000)
    {
        if (value & 1)
            gg.starcontrol |= (1 << (addr & 7));
        else
            gg.starcontrol &= ~(1 << (addr & 7));
    }
}

static uint8_t gg_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void gg_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* romconv.py -p: skip the tile RAM test, run the RAM test once instead of 30
 * times and skip the ROM test, so the game boots in about a second. Applied
 * only where the ROM has the expected bytes. */
static void gg_patch_boot(uint8_t *rom)
{
    static const struct
    {
        uint16_t addr;
        uint8_t from, to;
    } patches[] = {
        {0x3382, 0x06, 0xc3}, /* jp 0x3435 over the tile RAM test */
        {0x3383, 0x0a, 0x35},
        {0x3384, 0xd9, 0x34},
        {0x348a, 0x1e, 0x01}, /* one RAM test round */
        {0x352b, 0xe5, 0xc9}, /* ret: no ROM test */
    };
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); i++)
        if (rom[patches[i].addr] != patches[i].from)
            return;
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); i++)
        rom[patches[i].addr] = patches[i].to;
}

static int gg_init(uint8_t *const *regions)
{
    gg_patch_boot(regions[0]);
    gg.rom[0] = regions[0];
    gg.rom[1] = regions[1];
    gg.rom[2] = regions[2];
    gg.tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    gg.sprites = (uint32_t *)gal_alloc((128 + 4) * 16 * 4);
    gg.sprites_x = (uint32_t *)gal_alloc((128 + 4) * 16 * 4);
    gg.cmap_tiles = (uint16_t *)gal_alloc(64 * 4 * 2);
    gg.cmap_sprite = (uint16_t *)gal_alloc(64 * 4 * 2);
    gg.waves = (int8_t *)gal_alloc(8 * 32);
    if (!gg.tiles || !gg.sprites || !gg.sprites_x || !gg.cmap_tiles || !gg.cmap_sprite || !gg.waves)
        return 0;

    gal_conv_chars_namco(regions[3], 256, gg.tiles);
    memset(gg.sprites + 128 * 16, 0, 4 * 16 * 4);
    memset(gg.sprites_x + 128 * 16, 0, 4 * 16 * 4);
    gal_conv_sprites_namco(regions[4], 128, 0, gg.sprites, gg.sprites_x);

    /* cmapconv.py: sprites use palette entries 0-15, tiles 16-31 */
    uint16_t palette[32];
    for (int i = 0; i < 32; i++)
        palette[i] = gal_color(gal_prom_bgr233(regions[5][i]));
    for (int i = 0; i < 64 * 4; i++)
    {
        gg.cmap_tiles[i] = palette[16 + (regions[6][i] & 0x0f)];
        gg.cmap_sprite[i] = palette[regions[7][i] & 0x0f];
    }
    gal_conv_waves(regions[8], 8, gg.waves);

    for (int set = 0; set < 4; set++)
        for (int s = 0; s < NUM_STARS; s++)
        {
            const uint16_t c = galaga_star_set[set][s].col;
            gg.star_color[set][s] = gal_color((uint16_t)((c >> 8) | (c << 8)));
        }

    for (int c = 0; c < 3; c++)
    {
        z80_t *cpu = &gal.cpu[c];
        const int rom_pages = (c == 0) ? 0x40 : 0x10;
        for (int p = 0; p < rom_pages; p++)
            cpu->rd_page[p] = gg.rom[c] + p * 256;
        for (int p = 0; p < 0x20; p++)
        {
            uint8_t *ram = gal.ram + p * 256;
            cpu->rd_page[0x80 + p] = ram;
            if (p != 0x02) /* 8200-82ff is watched for the end of the boot, see gg_write */
                cpu->wr_page[0x80 + p] = ram;
        }
        cpu->read = gg_read;
        cpu->write = gg_write;
        cpu->in = gg_in;
        cpu->out = gg_out;
    }
    return 1;
}

static void gg_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    gg.credit = 0;
    gg.credit_mode = 0;
    gg.namco_cnt = 0;
    gg.namco_busy = 0;
    gg.cs_ctrl = 0;
    gg.nmi_cnt = 0;
    gg.coincred_mode = 0;
    gg.prev_mask = 0;
    gg.fire_timer = 0;
    memset(gg.irq_enable, 0, sizeof(gg.irq_enable));
    gg.sub_cpu_reset = 1;
    gg.starcontrol = 0;
    gg.stars_scroll_y = 0;
    gg.boom.data = NULL;
    gal.game_started = 0;
    for (int c = 0; c < 3; c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    gal_wsg_reset();
}

static void GAL_HOT(gg_run_slice)(int q)
{
    const int first = q * STEPS_PER_FRAME / 4, last = (q + 1) * STEPS_PER_FRAME / 4;
    for (int i = first; i < last; i++)
    {
        z80_run(&gal.cpu[0], CYCLES_PER_STEP);
        if (!gg.sub_cpu_reset)
        {
            z80_run(&gal.cpu[1], CYCLES_PER_STEP);
            z80_run(&gal.cpu[2], CYCLES_PER_STEP);
        }

        if (gg.namco_busy)
            gg.namco_busy--;

        /* the 06xx NMI of CPU1: ctrl[7:5] * 64 * 330 ns */
        if ((gg.cs_ctrl & 0xe0) != 0)
        {
            if (gg.nmi_cnt < (gg.cs_ctrl >> 5) * 64)
                gg.nmi_cnt++;
            else
            {
                z80_set_nmi_line(&gal.cpu[0], 1);
                z80_set_nmi_line(&gal.cpu[0], 0);
                gg.nmi_cnt = 0;
            }
        }

        /* CPU3 NMI at about line 64 and line 192 */
        if (!gg.sub_cpu_reset && !gg.irq_enable[2] && (i == STEPS_PER_FRAME / 4 || i == 3 * STEPS_PER_FRAME / 4))
        {
            z80_set_nmi_line(&gal.cpu[2], 1);
            z80_set_nmi_line(&gal.cpu[2], 0);
        }
    }

    if (q == 3)
    {
        /* VBLANK */
        if (gg.irq_enable[0])
            z80_set_irq_line(&gal.cpu[0], 1);
        if (!gg.sub_cpu_reset && gg.irq_enable[1])
            z80_set_irq_line(&gal.cpu[1], 1);

        static const int8_t speeds[8] = {-1, -2, -3, 0, 3, 2, 1, 0};
        gg.stars_scroll_y += speeds[gg.starcontrol & 7];
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(gg_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 64 && gal.active_sprites < GAL_MAX_SPRITES - 4; idx++)
    {
        const uint8_t *base = gal.ram + 2 * (63 - idx);
        if (base[0x1b80 + 1] & 2) /* not visible */
            continue;

        gal_sprite_t spr;
        spr.code = base[0x0b80];
        spr.color = base[0x0b80 + 1];
        spr.flags = base[0x1b80];
        spr.x = base[0x1380] - 16;
        spr.y = base[0x1380 + 1] + 0x100 * (base[0x1b80 + 1] & 1) - 40;

        if (spr.code < 128 && spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            /* for horizontally doubled sprites, this one becomes the code + 2 part */
            if (spr.flags & 0x08)
                gal.sprite[gal.active_sprites].code += 2;
            gal.active_sprites++;
        }

        /* horizontally doubled: a copy right of it */
        if ((spr.flags & 0x08) && spr.y > -16 && spr.y < GAL_H && spr.x + 16 >= -16 && spr.x + 16 < GAL_W)
        {
            gal.sprite[gal.active_sprites] = spr;
            gal.sprite[gal.active_sprites].x += 16;
            gal.active_sprites++;
        }

        /* vertically doubled: a copy below it (these don't seem to happen in Galaga) */
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
        }
    }
}

static void GAL_HOT(render_stars_set)(const gal_band_t *b, int set)
{
    const uint16_t *colors = gg.star_color[set];
    for (int i = 0; i < NUM_STARS; i++)
    {
        const struct galaga_star *s = &galaga_star_set[set][i];
        const int x = (244 - s->x) & 0xff;
        const int y = ((s->y + gg.stars_scroll_y) & 0xff) + 16;
        if (x < GAL_W && x >= b->x0 && x < b->x0 + b->w && y >= b->y0 && y < b->y0 + b->h)
            b->buf[(y - b->y0) * b->w + (x - b->x0)] = colors[i];
    }
}

static void GAL_HOT(gg_render_band)(const gal_band_t *b)
{
    gal_fill_band(b, 0);

    if (gg.starcontrol & 0x20)
    {
        /* two sets of stars controlled by these bits */
        render_stars_set(b, (gg.starcontrol & 0x08) ? 1 : 0);
        render_stars_set(b, (gg.starcontrol & 0x10) ? 3 : 2);
    }

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint16_t *colors = gg.cmap_sprite + 4 * (spr->color & 63);
        if (colors[0] != 0)
            continue; /* not a valid colormap entry */
        const uint32_t *rows = ((spr->flags & 2) ? gg.sprites_x : gg.sprites) + 16 * spr->code;
        gal_draw_sprite(b, spr->x, spr->y, rows, spr->flags & 1, colors);
    }

    /* tiles over the sprites; 0x24 is a blank tile */
    for (int row = b->y0 / 8; row <= (b->y0 + b->h - 1) / 8; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[row][col];
            if (gal.ram[addr] == 0x24)
                continue;
            gal_draw_tile(b, 8 * col, 8 * row, gg.tiles + 8 * gal.ram[addr],
                          gg.cmap_tiles + 4 * (gal.ram[0x400 + addr] & 63));
        }
    }
}

static void GAL_HOT(gg_sound)(int16_t *out, int n)
{
    gal_wsg_render(gg.waves, 0x07, out, n);
    gal_voice_mix(&gg.boom, out, n);
}


/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t gg_hiscore[] = {
    {0x8a20, 0x2d, 0x00, 0x18}, /* the best five scores and initials */
    {0x83ed, 6, 0x00, 0x24},    /* video RAM: the high score shown */
};

const gal_game_ops_t gal_galaga_ops = {
    .init = gg_init,
    .reset = gg_reset,
    .run_slice = gg_run_slice,
    .prepare_frame = gg_prepare_frame,
    .render_band = gg_render_band,
    .sound = gg_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .hiscore = gg_hiscore,
    .nhiscore = sizeof(gg_hiscore) / sizeof(gg_hiscore[0]),
};

#ifdef GAL_CONV_CHECK
int gal_tables_galaga(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"galaga_rom_cpu1", gg.rom[0], 0x4000};
    t[n++] = (gal_table_t){"galaga_rom_cpu2", gg.rom[1], 0x1000};
    t[n++] = (gal_table_t){"galaga_rom_cpu3", gg.rom[2], 0x1000};
    t[n++] = (gal_table_t){"gg1_9_4l", gg.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"galaga_sprites", gg.sprites, 128 * 16 * 4};
    t[n++] = (gal_table_t){"galaga_sprites_x", gg.sprites_x, 128 * 16 * 4};
    t[n++] = (gal_table_t){"galaga_colormap_tiles", gg.cmap_tiles, 64 * 4 * 2};
    t[n++] = (gal_table_t){"galaga_colormap_sprites", gg.cmap_sprite, 64 * 4 * 2};
    t[n++] = (gal_table_t){"galaga_wavetable", gg.waves, 8 * 32};
    return n;
}
#endif
