/*
 * BurgerTime (Data East, 1982), after MAME's dataeast/btime.cpp and
 * decocpu7.cpp.
 *
 * A DECO CPU-7 at 1.5 MHz: a 6502 that, after a write, fetches its next
 * opcode with bits swapped when the address has bits 8 and 2 set. A coin
 * raises its IRQ; the game polls the vertical blank on DSW1. A sound 6502 at
 * 500 kHz with two AY-3-8910: the main CPU's command raises its IRQ, and every
 * 16 lines an NMI while the game allows it. 240x240 shown (MAME ROT270): the
 * upright picture loses 8 columns at each side to fit the 224 of the canvas.
 *
 * 1024 characters and 256 sprites of 16x16 from the same ROMs, a background
 * of 16x16 tiles laid out by a map ROM, all three bits per pixel; a palette
 * of 16 colours in RAM.
 *
 * Bump 'n' Jump (Data East, 1982; the Midway set) on the same board: a DECO
 * C10707 at 750 kHz, which swaps bits 5 and 6 of every opcode; a coin is an
 * NMI; 256x240 shown; a background of 512x256 in RAM, scrolled along raster
 * x, with characters behind and in front of the sprites.
 *
 * The games do not fit in SRAM: their programs go to SRAM, the converted
 * graphics (72 KB) to PSRAM, or come from the flash cache.
 */
#include "gal_internal.h"

#include "m6502.h"

#define BT_CYCLES_PER_FRAME 26113  /* 1.5 MHz at 57.44 Hz */
#define BNJ_CYCLES_PER_FRAME 13057 /* 750 kHz */
#define SND_CYCLES_PER_FRAME 8704   /* 500 kHz */
#define SND_NMIS_PER_FRAME 17       /* 272 lines, one per 16 */
#define LINES 272
#define VISIBLE_LINES 240
#define VIEW_TOP 24

/* Galagino's RAM buffer */
#define RAM_MAIN 0x0000  /* 0000-07ff */
#define RAM_PAL 0x0800   /* 0c00-0c0f */
#define RAM_VIDEO 0x1000 /* 1000-13ff */
#define RAM_COLOR 0x1400 /* 1400-17ff */
#define RAM_SOUND 0x1800 /* the sound CPU's 0000-03ff */
#define RAM_BG 0x1c00    /* Bump 'n' Jump: 5000-53ff, the background first */

#define CHR_BYTES (8 * 4)   /* an 8x8 element */
#define SPR_BYTES (16 * 8)  /* a 16x16 one */

static struct
{
    uint8_t bnj; /* Bump 'n' Jump */
    int cycles_per_frame;
    m6502_t *cpu, *snd;
    uint8_t *rom, *rom_sound;
    const uint8_t *bgmap;
    const uint32_t *chr, *spr, *bg;
    uint16_t colors[16];
    uint8_t scroll0, scroll1;
    uint8_t latch, latch_pending;
    uint8_t snd_nmi_enable;
    uint8_t ay_addr[2];
    uint8_t coin;
    int run_base, run_len;
} bt;

/* --- Graphics ---------------------------------------------------------------------- */

static const gal_layout_t chr_layout = {
    8, 8, 3, {0x4000 * 8, 0x2000 * 8, 0}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 8 * 8};

#define TILE16(plane)                                                                       \
    {                                                                                       \
        16, 16, 3, {(plane) * 16, (plane) * 8, 0},                                          \
            {128, 129, 130, 131, 132, 133, 134, 135, 0, 1, 2, 3, 4, 5, 6, 7},               \
            {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120}, 32 * 8       \
    }
static const gal_layout_t spr_layout = TILE16(0x2000);
static const gal_layout_t bg_layout = TILE16(0x800);

/* Bump 'n' Jump's background tiles */
static const gal_layout_t bnj_bg_layout = {
    16, 16, 3, {0x1000 * 8 + 4, 0, 4},
    {384, 385, 386, 387, 256, 257, 258, 259, 128, 129, 130, 131, 0, 1, 2, 3},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120}, 64 * 8};

/* the tables the flash cache keeps: characters, sprites (both from the
 * character ROMs), background tiles */
static void derive(const gal_layout_t *bg, int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    const int eb = i ? SPR_BYTES : CHR_BYTES;
    const gal_layout_t *l = (i == 0) ? &chr_layout : (i == 1) ? &spr_layout : bg;
    gal_conv_4bpp(src, l, ofs / eb, len / eb, GAL_ROT270, (uint32_t *)out);
}

static void bt_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    derive(&bg_layout, i, src, ofs, out, len);
}

static void bnj_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    derive(&bnj_bg_layout, i, src, ofs, out, len);
}

/* --- Main CPU ---------------------------------------------------------------------- */

/* DECO CPU-7 */
static uint8_t bt_fetch(m6502_t *cpu, uint16_t addr)
{
    const uint8_t *page = cpu->rd_page[addr >> 8];
    uint8_t v = page ? page[addr & 0xff] : cpu->read(cpu, addr);
    if (cpu->written)
    {
        cpu->written = 0;
        if ((addr & 0x0104) == 0x0104) /* bitswap<8>(v, 6,5,3,4,2,7,1,0) */
            v = (uint8_t)(((v >> 6) & 1) << 7 | ((v >> 5) & 1) << 6 | ((v >> 3) & 1) << 5 | ((v >> 4) & 1) << 4 |
                          ((v >> 2) & 1) << 3 | ((v >> 7) & 1) << 2 | (v & 3));
    }
    return v;
}

/* DECO C10707: bitswap<8>(v, 7,5,6,4,3,2,1,0) on every opcode */
static uint8_t bnj_fetch(m6502_t *cpu, uint16_t addr)
{
    const uint8_t *page = cpu->rd_page[addr >> 8];
    const uint8_t v = page ? page[addr & 0xff] : cpu->read(cpu, addr);
    return (uint8_t)((v & 0x9f) | ((v & 0x20) << 1) | ((v & 0x40) >> 1));
}

static int in_vblank(void)
{
    const int c = bt.run_base + (bt.run_len - bt.cpu->icount);
    return c >= bt.cycles_per_frame * VISIBLE_LINES / LINES;
}

static uint8_t bnj_read(m6502_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    switch (addr)
    {
    case 0x1000: /* DSW1, MAME's defaults: 1C1C, no test mode, upright; bit 7 the vertical blank */
        return (uint8_t)(0x3f | (in_vblank() ? 0x80 : 0));
    case 0x1001: /* DSW2: 3 lives, a bonus every 30000, continues, easy */
        return 0xf7;
    case 0x1002: /* P1, active low */
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x01;
        if (keys & GAL_BTN_LEFT)
            v &= ~0x02;
        if (keys & GAL_BTN_UP)
            v &= ~0x04;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        return v;
    case 0x1004: /* SYSTEM, active low */
        if (keys & GAL_BTN_START)
            v &= ~0x08;
        if (keys & GAL_BTN_START2)
            v &= ~0x10;
        if (keys & GAL_BTN_COIN)
            v &= ~0x40;
        return v;
    default:
        break;
    }
    if (addr >= 0x4800 && addr < 0x5000)
    {
        const int o = addr & 0x3ff;
        return gal.ram[(addr < 0x4c00 ? RAM_VIDEO : RAM_COLOR) + 32 * (o % 32) + o / 32];
    }
    return addr == 0x1003 ? 0xff : 0;
}

static void bnj_write(m6502_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr >= 0x4800 && addr < 0x5000)
    {
        const int o = addr & 0x3ff;
        gal.ram[(addr < 0x4c00 ? RAM_VIDEO : RAM_COLOR) + 32 * (o % 32) + o / 32] = value;
        return;
    }
    switch (addr)
    {
    case 0x1002:
        bt.latch = value;
        bt.latch_pending = 1;
        break;
    case 0x5400:
        bt.scroll0 = value;
        break;
    case 0x5800:
        bt.scroll1 = value;
        break;
    default:
        break; /* 1001: the flip of a cocktail cabinet */
    }
}

static uint8_t bt_read(m6502_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v;
    switch (addr)
    {
    case 0x4000: /* P1, active low */
        v = 0xff;
        if (keys & GAL_BTN_RIGHT)
            v &= ~0x01;
        if (keys & GAL_BTN_LEFT)
            v &= ~0x02;
        if (keys & GAL_BTN_UP)
            v &= ~0x04;
        if (keys & GAL_BTN_DOWN)
            v &= ~0x08;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        return v;
    case 0x4002: /* SYSTEM: the starts active low, the coin active high */
        v = 0x3f;
        if (keys & GAL_BTN_START)
            v &= ~0x01;
        if (keys & GAL_BTN_START2)
            v &= ~0x02;
        if (keys & GAL_BTN_COIN)
            v |= 0x40;
        return v;
    case 0x4003: /* DSW1, MAME's defaults: 1C1C, upright; bit 7 the vertical blank */
        return (uint8_t)(0x3f | (in_vblank() ? 0x80 : 0));
    case 0x4004: /* DSW2: 3 lives, bonus at 10000, easy, pepper at the end of a level */
        return 0xeb;
    default:
        break;
    }
    /* the mirrors of the video and colour RAM, x and y swapped */
    if (addr >= 0x1800 && addr < 0x2000)
    {
        const int o = addr & 0x3ff;
        return gal.ram[(addr < 0x1c00 ? RAM_VIDEO : RAM_COLOR) + 32 * (o % 32) + o / 32];
    }
    return addr == 0x4001 ? 0xff : 0;
}

static void bt_write(m6502_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr >= 0x1800 && addr < 0x2000)
    {
        const int o = addr & 0x3ff;
        gal.ram[(addr < 0x1c00 ? RAM_VIDEO : RAM_COLOR) + 32 * (o % 32) + o / 32] = value;
        return;
    }
    switch (addr)
    {
    case 0x4003:
        bt.latch = value;
        bt.latch_pending = 1;
        break;
    case 0x4004:
        bt.scroll0 = value;
        break;
    default:
        break; /* 4002: the flip of a cocktail cabinet */
    }
}

/* --- Sound CPU --------------------------------------------------------------------- */

static uint8_t bt_snd_read(m6502_t *cpu, uint16_t addr)
{
    (void)cpu;
    if (addr >= 0xa000 && addr < 0xc000)
    {
        bt.latch_pending = 0;
        return bt.latch;
    }
    return 0;
}

static void bt_snd_write(m6502_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    const int k = (addr >= 0x6000); /* 2000-5fff the first AY, 6000-9fff the second */
    switch (addr & 0xe000)
    {
    case 0x2000:
    case 0x6000:
        gal.soundregs[16 * k + bt.ay_addr[k]] = value;
        break;
    case 0x4000:
    case 0x8000:
        bt.ay_addr[k] = value & 0x0f;
        break;
    case 0xc000:
        bt.snd_nmi_enable = value & 1;
        break;
    default:
        break;
    }
}

/* --- Set-up ----------------------------------------------------------------------- */

static int bt_init_common(uint8_t *const *regions, int bnj)
{
    memset(&bt, 0, sizeof(bt));
    bt.bnj = (uint8_t)bnj;
    bt.cycles_per_frame = bnj ? BNJ_CYCLES_PER_FRAME : BT_CYCLES_PER_FRAME;
    const int rom_size = bnj ? 0x6000 : 0x4000;
    bt.cpu = (m6502_t *)gal_fast_alloc(sizeof(m6502_t));
    bt.snd = (m6502_t *)gal_fast_alloc(sizeof(m6502_t));
    bt.rom = (uint8_t *)gal_fast_alloc(rom_size);
    bt.rom_sound = (uint8_t *)gal_fast_alloc(0x1000);
    if (!bt.cpu || !bt.snd || !bt.rom || !bt.rom_sound)
        return 0;
    memcpy(bt.rom, regions[0], rom_size);
    memcpy(bt.rom_sound, regions[1], 0x1000);
    bt.bgmap = bnj ? NULL : regions[4];

    if (regions[GAL_MAX_REGIONS])
    {
        bt.chr = (const uint32_t *)regions[GAL_MAX_REGIONS];
        bt.spr = (const uint32_t *)regions[GAL_MAX_REGIONS + 1];
        bt.bg = (const uint32_t *)regions[GAL_MAX_REGIONS + 2];
    }
    else
    {
        uint32_t *chr = (uint32_t *)gal_alloc(1024 * CHR_BYTES);
        uint32_t *spr = (uint32_t *)gal_alloc(256 * SPR_BYTES);
        uint32_t *bg = (uint32_t *)gal_alloc(64 * SPR_BYTES);
        if (!chr || !spr || !bg)
            return 0;
        gal_conv_4bpp(regions[2], &chr_layout, 0, 1024, GAL_ROT270, chr);
        gal_conv_4bpp(regions[2], &spr_layout, 0, 256, GAL_ROT270, spr);
        gal_conv_4bpp(regions[3], bnj ? &bnj_bg_layout : &bg_layout, 0, 64, GAL_ROT270, bg);
        bt.chr = chr;
        bt.spr = spr;
        bt.bg = bg;
    }

    m6502_t *c = bt.cpu;
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 8; p++)
        c->rd_page[p] = c->wr_page[p] = gal.ram + RAM_MAIN + 256 * p;
    if (bnj)
    {
        /* 4000-47ff video and colour RAM, 5000-53ff background and RAM,
         * 5c00 palette, a000-ffff program */
        for (int p = 0; p < 8; p++)
            c->rd_page[0x40 + p] = c->wr_page[0x40 + p] = gal.ram + RAM_VIDEO + 256 * p;
        for (int p = 0; p < 4; p++)
            c->rd_page[0x50 + p] = c->wr_page[0x50 + p] = gal.ram + RAM_BG + 256 * p;
        c->rd_page[0x5c] = c->wr_page[0x5c] = gal.ram + RAM_PAL;
        for (int p = 0; p < 0x60; p++)
            c->rd_page[0xa0 + p] = bt.rom + 256 * p;
        c->read = bnj_read;
        c->write = bnj_write;
        c->fetch = bnj_fetch;
    }
    else
    {
        c->rd_page[0x0c] = c->wr_page[0x0c] = gal.ram + RAM_PAL;
        for (int p = 0; p < 8; p++)
            c->rd_page[0x10 + p] = c->wr_page[0x10 + p] = gal.ram + RAM_VIDEO + 256 * p;
        for (int p = 0; p < 0x40; p++)
            c->rd_page[0xc0 + p] = bt.rom + 256 * p;
        c->read = bt_read;
        c->write = bt_write;
        c->fetch = bt_fetch;
    }

    m6502_t *s = bt.snd;
    memset(s, 0, sizeof(*s));
    for (int p = 0; p < 0x20; p++)
        s->rd_page[p] = s->wr_page[p] = gal.ram + RAM_SOUND + 256 * (p & 3);
    for (int p = 0; p < 0x20; p++)
        s->rd_page[0xe0 + p] = bt.rom_sound + 256 * (p & 0x0f);
    s->read = bt_snd_read;
    s->write = bt_snd_write;
    return 1;
}

static int bt_init(uint8_t *const *regions)
{
    return bt_init_common(regions, 0);
}

static int bnj_init(uint8_t *const *regions)
{
    return bt_init_common(regions, 1);
}

static void bt_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    bt.scroll0 = bt.scroll1 = 0;
    bt.latch = bt.latch_pending = 0;
    bt.snd_nmi_enable = 0;
    bt.coin = 0;
    m6502_reset(bt.cpu);
    m6502_reset(bt.snd);
    bt.cpu->icount = bt.snd->icount = 0;
    gal_ay_reset();
}

static void bt_run_slice(int q)
{
    if (q == 0)
    {
        /* a coin's rising edge: an IRQ, held until taken */
        const uint8_t coin = (gal.buttons & GAL_BTN_COIN) != 0;
        if (coin && !bt.coin)
        {
            if (bt.bnj)
                bt.cpu->nmi = 1; /* Bump 'n' Jump: an NMI */
            else
                bt.cpu->irq = bt.cpu->irq_hold = 1;
        }
        bt.coin = coin;
    }
    bt.run_base = q * bt.cycles_per_frame / 4;
    bt.run_len = (q + 1) * bt.cycles_per_frame / 4 - bt.run_base;
    m6502_run(bt.cpu, bt.run_len);

    /* the sound CPU, its NMIs spread over the frame */
    const int first = q * SND_NMIS_PER_FRAME / 4, last = (q + 1) * SND_NMIS_PER_FRAME / 4;
    for (int k = first; k < last; k++)
    {
        if (bt.snd_nmi_enable)
            bt.snd->nmi = 1;
        bt.snd->irq = bt.latch_pending;
        m6502_run(bt.snd, (k + 1) * SND_CYCLES_PER_FRAME / SND_NMIS_PER_FRAME - k * SND_CYCLES_PER_FRAME / SND_NMIS_PER_FRAME);
    }
}

static void bt_sound(int16_t *out, int n)
{
    gal_ay_render(2, 1500000, 6, out, n);
}

/* --- Video ------------------------------------------------------------------------ */

/* MAME ROT270, as Lady Bug: the raster's pixel (x, y) is the upright pixel
 * (y - 16, 271 - x), so an s x s element at (x, y) has its upright corner at
 * (y - 16, 272 - s - x) */
#define UP_X(y) ((y) - 16)
#define UP_Y(x, s) (272 - (s) - (x))

static void bt_prepare_frame(void)
{
    /* BGR_233, inverted */
    for (int i = 0; i < 16; i++)
    {
        const uint8_t v = (uint8_t)~gal.ram[RAM_PAL + i];
        const int r = v & 7, g = (v >> 3) & 7, b = v >> 6;
        bt.colors[i] = gal_color((uint16_t)(((r * 31 / 7) << 11) | ((g * 63 / 7) << 5) | (b * 31 / 3)));
    }

    /* MAME's draw_sprites: 8 sprites in the video RAM, every 0x80 bytes */
    gal.active_sprites = 0;
    const uint8_t *vram = gal.ram + RAM_VIDEO;
    for (int i = 0; i < 8; i++)
    {
        const uint8_t *s = vram + 0x80 * i;
        if (!(s[0] & 0x01))
            continue;
        const int x = 240 - s[0x60], y = 240 - s[0x40] - (bt.bnj ? 0 : 1);
        gal_sprite_t *o = &gal.sprite[gal.active_sprites++];
        o->code = s[0x20];
        /* flags: 1 flip x, 2 flip y (upright) */
        o->flags = (uint8_t)(((s[0] & 0x02) ? 1 : 0) | ((s[0] & 0x04) ? 2 : 0));
        o->x = (int16_t)UP_X(y);
        o->y = (int16_t)UP_Y(x, 16);
    }
}

/* the characters: column-major, x = 31 - offs / 32; priority -1 all, else
 * only those whose code has bit 7 set (1) or clear (0) */
static void draw_chars(const gal_band_t *b, int opaque, int priority)
{
    const uint8_t *vram = gal.ram + RAM_VIDEO, *cram = gal.ram + RAM_COLOR;
    const int ry0 = b->x0 + 16, ry1 = b->x0 + b->w - 1 + 16;
    for (int ty = ry0 >> 3; ty <= ry1 >> 3 && ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++)
        {
            const int offs = (31 - tx) * 32 + ty;
            const int code = vram[offs] + 256 * (cram[offs] & 3);
            if (priority >= 0 && priority != ((code >> 7) & 1))
                continue;
            gal_draw_4bpp(b, UP_X(8 * ty), UP_Y(8 * tx, 8), 8, bt.chr + code * (CHR_BYTES / 4), 0, 0, bt.colors,
                          opaque);
        }
}

static void draw_sprites(const gal_band_t *b)
{
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *o = &gal.sprite[s];
        const uint32_t *rows = bt.spr + o->code * (SPR_BYTES / 4);
        gal_draw_4bpp(b, o->x, o->y, 16, rows, o->flags & 1, (o->flags & 2) != 0, bt.colors, 0);
        /* and 256 lines further, where it wraps */
        gal_draw_4bpp(b, o->x + 256, o->y, 16, rows, o->flags & 1, (o->flags & 2) != 0, bt.colors, 0);
    }
}

/* BurgerTime: four pages of 16x16 tiles from the map ROM, side by side
 * along raster x; the scroll is never set */
static void draw_bt_background(const gal_band_t *b)
{
    for (int i = 0; i < 4; i++)
    {
        const int page = ((1 + i) & 3) | (bt.scroll0 & 0x04);
        const uint8_t *map = bt.bgmap + 0x100 * page;
        for (int offs = 0; offs < 0x100; offs++)
        {
            const int x = 240 - (16 * (offs / 16) + 256 * i) - 1, y = 16 * (offs % 16);
            if (x < -15 || x > 255)
                continue;
            gal_draw_4bpp(b, UP_X(y), UP_Y(x, 16), 16, bt.bg + map[offs] * (SPR_BYTES / 4), 0, 0, bt.colors + 8, 1);
        }
    }
}

/* Bump 'n' Jump: a 512x256 picture of 16x16 tiles from the background RAM,
 * scrolled along raster x (MAME's screen_update_bnj) */
static void draw_bnj_background(const gal_band_t *b)
{
    const uint8_t *bg = gal.ram + RAM_BG;
    const int scroll = 767 - ((bt.scroll0 & 0x02) * 128 + 511 - bt.scroll1);
    for (int offs = 0x1ff; offs >= 0; offs--)
    {
        const int sx = 496 - 16 * ((offs < 0x100) ? ((offs % 0x80) / 8) : ((offs % 0x80) / 8) + 16);
        const int sy = 16 * (((offs % 0x100) < 0x80) ? offs % 8 : (offs % 8) + 8);
        const int code = (bg[offs] >> 4) + ((offs & 0x80) >> 3) + 32;
        int x = (sx + scroll) & 511;
        for (int k = 0; k < 2; k++, x -= 512)
            if (x > -16 && x < 256)
                gal_draw_4bpp(b, UP_X(sy), UP_Y(x, 16), 16, bt.bg + code * (SPR_BYTES / 4), 0, 0, bt.colors + 8, 1);
    }
}

static void bt_render_band(const gal_band_t *b)
{
    gal_fill_band(b, bt.colors[0]);
    if (bt.bnj)
    {
        if (bt.scroll0)
        {
            draw_bnj_background(b);
            draw_chars(b, 0, 1);
            draw_sprites(b);
            draw_chars(b, 0, 0);
        }
        else
        {
            draw_chars(b, 1, -1);
            draw_sprites(b);
        }
        return;
    }
    const int bg_on = (bt.scroll0 & 0x10) != 0;
    if (bg_on)
        draw_bt_background(b);
    draw_chars(b, !bg_on, -1);
    draw_sprites(b);
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t bt_hiscore[] = {
    {0x0033, 0x27, 0x00, 0xff},
};

static uint8_t bt_hs_read(uint16_t addr)
{
    return gal.ram[RAM_MAIN + (addr & 0x7ff)];
}

static void bt_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[RAM_MAIN + (addr & 0x7ff)] = value;
}

static const gal_hiscore_region_t bnj_hiscore[] = {
    {0x000a, 3, 0x12, 0x01},
    {0x0500, 0x26c, 0x01, 0x00},
    {0x0640, 1, 0x4d, 0x4d},
};

const gal_game_ops_t gal_bnj_ops = {
    .init = bnj_init,
    .reset = bt_reset,
    .run_slice = bt_run_slice,
    .prepare_frame = bt_prepare_frame,
    .render_band = bt_render_band,
    .sound = bt_sound,
    .view_top = 16,
    .view_h = 256,
    .large = 1,
    .nderived = 3,
    .derived_src = {2, 2, 3},
    .derived_size = {1024 * CHR_BYTES, 256 * SPR_BYTES, 64 * SPR_BYTES},
    .derive = bnj_derive,
    .hiscore = bnj_hiscore,
    .nhiscore = sizeof(bnj_hiscore) / sizeof(bnj_hiscore[0]),
    .hs_read = bt_hs_read,
    .hs_write = bt_hs_write,
};

const gal_game_ops_t gal_btime_ops = {
    .init = bt_init,
    .reset = bt_reset,
    .run_slice = bt_run_slice,
    .prepare_frame = bt_prepare_frame,
    .render_band = bt_render_band,
    .sound = bt_sound,
    .view_top = VIEW_TOP,
    .view_h = 240,
    .large = 1,
    .nderived = 3,
    .derived_src = {2, 2, 3},
    .derived_size = {1024 * CHR_BYTES, 256 * SPR_BYTES, 64 * SPR_BYTES},
    .derive = bt_derive,
    .hiscore = bt_hiscore,
    .nhiscore = 1,
    .hs_read = bt_hs_read,
    .hs_write = bt_hs_write,
};
