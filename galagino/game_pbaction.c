/*
 * Pinball Action (Tehkan, 1985), after MAME's tecmo/pbaction.cpp.
 *
 * A Z80 at 4 MHz and a sound Z80 at 3 MHz with three AY-3-8910 at 1.5 MHz and
 * a Z80 CTC: channel 0 counts the main CPU's commands (each one an interrupt),
 * channel 1 is the music's timer. The main CPU's NMI comes at the vertical
 * blank while the game allows it. 256x224 shown (MAME ROT90: the upright
 * picture is 224x256, drawn at y 16..271).
 *
 * A background layer of 2048 tiles of four bits per pixel, the sprites (16x16
 * or 32x32, three bits) and over them a foreground layer of 1024 tiles of
 * three bits; both layers scroll along x with the table. The palette is RAM,
 * 256 colours of 12 bits.
 *
 * The game does not fit in SRAM: its programs go to SRAM, the converted
 * graphics (128 KB) to PSRAM, or come from the flash cache.
 *
 * The flippers are B (or left) and A (or right), the ball shooter down, a
 * nudge up.
 */
#include "gal_internal.h"

#define MAIN_CYCLES_PER_FRAME 66667 /* 4 MHz at 60 Hz */
#define SND_CYCLES_PER_FRAME 50000  /* 3 MHz */
#define SND_STEPS 16                /* sound CPU slices per frame, for the CTC */
#define VIEW_TOP 16

/* Galagino's RAM buffer */
#define RAM_MAIN 0x0000   /* c000-cfff */
#define RAM_VIDEO 0x1000  /* d000-dfff: fg video, fg colour, bg video, bg colour */
#define RAM_SPRITE 0x2000 /* e000-e07f */
#define RAM_PAL 0x2100    /* e400-e5ff */

#define FG_BYTES (8 * 4)   /* an 8x8 element */
#define SPR_BYTES (16 * 8) /* a 16x16 one */

static struct
{
    uint8_t *rom, *rom_sound, *ram_sound;
    const uint32_t *fg, *bg, *spr;
    uint16_t *colors; /* [256], in the arena */
    uint8_t nmi_mask;
    uint8_t latch;
    int scroll;
    uint8_t ay_addr[3];
    gal_ctc_t ctc;
} pa;


/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t fg_layout = {
    8, 8, 3, {0, 0x2000 * 8, 0x4000 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

static const gal_layout_t bg_layout = {
    8, 8, 4, {0, 0x4000 * 8, 0x8000 * 8, 0xc000 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

static const gal_layout_t spr_layout = {
    16, 16, 3, {0, 0x2000 * 8, 0x4000 * 8},
    {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71},
    {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184},
    32 * 8};

/* the tables the flash cache keeps: foreground, background, sprites */
static void pa_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    static const gal_layout_t *const layout[3] = {&fg_layout, &bg_layout, &spr_layout};
    const int eb = (i == 2) ? SPR_BYTES : FG_BYTES;
    gal_conv_4bpp(src, layout[i], ofs / eb, len / eb, GAL_ROT90, (uint32_t *)out);
}

/* the highest priority request goes to the sound CPU when it can take it */
static void ctc_interrupt(void)
{
    z80_t *cpu = &gal.cpu[1];
    if (!pa.ctc.pending || cpu->irq_state)
        return;
    z80_hold_irq(cpu, (uint8_t)gal_ctc_take(&pa.ctc));
}

/* --- Main CPU ---------------------------------------------------------------------- */

static uint8_t pa_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0;
    switch (addr)
    {
    case 0xe600: /* P1, active high */
        if (keys & (GAL_BTN_EXTRA | GAL_BTN_LEFT))
            v |= 0x08; /* left flipper */
        if (keys & (GAL_BTN_FIRE | GAL_BTN_RIGHT))
            v |= 0x10; /* right flipper */
        if (keys & GAL_BTN_DOWN)
            v |= 0x01; /* ball shooter */
        if (keys & GAL_BTN_UP)
            v |= 0x04; /* nudge */
        return v;
    case 0xe602: /* SYSTEM */
        if (keys & GAL_BTN_COIN)
            v |= 0x01;
        if (keys & GAL_BTN_START)
            v |= 0x04;
        if (keys & GAL_BTN_START2)
            v |= 0x08;
        return v;
    case 0xe604:
        return 0x40; /* MAME's defaults: 1C1C, 3 balls, upright, no demo sounds */
    case 0xe605:
        return 0x00; /* bonus at 70k 200k, normal flippers and outlanes */
    default:
        return 0;
    }
}

static void pa_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    switch (addr)
    {
    case 0xe600:
        pa.nmi_mask = value & 1;
        if (!pa.nmi_mask)
            z80_set_nmi_line(cpu, 0);
        break;
    case 0xe606:
        pa.scroll = value - 3;
        break;
    case 0xe800:
        /* the command, and a pulse on the CTC's trigger 0 */
        pa.latch = value;
        gal_ctc_trigger(&pa.ctc, 0);
        break;
    default:
        break; /* e604: the flip of a cocktail cabinet */
    }
}

/* --- Sound CPU --------------------------------------------------------------------- */

static uint8_t pa_snd_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    return (addr == 0x8000) ? pa.latch : 0;
}

static void pa_snd_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    (void)addr;
    (void)value; /* ffff: interrupt acknowledge */
}

static uint8_t pa_snd_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    const int p = port & 0xff;
    if (p < 4)
        return gal_ctc_read(&pa.ctc, p);
    return 0xff;
}

static void pa_snd_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    const int p = port & 0xff;
    if (p < 4)
    {
        gal_ctc_write(&pa.ctc, p, value);
        return;
    }
    const int k = (p >> 4) - 1; /* 10, 20, 30 */
    if (k < 0 || k > 2 || (p & 0x0e))
        return;
    if (!(p & 1))
        pa.ay_addr[k] = value & 0x0f;
    else
        gal.soundregs[16 * k + pa.ay_addr[k]] = value;
}

/* --- Set-up ----------------------------------------------------------------------- */

static int pa_init(uint8_t *const *regions)
{
    memset(&pa, 0, sizeof(pa));
    /* the programs and the sound CPU's RAM to SRAM */
    pa.rom = (uint8_t *)gal_fast_alloc(0xa000);
    pa.rom_sound = (uint8_t *)gal_fast_alloc(0x2000);
    pa.ram_sound = (uint8_t *)gal_fast_alloc(0x800);
    pa.colors = (uint16_t *)gal_fast_alloc(256 * 2);
    if (!pa.rom || !pa.rom_sound || !pa.ram_sound || !pa.colors)
        return 0;
    memcpy(pa.rom, regions[0], 0xa000);
    memcpy(pa.rom_sound, regions[1], 0x2000);

    if (regions[GAL_MAX_REGIONS])
    {
        pa.fg = (const uint32_t *)regions[GAL_MAX_REGIONS];
        pa.bg = (const uint32_t *)regions[GAL_MAX_REGIONS + 1];
        pa.spr = (const uint32_t *)regions[GAL_MAX_REGIONS + 2];
    }
    else
    {
        uint32_t *fg = (uint32_t *)gal_alloc(1024 * FG_BYTES);
        uint32_t *bg = (uint32_t *)gal_alloc(2048 * FG_BYTES);
        uint32_t *spr = (uint32_t *)gal_alloc(256 * SPR_BYTES);
        if (!fg || !bg || !spr)
            return 0;
        gal_conv_4bpp(regions[2], &fg_layout, 0, 1024, GAL_ROT90, fg);
        gal_conv_4bpp(regions[3], &bg_layout, 0, 2048, GAL_ROT90, bg);
        gal_conv_4bpp(regions[4], &spr_layout, 0, 256, GAL_ROT90, spr);
        pa.fg = fg;
        pa.bg = bg;
        pa.spr = spr;
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0xa0; p++)
        cpu->rd_page[p] = pa.rom + 256 * p;
    for (int p = 0xa0; p < 0xc0; p++)
        cpu->rd_page[p] = gal_zeros;
    for (int p = 0; p < 0x20; p++)
    {
        /* c000-dfff: RAM, the layers */
        cpu->rd_page[0xc0 + p] = gal.ram + 256 * p;
        cpu->wr_page[0xc0 + p] = gal.ram + 256 * p;
    }
    cpu->rd_page[0xe0] = cpu->wr_page[0xe0] = gal.ram + RAM_SPRITE;
    cpu->rd_page[0xe4] = cpu->wr_page[0xe4] = gal.ram + RAM_PAL;
    cpu->rd_page[0xe5] = cpu->wr_page[0xe5] = gal.ram + RAM_PAL + 0x100;
    cpu->read = pa_read;
    cpu->write = pa_write;

    z80_t *snd = &gal.cpu[1];
    for (int p = 0; p < 0x20; p++)
        snd->rd_page[p] = pa.rom_sound + 256 * p;
    for (int p = 0; p < 8; p++)
        snd->rd_page[0x40 + p] = snd->wr_page[0x40 + p] = pa.ram_sound + 256 * p;
    snd->read = pa_snd_read;
    snd->write = pa_snd_write;
    snd->in = pa_snd_in;
    snd->out = pa_snd_out;
    return 1;
}

static void pa_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    memset(pa.ram_sound, 0, 0x800);
    memset(&pa.ctc, 0, sizeof(pa.ctc));
    pa.nmi_mask = 0;
    pa.latch = 0;
    pa.scroll = 0;
    for (int k = 0; k < 2; k++)
    {
        z80_init(&gal.cpu[k]);
        z80_reset(&gal.cpu[k]);
    }
    gal_ay_reset();
}

static void pa_run_slice(int q)
{
    for (int i = 0; i < SND_STEPS / 4; i++)
    {
        const int step = q * (SND_STEPS / 4) + i;
        const int main_cycles =
            (step + 1) * MAIN_CYCLES_PER_FRAME / SND_STEPS - step * MAIN_CYCLES_PER_FRAME / SND_STEPS;
        const int snd_cycles = (step + 1) * SND_CYCLES_PER_FRAME / SND_STEPS - step * SND_CYCLES_PER_FRAME / SND_STEPS;
        z80_run(&gal.cpu[0], main_cycles);
        ctc_interrupt();
        z80_run(&gal.cpu[1], snd_cycles);
        gal_ctc_clock(&pa.ctc, snd_cycles);
    }
    if (q == 3 && pa.nmi_mask)
        z80_set_nmi_line(&gal.cpu[0], 1);
}

static void pa_sound(int16_t *out, int n)
{
    gal_ay_render(3, 1500000, 3, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90, as Bomb Jack: the raster's pixel (x, y) is the upright pixel
 * (239 - y, x + 16) */
#define UP_X(y, s) (240 - (s) - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void pa_prepare_frame(void)
{
    const uint8_t *pal = gal.ram + RAM_PAL;
    for (int i = 0; i < 256; i++)
    {
        const unsigned v = pal[2 * i] | (pal[2 * i + 1] << 8);
        const unsigned r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
        pa.colors[i] = gal_color((uint16_t)(((r * 31 / 15) << 11) | ((g * 63 / 15) << 5) | (b * 31 / 15)));
    }

    /* MAME's draw_sprites: from the last to the first; an entry after a
     * large sprite is part of it */
    gal.active_sprites = 0;
    const uint8_t *spr = gal.ram + RAM_SPRITE;
    for (int offs = 0x80 - 4; offs >= 0; offs -= 4)
    {
        if (offs > 0 && (spr[offs - 4] & 0x80))
            continue;
        const uint8_t *s = spr + offs;
        const int large = (s[0] & 0x80) != 0;
        const int size = large ? 32 : 16;
        const int x = s[3] - pa.scroll, y = (large ? 225 : 241) - s[2];
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        o->code = large ? (s[0] & 0x1f) : s[0];
        o->color = s[1] & 0x0f;
        /* flags: 1 flip x, 2 flip y (upright), 4 large */
        o->flags = (uint8_t)(((s[1] & 0x80) ? 1 : 0) | ((s[1] & 0x40) ? 2 : 0) | (large ? 4 : 0));
        o->x = (int16_t)UP_X(y, size);
        o->y = (int16_t)UP_Y(x);
    }
}

/* a layer of 32x32 tiles scrolled along raster x: fg (three bits, pen 0
 * transparent, flips in both) or bg (four bits, opaque, flip in y) */
static void pa_layer(const gal_band_t *b, int fg)
{
    const uint8_t *vram = gal.ram + RAM_VIDEO + (fg ? 0x000 : 0x800);
    const uint8_t *cram = vram + 0x400;
    const int sx = pa.scroll & 255;
    const int ry0 = 239 - (b->x0 + b->w - 1), ry1 = 239 - b->x0;
    int rx0 = b->y0 - VIEW_TOP, rx1 = b->y0 + b->h - 1 - VIEW_TOP;
    if (rx0 < 0)
        rx0 = 0;
    if (rx1 > 255)
        rx1 = 255;
    for (int r = ry0 >> 3; r <= ry1 >> 3; r++)
        for (int x = rx0 - ((rx0 + sx) & 7); x <= rx1; x += 8)
        {
            const int i = r * 32 + (((x + sx) & 255) >> 3);
            const uint8_t attr = cram[i];
            if (fg)
            {
                const int code = vram[i] + 0x10 * (attr & 0x30);
                gal_draw_4bpp(b, UP_X(8 * r, 8), UP_Y(x), 8, pa.fg + code * (FG_BYTES / 4), (attr & 0x80) != 0,
                              (attr & 0x40) != 0, pa.colors + 8 * (attr & 0x0f), 0);
            }
            else
            {
                const int code = vram[i] + 0x10 * (attr & 0x70);
                gal_draw_4bpp(b, UP_X(8 * r, 8), UP_Y(x), 8, pa.bg + code * (FG_BYTES / 4), (attr & 0x80) != 0, 0,
                              pa.colors + 128 + 16 * (attr & 0x07), 1);
            }
        }
}

static void pa_render_band(const gal_band_t *b)
{
    gal_fill_band(b, 0);
    pa_layer(b, 0);
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const uint16_t *colors = pa.colors + 8 * o->color;
        const int fx = o->flags & 1, fy = (o->flags & 2) != 0;
        if (!(o->flags & 4))
        {
            gal_draw_4bpp(b, o->x, o->y, 16, pa.spr + o->code * (SPR_BYTES / 4), fx, fy, colors, 0);
            continue;
        }
        /* a 32x32 sprite is the 16x16 ones 128 + 4n.. (MAME's spritelayout2
         * from 0x1000), raster quadrants (0,0), (1,0), (0,1), (1,1) */
        for (int k = 0; k < 4; k++)
        {
            int qx = k & 1, qy = k >> 1;
            if (fy)
                qx ^= 1;
            if (fx)
                qy ^= 1;
            gal_draw_4bpp(b, o->x + 16 * (1 - qy), o->y + 16 * qx, 16,
                          pa.spr + (128 + 4 * o->code + k) * (SPR_BYTES / 4), fx, fy, colors, 0);
        }
    }
    pa_layer(b, 1);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t pa_hiscore[] = {
    {0xc093, 0x51, 0x07, 0x00},
    {0xc12f, 0x27, 0x48, 0x4b},
};

const gal_game_ops_t gal_pbaction_ops = {
    .init = pa_init,
    .reset = pa_reset,
    .run_slice = pa_run_slice,
    .prepare_frame = pa_prepare_frame,
    .render_band = pa_render_band,
    .sound = pa_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .extra_button = 1,
    .large = 1,
    .nderived = 3,
    .derived_src = {2, 3, 4},
    .derived_size = {1024 * FG_BYTES, 2048 * FG_BYTES, 256 * SPR_BYTES},
    .derive = pa_derive,
    .hiscore = pa_hiscore,
    .nhiscore = sizeof(pa_hiscore) / sizeof(pa_hiscore[0]),
};
