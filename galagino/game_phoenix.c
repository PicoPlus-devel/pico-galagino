/*
 * Phoenix (Amstar, 1980), the board of pico-phoenix: its 8085, its machine
 * (phoenix.c: memory map, VBLANK polling, the video RAM pages of the two
 * players) and its sound (phoenix_sound.c, tms36xx.c: MAME's discrete sound
 * and the TMS3615 melody) are used unchanged, with their state in the arena.
 * This file connects them to the core and draws the picture in bands.
 *
 * The machine runs a whole frame at the start of the first quarter, as
 * pico-phoenix does, sound included; the quarters hand that sound out. The
 * 256x208 raster is turned upright (MAME ROT90) into 208x256, drawn at
 * x 8..215 and y 16..271 of the 224x288 picture. B is the shield.
 */
#include "gal_internal.h"

#include "phoenix.h"

#define PIC_X0 8
#define PIC_X1 (PIC_X0 + PHX_RAW_HEIGHT)
#define VIEW_TOP 16

static struct
{
    phx_roms_t *roms;
    phx_gfx_t *gfx;
    phx_t *m;
    int16_t *audio; /* a frame of sound */
    int frame_samples, audio_pos;
} ph;

static int ph_init(uint8_t *const *regions)
{
    ph.roms = (phx_roms_t *)gal_alloc(sizeof(phx_roms_t));
    ph.gfx = (phx_gfx_t *)gal_alloc(sizeof(phx_gfx_t));
    ph.m = (phx_t *)gal_alloc(sizeof(phx_t));
    ph.frame_samples = gal.samplerate / 60;
    ph.audio = (int16_t *)gal_alloc(ph.frame_samples * sizeof(int16_t));
    if (!ph.roms || !ph.gfx || !ph.m || !ph.audio)
        return 0;
    memcpy(ph.roms->maincpu, regions[0], sizeof(ph.roms->maincpu));
    memcpy(ph.roms->bgtiles, regions[1], sizeof(ph.roms->bgtiles));
    memcpy(ph.roms->fgtiles, regions[2], sizeof(ph.roms->fgtiles));
    memcpy(ph.roms->proms, regions[3], sizeof(ph.roms->proms));
    static const phx_pixfmt_t fmt[] = {
        [GAL_FMT_RGB555] = PHX_FMT_RGB555, [GAL_FMT_RGB444] = PHX_FMT_RGB444, [GAL_FMT_RGB565] = PHX_FMT_RGB565};
    phx_gfx_init(ph.gfx, ph.roms, fmt[gal.fmt]);
    phx_init(ph.m, ph.roms, gal.samplerate, ph.frame_samples);
    return 1;
}

static void ph_reset(void)
{
    phx_reset(ph.m);
    ph.audio_pos = ph.frame_samples;
}

static void ph_run_slice(int q)
{
    if (q)
        return;
    const uint16_t keys = gal.buttons;
    uint8_t in = 0;
    if (keys & GAL_BTN_COIN)
        in |= PHX_IN_COIN;
    if (keys & GAL_BTN_START)
        in |= PHX_IN_START1;
    if (keys & GAL_BTN_START2)
        in |= PHX_IN_START2;
    if (keys & GAL_BTN_FIRE)
        in |= PHX_IN_FIRE;
    if (keys & GAL_BTN_LEFT)
        in |= PHX_IN_LEFT;
    if (keys & GAL_BTN_RIGHT)
        in |= PHX_IN_RIGHT;
    if (keys & GAL_BTN_EXTRA)
        in |= PHX_IN_SHIELD;
    ph.m->inputs = in;
    phx_run_frame(ph.m, ph.audio);
    ph.audio_pos = 0;
}

static void ph_sound(int16_t *out, int n)
{
    for (int i = 0; i < n; i++)
        out[i] = (ph.audio_pos < ph.frame_samples) ? ph.audio[ph.audio_pos++] : 0;
}

static void ph_prepare_frame(void)
{
    gal.active_sprites = 0;
}

/* Upright pixel (X, Y) is raw pixel (Y - 16, 215 - X): the foreground over
 * the background, which scrolls along raw x (pico-phoenix's phx_render). */
static void ph_render_band(const gal_band_t *b)
{
    const phx_video_t *v = &ph.m->video;
    const phx_gfx_t *g = ph.gfx;
    const int pb = v->palette_bank << 4;
    for (int j = 0; j < b->h; j++)
    {
        uint16_t *dst = b->buf + j * b->w;
        const int x = b->y0 + j - VIEW_TOP;
        if (x < 0 || x >= PHX_RAW_WIDTH)
        {
            for (int i = 0; i < b->w; i++)
                dst[i] = 0;
            continue;
        }
        const int tx = x >> 3, px = x & 7;
        const int bx = (x + v->scroll) & 0xff, btx = bx >> 3, bpx = bx & 7;
        for (int i = 0; i < b->w; i++)
        {
            const int X = b->x0 + i;
            if (X < PIC_X0 || X >= PIC_X1)
            {
                dst[i] = 0;
                continue;
            }
            const int y = PIC_X1 - 1 - X, ty = y >> 3, sh = 2 * (y & 7);
            const uint8_t fc = v->fg[ty * 32 + tx];
            const int f = (g->fg_cols[fc][px] >> sh) & 3;
            if (f)
                dst[i] = g->pal[((((fc >> 5) | 8 | pb) << 2)) + f];
            else
            {
                const uint8_t bc = v->bg[ty * 32 + btx];
                dst[i] = g->pal[(((bc >> 5) | pb) << 2) + ((g->bg_cols[bc][bpx] >> sh) & 3)];
            }
        }
    }
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it: the three scores
 * shown, a tile per digit, in the video RAM page the CPU sees. */
static uint8_t ph_hs_read(uint16_t addr)
{
    const uint8_t *page = ph.m->cpu.rd_page[addr >> 8];
    return page ? page[addr & 0xff] : 0;
}

static void ph_hs_write(uint16_t addr, uint8_t value)
{
    uint8_t *page = ph.m->cpu.wr_page[addr >> 8];
    if (page)
        page[addr & 0xff] = value;
}

static const gal_hiscore_region_t ph_hiscore[] = {
    {0x4389, 3, 0x00, 0x00}, {0x41e1, 1, 0x20, 0x20}, {0x41c1, 1, 0x20, 0x20}, {0x41a1, 1, 0x20, 0x20},
    {0x4181, 1, 0x20, 0x20}, {0x4161, 1, 0x20, 0x20}, {0x4141, 1, 0x20, 0x20}, {0x4381, 3, 0x00, 0x00},
    {0x4301, 1, 0x20, 0x20}, {0x42e1, 1, 0x20, 0x20}, {0x42c1, 1, 0x20, 0x20}, {0x42a1, 1, 0x20, 0x20},
    {0x4281, 1, 0x20, 0x20}, {0x4261, 1, 0x20, 0x20}, {0x4385, 3, 0x00, 0x00}, {0x40c1, 1, 0x20, 0x20},
    {0x40a1, 1, 0x20, 0x20}, {0x4081, 1, 0x20, 0x20}, {0x4061, 1, 0x20, 0x20}, {0x4041, 1, 0x20, 0x20},
    {0x4021, 1, 0x20, 0x20},
};

const gal_game_ops_t gal_phoenix_ops = {
    .init = ph_init,
    .reset = ph_reset,
    .run_slice = ph_run_slice,
    .prepare_frame = ph_prepare_frame,
    .render_band = ph_render_band,
    .sound = ph_sound,
    .view_top = VIEW_TOP,
    .view_h = PHX_RAW_WIDTH,
    .extra_button = 1,
    .hiscore = ph_hiscore,
    .nhiscore = sizeof(ph_hiscore) / sizeof(ph_hiscore[0]),
    .hs_read = ph_hs_read,
    .hs_write = ph_hs_write,
};
