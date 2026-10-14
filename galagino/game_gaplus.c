/*
 * Namco's Gaplus (1984), after MAME's namco/gaplus.cpp and gaplus_v.cpp, with
 * the program of its clone galaga3 (as GalaginoPlus runs it).
 *
 * Three 6809s at 1.54 MHz: the main CPU, the sub CPU that shares its video
 * and sprite RAM, and the sound CPU that plays the eight voices of the 15XX.
 * Writes to address ranges hold the IRQ enables and the resets of the other
 * CPUs and of the custom I/O chips (56XX for the controls, 58XX for the DIP
 * switches, gal_namcoio.c; the 62XX answers with fixed values). The
 * explosion sample of the 62XX is not played.
 *
 * 288x224 (MAME ROT90: the full upright 224x288 picture). Behind everything a
 * starfield of three sets moving at the speeds the program sets; a 36x28
 * tilemap of 8x8 tiles of two bits, drawn behind or in front of the sprites
 * (attribute bit 6) and transparent where their colour is the last one;
 * 64 sprites of 16x16 to 32x32, three bits per pixel. Large: the sprites are
 * read from PSRAM or the flash cache.
 */
#include "gal_internal.h"

#include "m6809.h"

#define CYCLES_PER_FRAME 25344 /* 1.536 MHz at 60.6 Hz */
#define STEPS 32
#define NSPRITES 384
#define SPR_BYTES 128 /* 16x16, four bits (three used) */
#define MAX_STARS 250

/* Galagino's RAM buffer: the main CPU's 0000-1fff, then the shared RAM */
#define RAM_VIDEO 0x0000  /* 0000-03ff the codes, 0400-07ff the attributes */
#define RAM_SPR1 0x0f80   /* code, colour */
#define RAM_SPR2 0x1780   /* y, x */
#define RAM_SPR3 0x1f80   /* flags, x high bit and disable */
#define RAM_SHARED 0x2000 /* 6000-63ff, the 15XX's registers first */

typedef struct
{
    int16_t x, y;
    uint16_t color;
    uint8_t set, visible;
} star_t;

typedef struct
{
    uint16_t x, y; /* on the upright picture */
    uint16_t color;
} star_px_t;

static struct
{
    m6809_state *cpu, *sub, *snd;
    uint8_t *rom, *rom_sub, *rom_snd;
    uint16_t *tiles;           /* [512][8] */
    uint8_t *tile_pens;        /* [512] the pens each character uses */
    const uint32_t *sprites;   /* [384][32] */
    uint16_t *tile_colors;     /* [64][4] */
    uint8_t *tile_mask;        /* [64] */
    uint16_t *spr_colors;      /* [64][8] */
    uint8_t *spr_mask;         /* [64] */
    int8_t *waves;
    star_t *stars;
    star_px_t *lit; /* the stars drawn this frame */
    int nstars, nlit;
    uint32_t framecount;
    uint8_t main_irq, sub_irq, snd_irq, subs_reset, io_reset;
    uint8_t starfield[4];
    uint8_t io3[16];
    gal_namcoio_t io[2];
} gp;

/* --- Graphics ---------------------------------------------------------------------- */

/* MAME's charlayout, on the character ROM followed by itself shifted right by
 * four bits (driver_init): the second 256 are the ROM's high nibbles */
static int chr_pixel(const uint8_t *rom, int n, int x, int y)
{
    static const uint16_t xo[8] = {128, 129, 192, 193, 0, 1, 64, 65};
    const uint32_t base = (uint32_t)(n & 0xff) * 256 + 8 * y + xo[x];
    int v = 0;
    for (int p = 0; p < 2; p++)
    {
        const uint32_t bit = base + (p ? 6 : 4) - (n >= 256 ? 4 : 0);
        v = (v << 1) | ((rom[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return v;
}

/* MAME's spritelayout, three planes {RGN_FRAC(1,2), 0, 4} of a region that
 * driver_init makes 0xc000 long: the four ROMs, the last one shifted left by
 * four bits, and zeros */
static int spr_pixel(const uint8_t *rom, int n, int x, int y)
{
    const uint32_t off = (uint32_t)n * 512 + (y < 8 ? 8 * y : 256 + 8 * (y - 8)) +
                         (x < 4 ? x : x < 8 ? 64 + x - 4 : x < 12 ? 128 + x - 8 : 192 + x - 12);
    const int sh = 7 - (off & 7);
    const uint32_t b = off >> 3, hb = b + 0x6000;
    uint8_t hi = 0;
    if (hb < 0x8000)
        hi = rom[hb];
    else if (hb < 0xa000)
        hi = (uint8_t)(rom[hb - 0x2000] << 4);
    const int p0 = (hi >> sh) & 1, p1 = (rom[b] >> sh) & 1, p2 = (rom[b] >> (sh - 4)) & 1;
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
static void gp_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    (void)i;
    conv_sprites(src, ofs / SPR_BYTES, len / SPR_BYTES, (uint32_t *)out);
}

/* MAME's starfield_init, as GalaginoPlus has it: a 32-bit LFSR over the
 * screen, three sets in turn; their colours through the sprite lookup */
static void stars_init(const uint8_t *spr_lut, const uint16_t *pal)
{
    uint32_t generator = 0;
    int set = 0;
    gp.nstars = 0;
    for (int y = 0; y < 224; y++)
        for (int x = 288 - 32 - 1; x >= 0; x--)
        {
            generator <<= 1;
            const int bit1 = (~generator >> 17) & 1, bit2 = (generator >> 5) & 1;
            if (bit1 ^ bit2)
                generator |= 1;
            if (((~generator >> 16) & 1) && (generator & 0xff) == 0xff && gp.nstars < MAX_STARS)
            {
                static const uint16_t base[3] = {0x250, 0x230, 0x210};
                const int color = (int)((~(generator >> 8)) % 7) + 1;
                star_t *s = &gp.stars[gp.nstars++];
                s->x = (int16_t)(x + 16);
                s->y = (int16_t)y;
                const int i = base[set] + color - 256;
                const uint8_t lut = (uint8_t)((spr_lut[i] & 0x0f) | ((spr_lut[0x200 + i] & 0x0f) << 4));
                s->color = pal[lut];
                s->visible = lut != 0xff;
                s->set = (uint8_t)set;
                set = (set + 1) % 3;
            }
        }
}

static void stars_advance(void)
{
    gp.framecount++;
    if (!(gp.starfield[0] & 1))
        return;
    for (int i = 0; i < gp.nstars; i++)
    {
        star_t *s = &gp.stars[i];
        switch (gp.starfield[s->set + 1])
        {
        case 0x85:
        case 0x86:
            s->x += 1;
            break;
        case 0x06:
            s->x += 2;
            break;
        case 0x80:
            s->x -= 1;
            break;
        case 0x82:
            s->x -= 2;
            break;
        case 0x81:
            s->x -= 3;
            break;
        case 0x9f:
            s->y += 3;
            break;
        case 0xaf:
            s->y -= 3;
            break;
        default:
            break; /* 0x87 stands still */
        }
        if (s->x < 16)
            s->x += 288 - 32;
        if (s->x >= 288 - 16)
            s->x -= 288 - 32;
        if (s->y < 0)
            s->y += 224;
        if (s->y >= 224)
            s->y -= 224;
    }
}

/* the stars drawn this frame, once for all the bands */
static void stars_prepare(void)
{
    gp.nlit = 0;
    if (!(gp.starfield[0] & 1))
        return;
    for (int i = 0; i < gp.nstars; i++)
    {
        const star_t *s = &gp.stars[i];
        if (!s->visible)
            continue;
        /* GalaginoPlus's twinkle of the second set */
        if (s->set == 1 && gp.starfield[2] != 0x85 && !(i & 1))
        {
            const uint32_t v = gp.framecount + (uint32_t)i;
            if ((v >> (((v >> 3) & 1) ? 1 : 2)) & 1)
                continue;
        }
        star_px_t *p = &gp.lit[gp.nlit++];
        p->x = (uint16_t)(223 - s->y);
        p->y = (uint16_t)s->x;
        p->color = s->color;
    }
}

/* --- The custom I/O chips ---------------------------------------------------------- */

static uint8_t io_in0(int port)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0x0f;
    switch (port)
    {
    case 0:
        if (keys & GAL_BTN_COIN)
            v &= ~0x01;
        break;
    case 1:
        if (keys & GAL_BTN_UP)
            v &= ~0x01;
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x02;
        if (keys & GAL_BTN_DOWN)
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

/* the DIP switches, MAME's defaults: all off */
static uint8_t io_in1(int port)
{
    (void)port;
    return 0x0f;
}

/* the 62XX */
static uint8_t io3_read(int offset)
{
    const uint8_t mode = gp.io3[8];
    switch (offset)
    {
    case 0:
        return 0x0f; /* upright, no service */
    case 1:
        return mode == 2 ? gp.io3[1] : 0x0f;
    case 2:
        return mode == 2 ? 0x0f : 0x0e;
    case 3:
        return mode == 2 ? gp.io3[3] : 0x01;
    default:
        return gp.io3[offset];
    }
}

/* --- CPUs ------------------------------------------------------------------------ */

static void subs_reset(int running)
{
    if (running && gp.subs_reset)
    {
        m6809_reset(gp.sub);
        gp.sub->icount = 0;
        m6809_reset(gp.snd);
        gp.snd->icount = 0;
    }
    gp.subs_reset = !running;
}

static uint8_t gp_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    switch (addr & 0xfff0)
    {
    case 0x6800:
        return 0xf0 | gp.io[0].ram[addr & 0x0f];
    case 0x6810:
        return 0xf0 | gp.io[1].ram[addr & 0x0f];
    case 0x6820:
        return io3_read(addr & 0x0f);
    default:
        return 0xff;
    }
}

static void gp_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    switch (addr & 0xf000)
    {
    case 0x6000:
        if ((addr & 0xfff0) == 0x6800)
            gp.io[0].ram[addr & 0x0f] = value & 0x0f;
        else if ((addr & 0xfff0) == 0x6810)
            gp.io[1].ram[addr & 0x0f] = value & 0x0f;
        else if ((addr & 0xfff0) == 0x6820)
            gp.io3[addr & 0x0f] = value;
        break;
    case 0x7000:
        gp.main_irq = !(addr & 0x800);
        if (!gp.main_irq)
            s->irq_pending = 0;
        break;
    case 0x8000:
        subs_reset(!(addr & 0x800));
        break;
    case 0x9000:
    {
        const uint8_t reset = (addr & 0x800) != 0;
        if (reset && !gp.io_reset)
        {
            gal_namcoio_reset(&gp.io[0]);
            gal_namcoio_reset(&gp.io[1]);
            memset(gp.io3, 0, sizeof(gp.io3));
        }
        gp.io_reset = reset;
        break;
    }
    case 0xa000:
        if (addr < 0xa800)
            gp.starfield[addr & 3] = value;
        break;
    default:
        break;
    }
}

static uint8_t gp_sub_read(m6809_state *s, uint16_t addr)
{
    (void)s;
    (void)addr;
    return 0xff;
}

static void gp_sub_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    (void)value;
    if ((addr & 0xf000) == 0x6000)
    {
        gp.sub_irq = addr & 1;
        if (!gp.sub_irq)
            s->irq_pending = 0;
    }
}

static void gp_snd_write(m6809_state *s, uint16_t addr, uint8_t value)
{
    (void)value;
    if (addr >= 0x4000 && addr < 0x8000)
    {
        gp.snd_irq = addr < 0x6000;
        if (!gp.snd_irq)
            s->irq_pending = 0;
    }
    /* 2000-3fff: the watchdog */
}

/* --- Set-up ----------------------------------------------------------------------- */

static void map_unused(m6809_state *c)
{
    for (int p = 0; p < 0x100; p++)
    {
        c->rd_page[p] = gal_zeros;
        c->wr_page[p] = gal_ignored;
    }
}

/* The handshake of the main and sub CPUs: each waits for the other to set a
 * byte of their shared RAM ($10af, DP $10) to its value, LDA <$af; CMPA #n;
 * BNE, 9 cycles a round, skipped by m6809_run */
static void poll_loop(m6809_state *c, const uint8_t *rom, uint16_t pc, uint8_t value)
{
    const uint8_t *p = rom + (pc - 0xa000);
    if (p[0] == 0x96 && p[1] == 0xaf && p[2] == 0x81 && p[3] == value && p[4] == 0x26 && p[5] == 0xfa)
    {
        c->poll_end = (uint16_t)(pc + 6);
        c->poll_cycles = 9;
    }
}

static int gp_init(uint8_t *const *regions)
{
    memset(&gp, 0, sizeof(gp));
    /* the programs to SRAM */
    gp.rom = (uint8_t *)gal_fast_alloc(0x6000);
    gp.rom_sub = (uint8_t *)gal_fast_alloc(0x6000);
    gp.rom_snd = (uint8_t *)gal_fast_alloc(0x2000);
    gp.cpu = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    gp.sub = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    gp.snd = (m6809_state *)gal_fast_alloc(sizeof(m6809_state));
    gp.tiles = (uint16_t *)gal_fast_alloc(512 * 8 * 2);
    gp.tile_pens = (uint8_t *)gal_fast_alloc(512);
    gp.tile_colors = (uint16_t *)gal_fast_alloc(64 * 4 * 2);
    gp.tile_mask = (uint8_t *)gal_fast_alloc(64);
    gp.spr_colors = (uint16_t *)gal_fast_alloc(64 * 8 * 2);
    gp.spr_mask = (uint8_t *)gal_fast_alloc(64);
    gp.waves = (int8_t *)gal_fast_alloc(256);
    gp.stars = (star_t *)gal_fast_alloc(MAX_STARS * sizeof(star_t));
    gp.lit = (star_px_t *)gal_fast_alloc(MAX_STARS * sizeof(star_px_t));
    if (!gp.rom || !gp.rom_sub || !gp.rom_snd || !gp.cpu || !gp.sub || !gp.snd || !gp.tiles || !gp.tile_pens ||
        !gp.tile_colors || !gp.tile_mask || !gp.spr_colors || !gp.spr_mask || !gp.waves || !gp.stars || !gp.lit)
        return 0;
    memcpy(gp.rom, regions[0], 0x6000);
    memcpy(gp.rom_sub, regions[1], 0x6000);
    memcpy(gp.rom_snd, regions[2], 0x2000);
    gp.io[0].in = io_in0;
    gp.io[1].in = io_in1;

    for (int n = 0; n < 512; n++)
    {
        uint8_t pens = 0;
        for (int uy = 0; uy < 8; uy++)
        {
            uint16_t row = 0;
            for (int ux = 0; ux < 8; ux++)
            {
                const int pen = chr_pixel(regions[3], n, uy, 7 - ux);
                row |= (uint16_t)(pen << (2 * ux));
                pens |= (uint8_t)(1u << pen);
            }
            gp.tiles[8 * n + uy] = row;
        }
        gp.tile_pens[n] = pens;
    }
    if (regions[GAL_MAX_REGIONS])
        gp.sprites = (const uint32_t *)regions[GAL_MAX_REGIONS];
    else
    {
        uint32_t *sprites = (uint32_t *)gal_alloc(NSPRITES * SPR_BYTES);
        if (!sprites)
            return 0;
        conv_sprites(regions[4], 0, NSPRITES, sprites);
        gp.sprites = sprites;
    }
    gal_conv_waves(regions[8], 8, gp.waves);

    /* MAME's gaplus palette: 256 colours of 4+4+4 bits; characters through
     * their lookup PROM into the last 16, sprites through two (low and high
     * nibble) into all 256 */
    const uint8_t *prom = regions[5];
    uint16_t pal[256];
    for (int i = 0; i < 256; i++)
    {
        int c[3];
        for (int k = 0; k < 3; k++)
        {
            const uint8_t v = prom[0x100 * k + i];
            c[k] = 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
        }
        pal[i] = gal_color((uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)));
    }
    for (int i = 0; i < 64 * 4; i++)
    {
        const uint8_t lut = regions[6][i] & 0x0f;
        gp.tile_colors[i] = pal[0xf0 | lut];
        if (lut == 0x0f)
            gp.tile_mask[i >> 2] |= (uint8_t)(1u << (i & 3));
    }
    for (int i = 0; i < 0x200; i++)
    {
        const uint8_t lut = (uint8_t)((regions[7][i] & 0x0f) | ((regions[7][0x200 + i] & 0x0f) << 4));
        gp.spr_colors[i] = pal[lut];
        if (lut == 0xff)
            gp.spr_mask[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    stars_init(regions[7], pal);

    m6809_state *c = gp.cpu;
    memset(c, 0, sizeof(*c));
    map_unused(c);
    for (int p = 0; p < 0x20; p++) /* the video and sprite RAM */
        c->rd_page[p] = c->wr_page[p] = gal.ram + 256 * p;
    for (int p = 0; p < 4; p++)
        c->rd_page[0x60 + p] = c->wr_page[0x60 + p] = gal.ram + RAM_SHARED + 256 * p;
    c->rd_page[0x68] = NULL; /* the I/O chips */
    for (int p = 0x68; p < 0xa8; p++)
        c->wr_page[p] = NULL; /* I/O, the IRQ enable, the resets, the starfield */
    for (int p = 0; p < 0x60; p++)
        c->rd_page[0xa0 + p] = gp.rom + 256 * p;
    c->read = gp_read;
    c->write = gp_write;
    poll_loop(c, gp.rom, 0xc14b, 0x11);

    m6809_state *s = gp.sub;
    memset(s, 0, sizeof(*s));
    map_unused(s);
    for (int p = 0; p < 0x20; p++)
        s->rd_page[p] = s->wr_page[p] = gal.ram + 256 * p;
    for (int p = 0x60; p < 0x70; p++)
        s->wr_page[p] = NULL; /* the IRQ enable */
    for (int p = 0; p < 0x60; p++)
        s->rd_page[0xa0 + p] = gp.rom_sub + 256 * p;
    s->read = gp_sub_read;
    s->write = gp_sub_write;
    poll_loop(s, gp.rom_sub, 0xe0e2, 0x22);

    m6809_state *d = gp.snd;
    memset(d, 0, sizeof(*d));
    map_unused(d);
    for (int p = 0; p < 4; p++)
        d->rd_page[p] = d->wr_page[p] = gal.ram + RAM_SHARED + 256 * p;
    for (int p = 0x40; p < 0x80; p++)
        d->wr_page[p] = NULL; /* the IRQ enable */
    for (int p = 0; p < 0x20; p++)
        d->rd_page[0xe0 + p] = gp.rom_snd + 256 * p;
    d->read = gp_sub_read;
    d->write = gp_snd_write;
    return 1;
}

static void gp_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    gp.main_irq = gp.sub_irq = gp.snd_irq = 0;
    gp.subs_reset = gp.io_reset = 1;
    memset(gp.starfield, 0, sizeof(gp.starfield));
    memset(gp.io3, 0, sizeof(gp.io3));
    gal_namcoio_reset(&gp.io[0]);
    gal_namcoio_reset(&gp.io[1]);
    gp.framecount = 0;
    m6809_reset(gp.cpu);
    gp.cpu->icount = 0;
    m6809_reset(gp.sub);
    m6809_reset(gp.snd);
    gal_wsg15_reset();
}

static void gp_run_slice(int q)
{
    for (int i = 0; i < STEPS / 4; i++)
    {
        const int step = q * (STEPS / 4) + i;
        const int len = (step + 1) * CYCLES_PER_FRAME / STEPS - step * CYCLES_PER_FRAME / STEPS;
        /* profiling builds count the main, sub and sound CPUs in gal_prof[2..4] */
        GAL_PROF_START(t0);
        m6809_run(gp.cpu, len);
        GAL_PROF_ADD(2, t0);
        if (!gp.subs_reset)
        {
            GAL_PROF_START(t1);
            m6809_run(gp.sub, len);
            GAL_PROF_ADD(3, t1);
            GAL_PROF_START(t2);
            m6809_run(gp.snd, len);
            GAL_PROF_ADD(4, t2);
        }
    }
    if (q == 3)
    {
        if (gp.main_irq)
            m6809_irq(gp.cpu);
        if (!gp.subs_reset)
        {
            if (gp.sub_irq)
                m6809_irq(gp.sub);
            if (gp.snd_irq)
                m6809_irq(gp.snd);
        }
        if (!gp.io_reset)
        {
            gal_namcoio_run_56xx(&gp.io[0]);
            gal_namcoio_run_58xx(&gp.io[1]);
        }
        stars_advance();
    }
}

static void gp_sound(int16_t *out, int n)
{
    gal_wsg15_render(gal.ram + RAM_SHARED, gp.waves, !gp.subs_reset, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (223 - y, x), so
 * an s x s element at (x, y) has its upright corner at (224 - s - y, x); its
 * flips in x and y are flips in y and x upright */
#define UP_X(y, s) (224 - (s) - (y))

static void gp_prepare_frame(void)
{
    static const uint8_t gfx_offs[2][2] = {{0, 1}, {2, 3}};
    const uint8_t *s1 = gal.ram + RAM_SPR1, *s2 = gal.ram + RAM_SPR2, *s3 = gal.ram + RAM_SPR3;
    stars_prepare();
    gal.active_sprites = 0;
    /* MAME's draw_sprites: the later on top */
    for (int offs = 0; offs < 0x80; offs += 2)
    {
        if (s3[offs + 1] & 2)
            continue;
        const int code = s1[offs] | ((s3[offs] & 0x40) << 2);
        const int color = s1[offs + 1] & 0x3f;
        const int flipx = s3[offs] & 1, flipy = (s3[offs] >> 1) & 1;
        const int sizey = (s3[offs] >> 5) & 1, sizex = (s3[offs] >> 3) & 1, duplicate = s3[offs] & 0x80;
        const int sx = s2[offs + 1] + 0x100 * (s3[offs + 1] & 1) - 71;
        int sy = 256 - s2[offs] - 8;
        sy -= 16 * sizey;
        sy = (sy & 0xff) - 32;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++)
            {
                if (gal.active_sprites >= GAL_MAX_SPRITES)
                    return;
                const int c = (code + (duplicate ? 0 : gfx_offs[y ^ (sizey & flipy)][x ^ (sizex & flipx)])) % NSPRITES;
                gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
                o->code = (uint8_t)c;
                o->color = (uint8_t)color;
                /* flags: 1 flip x, 2 flip y (upright), 0x80 code bit 8 */
                o->flags = (uint8_t)(flipy | (flipx << 1) | ((c >> 8) << 7));
                o->x = (int16_t)UP_X(sy + 16 * y, 16);
                o->y = (int16_t)(sx + 16 * x);
            }
    }
}

/* a tile with the pens of its last colour left out */
static void draw_tile(const gal_band_t *b, int px, int py, const uint16_t *tile, const uint16_t *colors,
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

/* MAME's tilemap_scan: 36 columns of 28 rows, the outer two on each side
 * (the scores) stored apart. Most of the screen is space: tiles of only
 * transparent pens are skipped, those without any drawn whole. */
static void draw_tiles(const gal_band_t *b, int category)
{
    const int c0 = b->y0 >> 3, c1 = (b->y0 + b->h - 1) >> 3;
    for (int c = c0; c <= c1 && c < 36; c++)
        for (int r = 0; r < 28; r++)
        {
            const int x = UP_X(8 * r, 8);
            if (x + 8 <= b->x0 || x >= b->x0 + b->w)
                continue;
            const int row = r + 2, col = c - 2;
            const int idx = (col & 0x20) ? row + ((col & 0x1f) << 5) : col + (row << 5);
            const uint8_t attr = gal.ram[RAM_VIDEO + 0x400 + idx];
            if (((attr >> 6) & 1) != category)
                continue;
            const int code = gal.ram[RAM_VIDEO + idx] + ((attr & 0x80) << 1);
            const uint8_t pens = gp.tile_pens[code], transparent = gp.tile_mask[attr & 0x3f];
            if (!(pens & ~transparent))
                continue;
            if (pens & transparent)
                draw_tile(b, x, 8 * c, gp.tiles + 8 * code, gp.tile_colors + 4 * (attr & 0x3f), transparent);
            else
                gal_draw_tile_opaque(b, x, 8 * c, gp.tiles + 8 * code, gp.tile_colors + 4 * (attr & 0x3f));
        }
}

static void draw_stars(const gal_band_t *b)
{
    for (int i = 0; i < gp.nlit; i++)
    {
        const star_px_t *s = &gp.lit[i];
        const int y = s->y - b->y0, x = s->x - b->x0;
        if ((unsigned)y < (unsigned)b->h && (unsigned)x < (unsigned)b->w)
            b->buf[y * b->w + x] = s->color;
    }
}

static void gp_render_band(const gal_band_t *b)
{
    gal_fill_band(b, 0);
    draw_stars(b);
    draw_tiles(b, 0);
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        /* most miss the band: about 90 of them, 36 bands */
        if (o->y >= b->y0 + b->h || o->y + 16 <= b->y0 || o->x >= b->x0 + b->w || o->x + 16 <= b->x0)
            continue;
        const int code = o->code | ((o->flags >> 7) << 8);
        gal_draw_4bpp_mask(b, o->x, o->y, 16, gp.sprites + 32 * code, o->flags & 1, (o->flags & 2) != 0,
                           gp.spr_colors + 8 * o->color, gp.spr_mask[o->color]);
    }
    draw_tiles(b, 1);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t gp_hiscore[] = {
    {0x03ed, 0x08, 0x30, 0x20},
    {0x0900, 0xa0, 0x20, 0x00},
    {0x09b6, 0x03, 0x00, 0x00},
};

static uint8_t gp_hs_read(uint16_t addr)
{
    return gal.ram[addr & 0x1fff];
}

static void gp_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[addr & 0x1fff] = value;
}

const gal_game_ops_t gal_gaplus_ops = {
    .init = gp_init,
    .reset = gp_reset,
    .run_slice = gp_run_slice,
    .prepare_frame = gp_prepare_frame,
    .render_band = gp_render_band,
    .sound = gp_sound,
    .view_top = 0,
    .view_h = GAL_H,
    .large = 1,
    .nderived = 1,
    .derived_src = {4},
    .derived_size = {NSPRITES * SPR_BYTES},
    .derive = gp_derive,
    .hiscore = gp_hiscore,
    .nhiscore = sizeof(gp_hiscore) / sizeof(gp_hiscore[0]),
    .hs_read = gp_hs_read,
    .hs_write = gp_hs_write,
};
