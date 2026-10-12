/*
 * SNK's 6502 board: Vanguard (SNK, 1981), Fantasy (SNK, 1981; Rock-Ola's US
 * set) and Nibbler (Rock-Ola, 1982), after MAME's snk/snk6502.cpp,
 * snk6502_v.cpp and snk6502_a.cpp.
 *
 * A 6502 at 1.41 MHz with an IRQ every frame; a coin is an NMI. 256x224
 * shown (MAME ROT90: the upright picture is 224x256, drawn at y 16..271).
 * A background layer of characters from ROM, scrolled as a whole, with a
 * background colour of the game's choice, and over it a layer of characters
 * the game draws into RAM; two bits per pixel, a palette of 64 colours from
 * PROMs.
 *
 * The music is SNK's custom tone generator: two (Fantasy and Nibbler: three)
 * channels stepping through tunes in ROM, each playing a 16-step waveform.
 * The SN76477 effects and the speech chip are not emulated.
 *
 * Vanguard has four fire buttons, one per direction: A fires in each in turn
 * while held, as in GalaginoPlus.
 */
#include "gal_internal.h"

#include "m6502.h"

#define CYCLES_PER_FRAME 23581 /* 1.41 MHz at 59.84 Hz */
#define VIEW_TOP 16

typedef enum
{
    SK_VANGUARD,
    SK_FANTASY,
    SK_NIBBLER,
} sk_var_t;

/* Galagino's RAM buffer: 0000-1fff as the CPU sees it */
#define RAM_FG 0x0400    /* the foreground's characters */
#define RAM_BG 0x0800    /* the background's */
#define RAM_COLOR 0x0c00 /* colours of both */
#define RAM_CHAR 0x1000  /* the foreground's graphics */

/* MAME's tone generator */
#define FRAC_BITS 16
#define FRAC_ONE (1 << FRAC_BITS)
#define FRAC_MASK (FRAC_ONE - 1)
#define NUM_CHANNELS 3

typedef struct
{
    int mute, offset, base, mask;
    int32_t sample_rate, sample_step, sample_cur;
    int16_t form[16];
} tone_t;

static struct
{
    sk_var_t var;
    m6502_t *cpu;
    uint8_t *rom;          /* from 3000 (Vanguard 4000) to bfff */
    const uint8_t *music;  /* the tone generator's ROM */
    int music_size;
    uint16_t *bg_tiles;    /* [512][8] */
    uint16_t *fg_tiles;    /* [256][8], decoded from the RAM as it changes */
    uint8_t fg_dirty[32];  /* a bit per character */
    uint16_t pal[64];
    uint8_t backcolor, charbank;
    uint8_t scroll_x, scroll_y;
    uint8_t coin, fire_dir;
    tone_t tone[NUM_CHANNELS];
    int32_t tone_clock, tone_clock_expire;
    uint8_t stop_on_rollover;
} sk;

/* --- The tone generator (MAME's snk6502_sound_device) ------------------------------- */

static void build_waveform(int channel, int mask)
{
    int bit0 = 0, bit1 = 0, bit2 = 0, bit3 = 0;
    if ((mask & 1) || (mask & 2))
        bit3 = 8;
    else if (mask & 4)
        bit3 = 4;
    else if (mask & 8)
        bit3 = 2;
    if (mask & 4)
        bit2 = 8;
    else if ((mask & 2) || (mask & 8))
        bit2 = 4;
    if (mask & 8)
        bit1 = 8;
    else if (mask & 4)
        bit1 = 4;
    else if (mask & 2)
        bit1 = 2;
    bit0 = bit1 >> 1;
    if (bit0 + bit1 + bit2 + bit3 < 16)
    {
        bit0 <<= 1;
        bit1 <<= 1;
        bit2 <<= 1;
        bit3 <<= 1;
    }
    const int base = (bit0 + bit1 + bit2 + bit3 + 1) / 2;
    for (int i = 0; i < 16; i++)
    {
        int v;
        if (channel == 2) /* Fantasy's special channel */
            v = (i & 8) ? 7 : -8;
        else
            v = ((i & 1) ? bit0 : 0) + ((i & 2) ? bit1 : 0) + ((i & 4) ? bit2 : 0) + ((i & 8) ? bit3 : 0) - base;
        sk.tone[channel].form[i] = (int16_t)(v * (65535 / 160));
    }
}

static void set_music_freq(int freq)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        tone_t *t = &sk.tone[i];
        t->mute = 1;
        t->offset = 0;
        t->base = i * 0x800;
        t->mask = 0xff;
        t->sample_step = 0;
        t->sample_cur = 0;
        t->sample_rate = (int32_t)((double)(freq * 8) / gal.samplerate * FRAC_ONE);
        build_waveform(i, 1);
    }
}

static void validate_tone_channel(int channel)
{
    tone_t *t = &sk.tone[channel];
    if (t->mute)
        return;
    const int a = t->base + t->offset;
    const uint8_t romdata = a < sk.music_size ? sk.music[a] : 0xff;
    t->sample_step = (romdata != 0xff) ? t->sample_rate / (256 - romdata) : 0;
}

static void mute_channel(int c)
{
    sk.tone[c].mute = 1;
    sk.tone[c].offset = 0;
}

static void unmute_channel(int c)
{
    if (sk.tone[c].mute)
        sk.tone[c].offset = 0;
    sk.tone[c].mute = 0;
}

static void sound_w(int offset, uint8_t data)
{
    if (sk.var == SK_VANGUARD)
    {
        switch (offset)
        {
        case 0:
            sk.tone[0].base = (data & 7) << 8;
            sk.stop_on_rollover = 1;
            if (data & 0x08)
                mute_channel(0);
            if (data & 0x10)
                unmute_channel(0);
            break;
        case 1:
            sk.tone[1].base = 0x800 | ((data & 7) << 8);
            if (data & 0x08)
                unmute_channel(1);
            else
                mute_channel(1);
            break;
        case 2:
            build_waveform(0, (data & 3) | ((data & 4) << 1) | ((data & 8) >> 1));
            build_waveform(1, data >> 4);
            break;
        default:
            break;
        }
        return;
    }
    switch (offset)
    {
    case 0:
        sk.tone[0].base = (data & 7) << 8;
        sk.stop_on_rollover = 0;
        if (data & 0x08)
            unmute_channel(0);
        else
            mute_channel(0);
        if (data & 0x10)
            unmute_channel(2);
        else
            mute_channel(2);
        break;
    case 1:
        sk.tone[1].base = 0x800 | ((data & 7) << 8);
        if (data & 0x08)
            unmute_channel(1);
        else
            mute_channel(1);
        break;
    case 2:
        build_waveform(0, (data & 9) | ((data & 2) << 1) | ((data & 4) >> 1));
        build_waveform(1, data >> 4);
        break;
    case 3:
        sk.tone[2].base = 0x1000 | ((data & 0x70) << 4);
        break;
    default:
        break;
    }
}

static void sk_sound(int16_t *out, int n)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
        validate_tone_channel(i);
    for (int s = 0; s < n; s++)
    {
        int32_t data = 0;
        for (int i = 0; i < NUM_CHANNELS; i++)
        {
            tone_t *t = &sk.tone[i];
            if (t->mute || !t->sample_step)
                continue;
            const int cur_pos = t->sample_cur + t->sample_step;
            const int prev = t->form[(t->sample_cur >> FRAC_BITS) & 15];
            const int cur = t->form[(cur_pos >> FRAC_BITS) & 15];
            data += (prev * (FRAC_ONE - (cur_pos & FRAC_MASK)) + cur * (cur_pos & FRAC_MASK)) >> FRAC_BITS;
            t->sample_cur = cur_pos;
        }
        data *= 2;
        out[s] = (int16_t)(data > 32767 ? 32767 : (data < -32768 ? -32768 : data));
        sk.tone_clock += FRAC_ONE;
        if (sk.tone_clock >= sk.tone_clock_expire)
        {
            for (int i = 0; i < NUM_CHANNELS; i++)
            {
                sk.tone[i].offset = (sk.tone[i].offset + 1) & sk.tone[i].mask;
                validate_tone_channel(i);
            }
            if (sk.tone[0].offset == 0 && sk.stop_on_rollover)
                sk.tone[0].mute = 1;
            sk.tone_clock -= sk.tone_clock_expire;
        }
    }
}

/* --- CPU -------------------------------------------------------------------------- */

static uint8_t sk_read(m6502_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    const uint16_t io = (sk.var == SK_VANGUARD) ? 0x3100 : 0x2100;
    uint8_t v = 0;
    switch (addr - io)
    {
    case 4: /* IN0, active high: the joystick, the fire buttons */
        if (keys & GAL_BTN_DOWN)
            v |= 0x10;
        if (keys & GAL_BTN_UP)
            v |= 0x20;
        if (keys & GAL_BTN_RIGHT)
            v |= 0x40;
        if (keys & GAL_BTN_LEFT)
            v |= 0x80;
        if (keys & GAL_BTN_FIRE)
        {
            if (sk.var == SK_VANGUARD)
                v |= (uint8_t)(1u << sk.fire_dir);
            else if (sk.var == SK_FANTASY)
                v |= 0x08;
        }
        return v;
    case 6: /* DSW: MAME's defaults */
        return 0x00;
    case 7: /* IN2: the starts; Vanguard: is music 0 silent */
        if (keys & GAL_BTN_START)
            v |= 0x80;
        if (keys & GAL_BTN_START2)
            v |= 0x40;
        if (keys & GAL_BTN_COIN)
            v |= 0x02;
        if (sk.var == SK_VANGUARD && sk.tone[0].mute)
            v |= 0x10;
        return v;
    default:
        return 0; /* 5: IN1, the cocktail cabinet's second player */
    }
}

static void sk_write(m6502_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    if (addr >= 0x1000 && addr < 0x2000)
    {
        /* the foreground's graphics: decoded again when drawn */
        uint8_t *p = gal.ram + RAM_CHAR + (addr & 0xfff);
        if (*p != value)
        {
            *p = value;
            const int ch = ((addr & 0xfff) / 8) & 0xff;
            sk.fg_dirty[ch >> 3] |= (uint8_t)(1u << (ch & 7));
        }
        return;
    }
    const uint16_t io = (sk.var == SK_VANGUARD) ? 0x3100 : 0x2100;
    if (addr >= io && addr < io + 3)
        sound_w(addr - io, value);
    else if (addr == io + 3)
    {
        if (sk.var != SK_VANGUARD)
            sound_w(3, value);
        /* bits 0-2 the background colour, bit 3 (inverted) the character
         * bank; bit 7 the flip of a cocktail cabinet */
        sk.backcolor = value & 7;
        sk.charbank = (uint8_t)((~value & 0x08) >> 3);
    }
    else if (addr == io + 0x100)
        sk.scroll_x = value;
    else if (addr == io + 0x200)
        sk.scroll_y = value;
    /* the CRTC, the speech chip */
}

/* --- Set-up ----------------------------------------------------------------------- */

/* the background's characters: two planes in the two halves of the ROMs,
 * the leftmost pixel in the top bit */
static gal_layout_t bg_layout(int half)
{
    gal_layout_t l = {8, 8, 2, {0, (uint32_t)half * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 64};
    return l;
}

static int sk_init_common(uint8_t *const *regions, sk_var_t var, int rom_base, int rom_size, int gfx_size)
{
    memset(&sk, 0, sizeof(sk));
    sk.var = var;
    sk.cpu = (m6502_t *)gal_alloc(sizeof(m6502_t));
    sk.bg_tiles = (uint16_t *)gal_alloc(512 * 8 * 2);
    sk.fg_tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    if (!sk.cpu || !sk.bg_tiles || !sk.fg_tiles)
        return 0;
    sk.rom = regions[0];
    sk.music = regions[3];
    sk.music_size = (var == SK_VANGUARD) ? 0x1000 : 0x1800;

    const int n = gfx_size / 16; /* 256 or 512 characters */
    const gal_layout_t l = bg_layout(gfx_size / 2);
    gal_conv_tiles8(regions[1], &l, n, GAL_ROT90, sk.bg_tiles);
    if (n < 512)
        memcpy(sk.bg_tiles + 256 * 8, sk.bg_tiles, 256 * 8 * 2);
    memset(sk.fg_dirty, 0xff, sizeof(sk.fg_dirty));

    /* MAME's snk6502_palette */
    const uint8_t *prom = regions[2];
    for (int i = 0; i < 64; i++)
    {
        const uint8_t c = prom[i];
        const int r = 0x21 * (c & 1) + 0x47 * ((c >> 1) & 1) + 0x97 * ((c >> 2) & 1);
        const int g = 0x21 * ((c >> 3) & 1) + 0x47 * ((c >> 4) & 1) + 0x97 * ((c >> 5) & 1);
        const int b = 0x52 * ((c >> 6) & 1) + 0xad * ((c >> 7) & 1);
        sk.pal[i] = gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }

    m6502_t *c = sk.cpu;
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 0x10; p++)
        c->rd_page[p] = c->wr_page[p] = gal.ram + 256 * p; /* 0000-0fff */
    for (int p = 0; p < 0x10; p++)
        c->rd_page[0x10 + p] = gal.ram + RAM_CHAR + 256 * p; /* 1000-1fff, written through sk_write */
    for (int p = 0; p < rom_size / 256; p++)
        c->rd_page[rom_base / 256 + p] = sk.rom + 256 * p;
    /* f000-ffff: the ROM at 8000 again, for the vectors (ROM_RELOAD) */
    for (int p = 0; p < 0x10; p++)
        c->rd_page[0xf0 + p] = sk.rom + (0x8000 - rom_base) + 256 * p;
    c->read = sk_read;
    c->write = sk_write;
    return 1;
}

static int vanguard_init(uint8_t *const *regions)
{
    return sk_init_common(regions, SK_VANGUARD, 0x4000, 0x8000, 0x1000);
}

static int fantasy_init(uint8_t *const *regions)
{
    return sk_init_common(regions, SK_FANTASY, 0x3000, 0x9000, 0x2000);
}

static int nibbler_init(uint8_t *const *regions)
{
    return sk_init_common(regions, SK_NIBBLER, 0x3000, 0x9000, 0x2000);
}

static void sk_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(sk.fg_dirty, 0xff, sizeof(sk.fg_dirty));
    sk.backcolor = 0;
    sk.charbank = 1;
    sk.scroll_x = sk.scroll_y = 0;
    sk.coin = sk.fire_dir = 0;
    set_music_freq(43000);
    /* the music clock: Vanguard 38.99 Hz (from the schematic), Fantasy and
     * Nibbler 41.6 Hz (measured) */
    const double clock_time = (sk.var == SK_VANGUARD) ? 0.693147 * (18000.0 * 2 + 1000) * 1e-6 : 1 / 41.6;
    sk.tone_clock_expire = (int32_t)(clock_time * gal.samplerate * FRAC_ONE);
    sk.tone_clock = 0;
    sk.stop_on_rollover = 0;
    m6502_reset(sk.cpu);
    sk.cpu->icount = 0;
}

static void sk_run_slice(int q)
{
    if (q == 0)
    {
        /* a coin: the NMI comes when it is released */
        const uint8_t coin = (gal.buttons & GAL_BTN_COIN) != 0;
        if (!coin && sk.coin)
            sk.cpu->nmi = 1;
        sk.coin = coin;
        sk.fire_dir = (gal.buttons & GAL_BTN_FIRE) ? (uint8_t)((sk.fire_dir + 1) & 3) : 0;
    }
    m6502_run(sk.cpu, (q + 1) * CYCLES_PER_FRAME / 4 - q * CYCLES_PER_FRAME / 4);
    if (q == 3)
        sk.cpu->irq = sk.cpu->irq_hold = 1; /* one IRQ per frame */
}

/* --- Video ------------------------------------------------------------------------ */

static void sk_prepare_frame(void)
{
    gal.active_sprites = 0;
    /* the foreground's characters the game changed */
    static const gal_layout_t fg_layout = {
        8, 8, 2, {0, 256 * 8 * 8}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 8, 16, 24, 32, 40, 48, 56}, 64};
    for (int k = 0; k < 32; k++)
    {
        if (!sk.fg_dirty[k])
            continue;
        for (int b = 0; b < 8; b++)
            if (sk.fg_dirty[k] & (1u << b))
            {
                const int ch = 8 * k + b;
                gal_conv_tiles8(gal.ram + RAM_CHAR + 8 * ch, &fg_layout, 1, GAL_ROT90, sk.fg_tiles + 8 * ch);
            }
        sk.fg_dirty[k] = 0;
    }
}

/* MAME ROT90: the raster's pixel (x, y) is the upright pixel (223 - y,
 * x + 16), so an 8x8 element at (x, y) has its upright corner at
 * (216 - y, x + 16) */
#define UP_X(y) (216 - (y))
#define UP_Y(x) ((x) + VIEW_TOP)

static void sk_render_band(const gal_band_t *b)
{
    const uint8_t *bg = gal.ram + RAM_BG, *fg = gal.ram + RAM_FG, *cram = gal.ram + RAM_COLOR;
    const int ry0 = 223 - (b->x0 + b->w - 1), ry1 = 223 - b->x0;
    int rx0 = b->y0 - VIEW_TOP, rx1 = b->y0 + b->h - 1 - VIEW_TOP;
    if (rx0 < 0)
        rx0 = 0;
    if (rx1 > 255)
        rx1 = 255;
    gal_fill_band(b, 0);
    /* the background, scrolled; pen 0 is the background colour */
    uint16_t colors[4];
    for (int y = ry0 - ((ry0 + sk.scroll_y) & 7); y <= ry1; y += 8)
    {
        const int row = ((y + sk.scroll_y) & 255) >> 3;
        for (int x = rx0 - ((rx0 + sk.scroll_x) & 7); x <= rx1; x += 8)
        {
            const int i = row * 32 + (((x + sk.scroll_x) & 255) >> 3);
            const int c = (cram[i] & 0x38) >> 3;
            colors[0] = sk.pal[4 * sk.backcolor + 0x20];
            colors[1] = sk.pal[0x20 + 4 * c + 1];
            colors[2] = sk.pal[0x20 + 4 * c + 2];
            colors[3] = sk.pal[0x20 + 4 * c + 3];
            gal_draw_tile_opaque(b, UP_X(y), UP_Y(x), sk.bg_tiles + 8 * (bg[i] + 256 * sk.charbank), colors);
        }
    }
    /* the foreground, pen 0 transparent */
    for (int row = ry0 >> 3; row <= ry1 >> 3; row++)
        for (int col = rx0 >> 3; col <= rx1 >> 3; col++)
        {
            const int i = row * 32 + col;
            gal_draw_tile(b, UP_X(8 * row), UP_Y(8 * col), sk.fg_tiles + 8 * fg[i], sk.pal + 4 * (cram[i] & 7));
        }
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t vanguard_hiscore[] = {{0x0220, 0x70, 0x11, 0x30}, {0x0025, 3, 0x00, 0x00}};
static const gal_hiscore_region_t fantasy_hiscore[] = {{0x0025, 3, 0x00, 0x00}, {0x0220, 0x30, 0x11, 0x30}};
static const gal_hiscore_region_t nibbler_hiscore[] = {{0x0290, 0x28, 0x00, 0x00}, {0x02d0, 0x1e, 0x13, 0x14}};

static uint8_t sk_hs_read(uint16_t addr)
{
    return gal.ram[addr & 0x0fff];
}

static void sk_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[addr & 0x0fff] = value;
}

#define N(a) (sizeof(a) / sizeof((a)[0]))
#define SK_OPS(init_fn, hs)                                                                         \
    {                                                                                              \
        .init = init_fn, .reset = sk_reset, .run_slice = sk_run_slice, .prepare_frame = sk_prepare_frame, \
        .render_band = sk_render_band, .sound = sk_sound, .view_top = VIEW_TOP, .view_h = 256,      \
        .hiscore = hs, .nhiscore = N(hs), .hs_read = sk_hs_read, .hs_write = sk_hs_write,           \
    }

const gal_game_ops_t gal_vanguard_ops = SK_OPS(vanguard_init, vanguard_hiscore);
const gal_game_ops_t gal_fantasy_ops = SK_OPS(fantasy_init, fantasy_hiscore);
const gal_game_ops_t gal_nibbler_ops = SK_OPS(nibbler_init, nibbler_hiscore);
