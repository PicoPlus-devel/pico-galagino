/*
 * Tutankham (Konami, 1982), after MAME's konami/tutankhm.cpp and
 * tutankhm_v.cpp, on the sound board of konami_snd.c.
 *
 * A 6809 at 1.54 MHz, its IRQ at every second vertical blank while enabled;
 * 36 KB of program in banks of 4 KB at 9000. The picture is a bitmap of
 * 256x256 pixels of four bits in RAM, the top 192 columns of it scrolled
 * vertically, through a palette of 16 colours in RAM. 256x224 shown; the
 * upright picture is 224x256, drawn at y 16..271. The starfield of the custom
 * chip 084 is not drawn.
 *
 * A fires (left and right at once, as GalaginoPlus does), B is the flash
 * bomb. The banks do not fit in SRAM: they are read from PSRAM or the flash
 * cache.
 */
#include "gal_internal.h"

#include "konami_snd.h"
#include "m6809.h"

#define CYCLES_PER_FRAME 25344 /* 1.536 MHz at 60.6 Hz */
#define STEPS 16
#define VIEW_TOP 16

/* Galagino's RAM buffer */
#define RAM_PAL 0x0000   /* 8000-800f */
#define RAM_WORK 0x0800  /* 8800-8fff */
#define RAM_SOUND 0x1400 /* the sound CPU's 3000-33ff */

static struct
{
    m6809_state *cpu;
    uint8_t *vram, *rom;
    const uint8_t *banks;
    uint16_t colors[16];
    uint8_t scroll;
    uint8_t irq_enable, irq_toggle;
} tk;

static uint8_t tk_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    if (addr < 0x8100)
        return gal.ram[RAM_PAL + (addr & 0x0f)];
    switch (addr & 0xfff0)
    {
    case 0x8100:
        return tk.scroll;
    case 0x8160:
        return 0x6b; /* DSW2, MAME's defaults: 3 lives, upright, bonus at 30000, normal, 1 flash bomb */
    case 0x8180: /* IN0, active low */
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        return v;
    case 0x81a0: /* IN1 */
        if (keys & GAL_BTN_LEFT)
            v &= ~0x01;
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x02;
        if (keys & GAL_BTN_UP)
            v &= ~0x04;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x30; /* shoot left and right */
        if (keys & GAL_BTN_EXTRA)
            v &= ~0x40; /* the flash bomb */
        return v;
    default: /* 8120 the watchdog, 81c0 the second player, 81e0 DSW1 (1C1C) */
        return 0xff;
    }
}

static void tk_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    if (addr < 0x8100)
    {
        gal.ram[RAM_PAL + (addr & 0x0f)] = value;
        return;
    }
    switch (addr & 0xff00)
    {
    case 0x8100:
        if ((addr & 0xfff0) == 0x8100)
            tk.scroll = value;
        break;
    case 0x8200:
        if ((addr & 7) == 0)
        {
            tk.irq_enable = value & 1;
            if (!tk.irq_enable)
                s->irq_pending = 0;
        }
        /* 1 pay out, 2 and 3 the coin counters, 4 the stars, 5 the mute,
         * 6 and 7 the flips */
        break;
    case 0x8300:
        for (int p = 0; p < 0x10; p++)
            s->rd_page[0x90 + p] = tk.banks + 0x1000 * ((value & 0x0f) % 9) + 256 * p;
        break;
    case 0x8600:
        konami_snd_trigger(value & 1);
        break;
    case 0x8700:
        konami_snd_command(value);
        break;
    default:
        break;
    }
}

static int tk_init(uint8_t *const *regions)
{
    memset(&tk, 0, sizeof(tk));
    /* the bitmap, the program and the CPU to SRAM; the banks stay where the
     * set is, PSRAM or the flash cache */
    tk.cpu = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    tk.vram = (uint8_t *)gal_fast_alloc(0x8000);
    tk.rom = (uint8_t *)gal_fast_alloc(0x6000);
    uint8_t *snd = (uint8_t *)gal_fast_alloc(0x2000);
    if (!tk.cpu || !tk.vram || !tk.rom || !snd)
        return 0;
    memcpy(tk.rom, regions[0], 0x6000);
    memcpy(snd, regions[2], 0x2000);
    tk.banks = regions[1];

    m6809_state *c = tk.cpu;
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 0x80; p++)
        c->rd_page[p] = c->wr_page[p] = tk.vram + 256 * p;
    for (int p = 0; p < 8; p++)
        c->rd_page[0x88 + p] = c->wr_page[0x88 + p] = gal.ram + RAM_WORK + 256 * p;
    for (int p = 0; p < 0x10; p++)
        c->rd_page[0x90 + p] = tk.banks + 256 * p;
    for (int p = 0; p < 0x60; p++)
        c->rd_page[0xa0 + p] = tk.rom + 256 * p;
    c->read = tk_read;
    c->write = tk_write;
    konami_snd_init(snd, 0x2000, RAM_SOUND);
    return 1;
}

static void tk_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(tk.vram, 0, 0x8000);
    tk.scroll = 0;
    tk.irq_enable = tk.irq_toggle = 0;
    for (int p = 0; p < 0x10; p++)
        tk.cpu->rd_page[0x90 + p] = tk.banks + 256 * p;
    m6809_reset(tk.cpu);
    tk.cpu->icount = 0;
    konami_snd_reset();
}

static void tk_run_slice(int q)
{
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        m6809_run(tk.cpu, (step + 1) * CYCLES_PER_FRAME / STEPS - step * CYCLES_PER_FRAME / STEPS);
        konami_snd_run((step + 1) * KONAMI_SND_CYCLES_PER_FRAME / STEPS - step * KONAMI_SND_CYCLES_PER_FRAME / STEPS);
    }
    if (q == 3)
    {
        /* every second vertical blank */
        tk.irq_toggle ^= 1;
        if (tk.irq_toggle && tk.irq_enable)
            m6809_irq(tk.cpu);
    }
}

static void tk_sound(int16_t *out, int n)
{
    konami_snd_render(out, n);
}

/* MAME's raw_to_rgb_func: resistors of 1K, 470 and 220 ohm against 470,
 * scaled to 224 to leave room for the stars */
static void tk_prepare_frame(void)
{
    static double wrg[3], wb[2];
    if (!wrg[0])
    {
        const double rg[3] = {1000, 470, 220}, bl[2] = {470, 220}, pd = 1.0 / 470;
        double s_rg = 0, s_b = 0;
        for (int k = 0; k < 3; k++)
            s_rg += 1 / rg[k];
        for (int k = 0; k < 2; k++)
            s_b += 1 / bl[k];
        const double out_rg = s_rg / (s_rg + pd), out_b = s_b / (s_b + pd);
        const double scale = 224 / (out_rg > out_b ? out_rg : out_b);
        for (int k = 0; k < 3; k++)
            wrg[k] = scale * (1 / rg[k]) / (s_rg + pd);
        for (int k = 0; k < 2; k++)
            wb[k] = scale * (1 / bl[k]) / (s_b + pd);
    }
    for (int i = 0; i < 16; i++)
    {
        const uint8_t c = gal.ram[RAM_PAL + i];
        const int r = (int)(wrg[0] * (c & 1) + wrg[1] * ((c >> 1) & 1) + wrg[2] * ((c >> 2) & 1) + 0.5);
        const int g = (int)(wrg[0] * ((c >> 3) & 1) + wrg[1] * ((c >> 4) & 1) + wrg[2] * ((c >> 5) & 1) + 0.5);
        const int b = (int)(wb[0] * ((c >> 6) & 1) + wb[1] * ((c >> 7) & 1) + 0.5);
        tk.colors[i] = gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }
    gal.active_sprites = 0;
}

/* MAME ROT90, with the flips the game sets: upright pixel (X, Y) is the
 * raster's pixel (271 - Y, X + 16), as GalaginoPlus draws it */
static void tk_render_band(const gal_band_t *b)
{
    for (int j = 0; j < b->h; j++)
    {
        uint16_t *dst = b->buf + j * b->w;
        const int x = 255 - (b->y0 + j - VIEW_TOP);
        if (x < 0 || x > 255)
        {
            for (int i = 0; i < b->w; i++)
                dst[i] = 0;
            continue;
        }
        const int scroll = x < 192 ? tk.scroll : 0, shift = 4 * (x & 1);
        const uint8_t *col = tk.vram + x / 2;
        for (int i = 0; i < b->w; i++)
        {
            const int y = (b->x0 + i + 16 + scroll) & 255;
            dst[i] = tk.colors[(col[y * 128] >> shift) & 15];
        }
    }
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t tk_hiscore[] = {
    {0x88a9, 0x31, 0x03, 0x01},
    {0x88a6, 0x03, 0x03, 0x40},
};

static uint8_t tk_hs_read(uint16_t addr)
{
    return gal.ram[RAM_WORK + (addr & 0x7ff)];
}

static void tk_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[RAM_WORK + (addr & 0x7ff)] = value;
}

const gal_game_ops_t gal_tutankhm_ops = {
    .init = tk_init,
    .reset = tk_reset,
    .run_slice = tk_run_slice,
    .prepare_frame = tk_prepare_frame,
    .render_band = tk_render_band,
    .sound = tk_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .extra_button = 1,
    .large = 1,
    .hiscore = tk_hiscore,
    .nhiscore = sizeof(tk_hiscore) / sizeof(tk_hiscore[0]),
    .hs_read = tk_hs_read,
    .hs_write = tk_hs_write,
};
