/*
 * Namco's Xevious (1982), after MAME's namco/xevious.cpp and galaga.cpp.
 *
 * The Galaga board: three Z80s at 3.072 MHz with one memory map, each with
 * its own ROM, and Namco's custom chips behind the 06XX: the 51XX (coins and
 * controls), the 50XX (only a protection check at start-up) and the 54XX
 * (explosion sounds, not emulated). The 06XX gives the main CPU an NMI at the
 * rate in bits 5-7 of its control register while a chip is selected, as in
 * MAME; the chips themselves follow GalaginoPlus's high-level model. The
 * background map is read by the CPUs through ROMs at f000 (MAME's
 * xevious_bb_r). Three-voice Namco WSG.
 *
 * 288x224 (MAME ROT90: the full upright 224x288 picture). A scrolled
 * background of 8x8 tiles of two bits and a scrolled foreground of 1-bit
 * characters, both 64x32 tiles, with 64 sprites of 16x16 to 32x32 and three
 * bits per pixel between them. Large: the sprites are read from PSRAM or the
 * flash cache.
 *
 * A fires the zapper, B drops the blaster's bombs.
 */
#include "gal_internal.h"

#define STEPS_PER_FRAME 1267 /* 3.072 MHz at 60.6 Hz, 40 cycles a step */
#define CYCLES_PER_STEP 40
#define NSPRITES 320
#define SPR_BYTES 128 /* 16x16, four bits (three used) */

/* The RAM, in the arena */
#define RAM_WORK 0x0000    /* 7800-7fff */
#define RAM_SR1 0x0800     /* 8000-87ff, the sprites' position at 0f80 */
#define RAM_SR2 0x1000     /* 9000-97ff, their size and flips */
#define RAM_SR3 0x1800     /* a000-a7ff, their code and colour */
#define RAM_FGCOLOR 0x2000 /* b000-b7ff */
#define RAM_BGCOLOR 0x2800 /* b800-bfff */
#define RAM_FGVIDEO 0x3000 /* c000-c7ff */
#define RAM_BGVIDEO 0x3800 /* c800-cfff */
#define RAM_SIZE 0x4000

static struct
{
    uint8_t *ram, *rom[3];
    const uint8_t *planet; /* the background map ROMs, 2a, 2b and 2c */
    uint16_t *fg;          /* [512][8], upright, pens 0 and 1 */
    uint16_t *bg;          /* [512][8], upright */
    const uint32_t *sprites;
    uint16_t *bg_colors;   /* [128][4] */
    uint16_t *spr_colors;  /* [64][8] */
    uint8_t *spr_mask;     /* [64] */
    uint16_t *fg_colors;   /* [64][4]: pen 1 the colour */
    int8_t *waves;

    uint8_t irq_enable[3], sub_cpu_reset;
    uint16_t bg_scrollx, bg_scrolly, fg_scrollx, fg_scrolly;
    uint8_t bs[2];

    /* the 06XX */
    uint8_t ctrl;
    int nmi_steps, nmi_count;
    /* the 51XX, 50XX and 54XX */
    uint8_t cnt, credit_mode, coinage_args, credits, prot_param, n54_skip;
    uint16_t keys_d[3];
    uint32_t rng;
} xv;

/* --- Graphics ---------------------------------------------------------------------- */

/* MAME's gfx_8x8x1 and bgcharlayout */
static const gal_layout_t fg_layout = {
    8, 8, 1, {0}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};
static const gal_layout_t bg_layout = {
    8, 8, 2, {0, 0x1000 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

/* MAME's spritelayout_xevious, three planes {RGN_FRAC(1,2)+4, 0, 4} of a
 * region that init_xevious makes 0xa000 long: the ROMs, the last one
 * repeated shifted right by four bits, and zeros */
static int spr_pixel(const uint8_t *rom, int n, int x, int y)
{
    const uint32_t off = (uint32_t)n * 512 + (y < 8 ? 8 * y : 256 + 8 * (y - 8)) + 64 * (x >> 2) + (x & 3);
    const int sh = 7 - (off & 7);
    const uint32_t b = off >> 3, hb = b + 0x5000;
    uint8_t hi = 0;
    if (hb < 0x7000)
        hi = rom[hb];
    else if (hb < 0x9000)
        hi = rom[hb - 0x2000] >> 4;
    const int p0 = (hi >> (sh - 4)) & 1, p1 = (rom[b] >> sh) & 1, p2 = (rom[b] >> (sh - 4)) & 1;
    return (p0 << 2) | (p1 << 1) | p2;
}

/* upright (MAME ROT90), as gal_conv_4bpp */
static void conv_sprites(const uint8_t *rom, int first, int count, uint32_t *out)
{
    for (int n = first; n < first + count; n++)
        for (int uy = 0; uy < 16; uy++)
            for (int w = 0; w < 2; w++)
            {
                uint32_t word = 0;
                for (int k = 0; k < 8; k++)
                    word |= (uint32_t)spr_pixel(rom, n, uy, 15 - (8 * w + k)) << (4 * k);
                *out++ = word;
            }
}

/* the table the flash cache keeps: the sprites */
static void xv_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    (void)i;
    conv_sprites(src, ofs / SPR_BYTES, len / SPR_BYTES, (uint32_t *)out);
}

/* --- The custom chips behind the 06XX ---------------------------------------------- */

static void chips_reset(void)
{
    xv.ctrl = 0x10;
    xv.nmi_steps = xv.nmi_count = 0;
    xv.cnt = xv.credit_mode = xv.coinage_args = xv.n54_skip = 0;
}

/* the 51XX in credit mode: the credits, then the joystick as a direction
 * 0-7 (8 none) with the fire button held (bit 5) and just pressed (bit 4),
 * active low */
static uint8_t read_51xx(void)
{
    const uint16_t keys = gal.buttons;
    xv.keys_d[2] = xv.keys_d[1];
    xv.keys_d[1] = xv.keys_d[0];
    xv.keys_d[0] = keys;
    if (!xv.credit_mode)
        return 0xff;
    if ((keys & GAL_BTN_COIN) && !(xv.keys_d[1] & GAL_BTN_COIN) && xv.credits < 99)
        xv.credits++;
    if ((keys & GAL_BTN_START) && !(xv.keys_d[1] & GAL_BTN_START) && xv.credits > 0)
        xv.credits--;
    const int up = keys & GAL_BTN_UP, down = keys & GAL_BTN_DOWN, left = keys & GAL_BTN_LEFT,
              right = keys & GAL_BTN_RIGHT;
    uint8_t dir = 8;
    if (up && left)
        dir = 7;
    else if (up && right)
        dir = 1;
    else if (down && right)
        dir = 3;
    else if (down && left)
        dir = 5;
    else if (left)
        dir = 6;
    else if (up)
        dir = 0;
    else if (right)
        dir = 2;
    else if (down)
        dir = 4;
    uint8_t fire = 0x30;
    if (keys & GAL_BTN_FIRE)
        fire &= ~0x20;
    if ((keys ^ xv.keys_d[2]) & keys & GAL_BTN_FIRE)
        fire &= ~0x10;
    const uint8_t seq[3] = {(uint8_t)(16 * (xv.credits / 10) + xv.credits % 10), (uint8_t)(dir | fire), 0x38};
    return xv.cnt < 3 ? seq[xv.cnt++] : 0xff;
}

static uint8_t data_read(void)
{
    if (xv.ctrl == 0x74)
    {
        /* the 50XX's answer to the protection check: the fourth byte */
        const uint8_t n = xv.cnt++;
        if (n == 3)
            return (xv.prot_param == 0x80 || xv.prot_param == 0x10) ? 0x05 : 0x95;
        return 0xff;
    }
    if (xv.ctrl & 1)
        return read_51xx();
    if (xv.ctrl & 4)
    {
        xv.rng ^= xv.rng << 7;
        xv.rng ^= xv.rng >> 9;
        xv.rng ^= xv.rng << 8;
        return (uint8_t)xv.rng;
    }
    return 0xff;
}

static void data_write(uint8_t value)
{
    xv.cnt++;
    if (xv.ctrl == 0x64)
    {
        xv.prot_param = value; /* the 50XX's parameter */
        return;
    }
    if (xv.ctrl == 0x68)
    {
        /* the 54XX: commands with their parameter bytes; the sounds are not
         * emulated */
        if (xv.n54_skip)
            xv.n54_skip--;
        else if ((value >> 4) == 3 || (value >> 4) == 4)
            xv.n54_skip = 4;
        else if ((value >> 4) == 6)
            xv.n54_skip = 5;
        return;
    }
    if (xv.ctrl & 1)
    {
        /* the 51XX's commands */
        if (xv.coinage_args)
        {
            xv.coinage_args--;
            return;
        }
        switch (value & 7)
        {
        case 1:
            xv.coinage_args = 6;
            break;
        case 2:
            xv.credit_mode = 1;
            break;
        case 5:
            xv.credit_mode = 0;
            break;
        default:
            break;
        }
    }
}

/* MAME's namco_06xx ctrl_w: an NMI every 64 << (bits 5-7) cycles while a
 * chip is selected */
static void ctrl_write(uint8_t value)
{
    xv.ctrl = value;
    xv.cnt = 0;
    xv.n54_skip = 0;
    if (value & 0x0f)
    {
        xv.nmi_steps = ((64 << ((value >> 5) & 7)) + CYCLES_PER_STEP - 1) / CYCLES_PER_STEP;
        xv.nmi_count = xv.nmi_steps;
    }
    else
        xv.nmi_steps = 0;
}

/* --- Memory map -------------------------------------------------------------------- */

/* MAME's xevious_bb_r: the background map, decoded from three ROMs */
static uint8_t bb_read(int offset)
{
    const uint8_t *rom2a = xv.planet, *rom2b = xv.planet + 0x1000, *rom2c = xv.planet + 0x3000;
    const int adr_2b = ((xv.bs[1] & 0x7e) << 6) | ((xv.bs[0] & 0xfe) >> 1);
    int dat1;
    if (adr_2b & 1)
        dat1 = ((rom2a[adr_2b >> 1] & 0xf0) << 4) | rom2b[adr_2b];
    else
        dat1 = ((rom2a[adr_2b >> 1] & 0x0f) << 8) | rom2b[adr_2b];
    int adr_2c = ((dat1 & 0x1ff) << 2) | ((xv.bs[1] & 1) << 1) | (xv.bs[0] & 1);
    if (dat1 & 0x400)
        adr_2c ^= 1;
    if (dat1 & 0x200)
        adr_2c ^= 2;
    if (offset & 1)
        return rom2c[adr_2c | 0x800];
    uint8_t dat2 = rom2c[adr_2c];
    dat2 = (uint8_t)((dat2 & 0x3f) | ((dat2 & 0x80) >> 1) | ((dat2 & 0x40) << 1));
    if (dat1 & 0x400)
        dat2 ^= 0x40;
    if (dat1 & 0x200)
        dat2 ^= 0x80;
    return dat2;
}

static uint8_t xv_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if ((addr & 0xfff8) == 0x6800)
    {
        /* MAME's bosco_dsw_r, both switches at their defaults (all off: 1C1C,
         * 3 lives, bonus at 20000/60000, normal); DSWB bit 0 is the blaster */
        const int bit = addr & 7;
        int b0 = 1;
        if (bit == 0 && (gal.buttons & GAL_BTN_EXTRA))
            b0 = 0;
        return (uint8_t)(b0 | 2);
    }
    if (addr == 0x7100)
        return xv.ctrl;
    if ((addr & 0xff00) == 0x7000)
        return data_read();
    if (addr >= 0xf000)
        return bb_read(addr & 1);
    return 0xff;
}

static void xv_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if ((addr & 0xffe0) == 0x6800)
        gal.soundregs[addr - 0x6800] = value & 0x0f;
    else if ((addr & 0xfff8) == 0x6820)
    {
        const int bit = addr & 7;
        if (bit < 2)
        {
            xv.irq_enable[bit] = value & 1;
            if (!(value & 1))
                z80_set_irq_line(&gal.cpu[bit], 0);
        }
        else if (bit == 2)
            xv.irq_enable[2] = value & 1;
        else if (bit == 3)
        {
            xv.sub_cpu_reset = !(value & 1);
            if (xv.sub_cpu_reset)
            {
                /* also the custom chips */
                chips_reset();
                z80_reset(&gal.cpu[1]);
                z80_reset(&gal.cpu[2]);
            }
        }
    }
    else if (addr == 0x7100)
        ctrl_write(value);
    else if ((addr & 0xff00) == 0x7000)
        data_write(value);
    else if ((addr & 0xff80) == 0xd000)
    {
        /* MAME's xevious_vh_latch_w: A0 is bit 8 of the scroll */
        const uint16_t scroll = (uint16_t)(value | ((addr & 1) << 8));
        switch ((addr >> 4) & 0x0f)
        {
        case 0:
            xv.bg_scrollx = scroll;
            break;
        case 1:
            xv.fg_scrollx = scroll;
            break;
        case 2:
            xv.bg_scrolly = scroll;
            break;
        case 3:
            xv.fg_scrolly = scroll;
            break;
        default:
            break; /* 7 the flip */
        }
    }
    else if (addr >= 0xf000)
        xv.bs[addr & 1] = value;
}

/* --- Set-up ----------------------------------------------------------------------- */

static int xv_init(uint8_t *const *regions)
{
    memset(&xv, 0, sizeof(xv));
    static const int rom_size[3] = {0x4000, 0x2000, 0x1000};
    for (int c = 0; c < 3; c++)
    {
        xv.rom[c] = (uint8_t *)gal_fast_alloc(rom_size[c]);
        if (!xv.rom[c])
            return 0;
        memcpy(xv.rom[c], regions[c], rom_size[c]);
    }
    xv.ram = (uint8_t *)gal_fast_alloc(RAM_SIZE);
    xv.fg = (uint16_t *)gal_fast_alloc(512 * 8 * 2);
    xv.fg_colors = (uint16_t *)gal_fast_alloc(64 * 4 * 2);
    xv.bg = (uint16_t *)gal_fast_alloc(512 * 8 * 2);
    xv.bg_colors = (uint16_t *)gal_fast_alloc(128 * 4 * 2);
    xv.spr_colors = (uint16_t *)gal_fast_alloc(64 * 8 * 2);
    xv.spr_mask = (uint8_t *)gal_fast_alloc(64);
    xv.waves = (int8_t *)gal_fast_alloc(256);
    if (!xv.ram || !xv.fg || !xv.fg_colors || !xv.bg || !xv.bg_colors || !xv.spr_colors || !xv.spr_mask || !xv.waves)
        return 0;
    xv.planet = regions[6];
    xv.rng = 0xace1;

    gal_conv_tiles8(regions[3], &fg_layout, 512, GAL_ROT90, xv.fg);
    gal_conv_tiles8(regions[4], &bg_layout, 512, GAL_ROT90, xv.bg);
    if (regions[GAL_MAX_REGIONS])
        xv.sprites = (const uint32_t *)regions[GAL_MAX_REGIONS];
    else
    {
        uint32_t *sprites = (uint32_t *)gal_alloc(NSPRITES * SPR_BYTES);
        if (!sprites)
            return 0;
        conv_sprites(regions[5], 0, NSPRITES, sprites);
        xv.sprites = sprites;
    }
    gal_conv_waves(regions[10] + 0x100, 8, xv.waves);

    /* MAME's xevious palette: 128 colours of 4+4+4 bits; the background
     * through two lookup PROMs (high nibble first in region 8), the sprites
     * through two (transparent unless bit 7), the characters directly */
    const uint8_t *prom = regions[7];
    uint16_t pal[128];
    for (int i = 0; i < 128; i++)
    {
        int c[3];
        for (int k = 0; k < 3; k++)
        {
            const uint8_t v = prom[0x100 * k + i];
            c[k] = 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
        }
        pal[i] = gal_color((uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)));
    }
    for (int i = 0; i < 512; i++)
    {
        const uint8_t lut = (uint8_t)((regions[8][0x200 + i] & 0x0f) | ((regions[8][i] & 0x0f) << 4));
        xv.bg_colors[i] = pal[lut & 0x7f];
    }
    for (int i = 0; i < 512; i++)
    {
        const uint8_t lut = (uint8_t)((regions[9][i] & 0x0f) | ((regions[9][0x200 + i] & 0x0f) << 4));
        xv.spr_colors[i] = pal[lut & 0x7f];
        if (!(lut & 0x80))
            xv.spr_mask[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    for (int i = 0; i < 64; i++)
        for (int p = 0; p < 4; p++)
            xv.fg_colors[4 * i + p] = pal[i];

    for (int c = 0; c < 3; c++)
    {
        z80_t *cpu = &gal.cpu[c];
        for (int p = 0; p < 0x100; p++)
        {
            cpu->rd_page[p] = gal_zeros;
            cpu->wr_page[p] = gal_ignored;
        }
        for (int p = 0; p < rom_size[c] / 256; p++)
            cpu->rd_page[p] = xv.rom[c] + 256 * p;
        static const uint8_t page[8] = {0x78, 0x80, 0x90, 0xa0, 0xb0, 0xb8, 0xc0, 0xc8};
        for (int k = 0; k < 8; k++)
            for (int p = 0; p < 8; p++)
                cpu->rd_page[page[k] + p] = cpu->wr_page[page[k] + p] = xv.ram + 0x800 * k + 256 * p;
        cpu->rd_page[0x68] = cpu->rd_page[0x70] = cpu->rd_page[0x71] = NULL;
        cpu->wr_page[0x68] = cpu->wr_page[0x70] = cpu->wr_page[0x71] = cpu->wr_page[0xd0] = NULL;
        for (int p = 0xf0; p < 0x100; p++)
            cpu->rd_page[p] = cpu->wr_page[p] = NULL;
        cpu->read = xv_read;
        cpu->write = xv_write;
    }
    return 1;
}

static void xv_reset(void)
{
    memset(xv.ram, 0, RAM_SIZE);
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    memset(xv.irq_enable, 0, sizeof(xv.irq_enable));
    xv.sub_cpu_reset = 1;
    xv.bg_scrollx = xv.bg_scrolly = xv.fg_scrollx = xv.fg_scrolly = 0;
    xv.bs[0] = xv.bs[1] = 0;
    xv.credits = 0;
    memset(xv.keys_d, 0, sizeof(xv.keys_d));
    chips_reset();
    for (int c = 0; c < 3; c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    gal_wsg_reset();
}

static void xv_run_slice(int q)
{
    /* profiling builds count the three CPUs in gal_prof[2..4] */
    const int first = q * STEPS_PER_FRAME / 4, last = (q + 1) * STEPS_PER_FRAME / 4;
    for (int i = first; i < last; i++)
    {
        GAL_PROF_START(t0);
        z80_run(&gal.cpu[0], CYCLES_PER_STEP);
        GAL_PROF_ADD(2, t0);
        if (!xv.sub_cpu_reset)
        {
            GAL_PROF_START(t1);
            z80_run(&gal.cpu[1], CYCLES_PER_STEP);
            GAL_PROF_ADD(3, t1);
            GAL_PROF_START(t2);
            z80_run(&gal.cpu[2], CYCLES_PER_STEP);
            GAL_PROF_ADD(4, t2);
        }

        /* the 06XX's NMI */
        if (xv.nmi_steps && !--xv.nmi_count)
        {
            z80_set_nmi_line(&gal.cpu[0], 1);
            z80_set_nmi_line(&gal.cpu[0], 0);
            xv.nmi_count = xv.nmi_steps;
        }

        /* the third CPU's NMI at lines 64 and 192 */
        if ((i == 64 * STEPS_PER_FRAME / 264 || i == 192 * STEPS_PER_FRAME / 264) && !xv.sub_cpu_reset &&
            !xv.irq_enable[2])
        {
            z80_set_nmi_line(&gal.cpu[2], 1);
            z80_set_nmi_line(&gal.cpu[2], 0);
        }
    }

    if (q == 3)
    {
        if (xv.irq_enable[0])
            z80_set_irq_line(&gal.cpu[0], 1);
        if (!xv.sub_cpu_reset && xv.irq_enable[1])
            z80_set_irq_line(&gal.cpu[1], 1);
    }
}

static void xv_sound(int16_t *out, int n)
{
    gal_wsg_render(xv.waves, 7, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (223 - y, x), so
 * an s x s element at (x, y) has its upright corner at (224 - s - y, x); its
 * flips in x and y are flips in y and x upright */
#define UP_X(y, s) (224 - (s) - (y))

static void GAL_HOT(xv_prepare_frame)(void)
{
    const uint8_t *sr1 = xv.ram + RAM_SR1 + 0x780, *sr2 = xv.ram + RAM_SR2 + 0x780, *sr3 = xv.ram + RAM_SR3 + 0x780;
    gal.active_sprites = 0;
    /* MAME's draw_sprites: the later on top */
    for (int offs = 0; offs < 0x80; offs += 2)
    {
        if (sr3[offs + 1] & 0x40)
            continue;
        int code = (sr2[offs] & 0x80) ? (sr3[offs] & 0x3f) + 0x100 : sr3[offs];
        const int color = sr3[offs + 1] & 0x3f;
        const int flipx = (sr2[offs] & 4) != 0, flipy = (sr2[offs] & 8) != 0;
        const int sx = sr1[offs + 1] - 40 + 0x100 * (sr2[offs + 1] & 1), sy = 28 * 8 - sr1[offs] - 1;
        /* the cells: code, x, y */
        int cells[4][3], nc = 0;
        if (sr2[offs] & 2)
        {
            if (sr2[offs] & 1)
            {
                code &= ~3;
                cells[nc][0] = code + 3, cells[nc][1] = flipx ? sx : sx + 16, cells[nc][2] = flipy ? sy - 16 : sy, nc++;
                cells[nc][0] = code + 1, cells[nc][1] = flipx ? sx : sx + 16, cells[nc][2] = flipy ? sy : sy - 16, nc++;
            }
            code &= ~2;
            cells[nc][0] = code + 2, cells[nc][1] = flipx ? sx + 16 : sx, cells[nc][2] = flipy ? sy - 16 : sy, nc++;
            cells[nc][0] = code, cells[nc][1] = flipx ? sx + 16 : sx, cells[nc][2] = flipy ? sy : sy - 16, nc++;
        }
        else if (sr2[offs] & 1)
        {
            code &= ~1;
            cells[nc][0] = code, cells[nc][1] = flipx ? sx + 16 : sx, cells[nc][2] = flipy ? sy - 16 : sy, nc++;
            cells[nc][0] = code + 1, cells[nc][1] = flipx ? sx : sx + 16, cells[nc][2] = flipy ? sy - 16 : sy, nc++;
        }
        else
            cells[nc][0] = code, cells[nc][1] = sx, cells[nc][2] = sy, nc++;
        for (int k = 0; k < nc; k++)
        {
            if (gal.active_sprites >= GAL_MAX_SPRITES)
                return;
            const int c = cells[k][0] % NSPRITES;
            gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
            o->code = (uint8_t)c;
            o->color = (uint8_t)color;
            /* flags: 1 flip x, 2 flip y (upright), 0x80 code bit 8 */
            o->flags = (uint8_t)(flipy | (flipx << 1) | ((c >> 8) << 7));
            o->x = (int16_t)UP_X(cells[k][2], 16);
            o->y = (int16_t)cells[k][1];
        }
    }
}

/* The background, opaque, or the foreground characters over it, a tile at a
 * time. A raster pixel (x, y) shows the tilemap's pixel (x + scrollx - dx,
 * y + scrolly - dy), with MAME's offsets: bg (-20, -16), fg (-32, -18). */
static void GAL_HOT(draw_layer)(const gal_band_t *b, int fg)
{
    const int sxo = ((fg ? xv.fg_scrollx : xv.bg_scrollx) + (fg ? 32 : 20)) & 0x1ff;
    const int syo = ((fg ? xv.fg_scrolly : xv.bg_scrolly) + (fg ? 18 : 16)) & 0xff;
    const uint8_t *vram = xv.ram + (fg ? RAM_FGVIDEO : RAM_BGVIDEO), *cram = xv.ram + (fg ? RAM_FGCOLOR : RAM_BGCOLOR);
    /* the band's raster lines (upright columns: y = 223 - X) */
    const int ry0 = 223 - (b->x0 + b->w - 1), ry1 = 223 - b->x0;
    for (int Y = b->y0 - ((b->y0 + sxo) & 7); Y < b->y0 + b->h; Y += 8)
    {
        const int tcol = ((Y + sxo) & 0x1ff) >> 3;
        for (int ry = ry0 - ((ry0 + syo) & 7); ry <= ry1; ry += 8)
        {
            const int idx = (((ry + syo) & 0xff) >> 3) * 64 + tcol;
            const uint8_t code = vram[idx], attr = cram[idx];
            const uint16_t *rows, *colors;
            if (fg)
            {
                rows = xv.fg + 8 * code;
                colors = xv.fg_colors + 4 * (((attr & 3) << 4) | ((attr & 0x3c) >> 2));
            }
            else
            {
                rows = xv.bg + 8 * (code | ((attr & 1) << 8));
                colors = xv.bg_colors + 4 * (((attr & 0x3c) >> 2) | ((code & 0x80) >> 3) | ((attr & 3) << 5));
            }
            /* raster flips (bit 6 x, bit 7 y) are upright flips in y and x */
            uint16_t t[8];
            if (attr & 0xc0)
            {
                const int fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
                for (int k = 0; k < 8; k++)
                {
                    uint32_t v = rows[fy ? 7 - k : k];
                    if (fx)
                        v = gal_rev2(v) >> 16;
                    t[k] = (uint16_t)v;
                }
                rows = t;
            }
            if (fg)
                gal_draw_tile(b, 216 - ry, Y, rows, colors);
            else
                gal_draw_tile_opaque(b, 216 - ry, Y, rows, colors);
        }
    }
}

static void GAL_HOT(xv_render_band)(const gal_band_t *b)
{
    draw_layer(b, 0);
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const int code = o->code | ((o->flags >> 7) << 8);
        gal_draw_4bpp_mask(b, o->x, o->y, 16, xv.sprites + 32 * code, o->flags & 1, (o->flags & 2) != 0,
                           xv.spr_colors + 8 * o->color, xv.spr_mask[o->color]);
    }
    draw_layer(b, 1);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t xv_hiscore[] = {
    {0x8510, 0x4d, 0x00, 0x24},
    {0x8024, 0x03, 0x00, 0x00},
    {0x8025, 0x01, 0x40, 0x40},
};

const gal_game_ops_t gal_xevious_ops = {
    .init = xv_init,
    .reset = xv_reset,
    .run_slice = xv_run_slice,
    .prepare_frame = xv_prepare_frame,
    .render_band = xv_render_band,
    .sound = xv_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .extra_button = 1,
    .large = 1,
    .nderived = 1,
    .derived_src = {5},
    .derived_size = {NSPRITES * SPR_BYTES},
    .derive = xv_derive,
    .hiscore = xv_hiscore,
    .nhiscore = sizeof(xv_hiscore) / sizeof(xv_hiscore[0]),
};
