/*
 * Galagino core: game life cycle, the game arena, and the band renderer that
 * puts the upright picture into the canvas in the three orientations.
 */
#include "gal_internal.h"

#include "romload.h"
#include "tileaddr.h"

gal_t gal = {.game = -1};

static const gal_game_ops_t *const game_ops[GAL_GAME_COUNT] = {
    [GAL_GAME_PACMAN] = &gal_pacman_ops,
    [GAL_GAME_GALAGA] = &gal_galaga_ops,
    [GAL_GAME_DKONG] = &gal_dkong_ops,
    [GAL_GAME_FROGGER] = &gal_frogger_ops,
    [GAL_GAME_DIGDUG] = &gal_digdug_ops,
    [GAL_GAME_1942] = &gal_1942_ops,
};

/* ---------------------------------------------------------------------------
 * Arenas
 * ------------------------------------------------------------------------ */

typedef struct
{
    uint8_t *mem;
    uint32_t size, lo, hi, peak;
} arena_t;

static uint8_t __attribute__((aligned(4))) sram_mem[GAL_ARENA_SIZE];
static arena_t sram = {sram_mem, GAL_ARENA_SIZE, 0, GAL_ARENA_SIZE, 0};
static arena_t psram;          /* empty until gal_set_psram */
static arena_t *data = &sram; /* the running game's */

static void note_peak(arena_t *a)
{
    const uint32_t used = a->lo + (a->size - a->hi);
    if (used > a->peak)
        a->peak = used;
}

static void *arena_alloc(arena_t *a, uint32_t size)
{
    size = (size + 3) & ~3u;
    if (size > a->hi - a->lo)
        return NULL;
    void *p = a->mem + a->lo;
    a->lo += size;
    note_peak(a);
    return p;
}

void *gal_alloc(uint32_t size)
{
    return arena_alloc(data, size);
}

void *gal_fast_alloc(uint32_t size)
{
    return arena_alloc(&sram, size);
}

void *gal_temp_alloc(uint32_t size)
{
    size = (size + 3) & ~3u;
    if (size > data->hi - data->lo)
        return NULL;
    data->hi -= size;
    note_peak(data);
    return data->mem + data->hi;
}

void gal_temp_release(void)
{
    data->hi = data->size;
}

void gal_arena_reset(void)
{
    sram.lo = psram.lo = 0;
    sram.hi = sram.size;
    psram.hi = psram.size;
}

void gal_set_psram(void *mem, uint32_t size)
{
    psram.mem = (uint8_t *)mem;
    psram.size = mem ? size : 0;
    psram.lo = psram.peak = 0;
    psram.hi = psram.size;
}

uint32_t gal_arena_peak(void)
{
    return sram.peak;
}

uint32_t gal_arena_size(void)
{
    return sram.size;
}

uint32_t gal_psram_peak(void)
{
    return psram.peak;
}

uint32_t gal_psram_size(void)
{
    return psram.size;
}

/* ---------------------------------------------------------------------------
 * Life cycle
 * ------------------------------------------------------------------------ */

void gal_init(gal_fmt_t fmt, int samplerate)
{
    gal.fmt = fmt;
    gal.samplerate = samplerate;
    gal.game = -1;
}

uint32_t gal_probe(gal_game_id_t game, const gal_io_t *io, const char *dir)
{
    return gal_romload(&gal_games[game], NULL, io, dir, 0);
}

#ifdef GAL_CONV_CHECK
int gal_tables(gal_table_t *t)
{
    static int (*const fn[GAL_GAME_COUNT])(gal_table_t *) = {
        [GAL_GAME_PACMAN] = gal_tables_pacman, [GAL_GAME_GALAGA] = gal_tables_galaga,
        [GAL_GAME_DKONG] = gal_tables_dkong,   [GAL_GAME_FROGGER] = gal_tables_frogger,
        [GAL_GAME_DIGDUG] = gal_tables_digdug, [GAL_GAME_1942] = gal_tables_1942,
    };
    return gal.game >= 0 ? fn[gal.game](t) : 0;
}
#endif

int gal_supported(gal_game_id_t game)
{
    return game >= 0 && game < GAL_GAME_COUNT && game_ops[game] != NULL;
}

int gal_needs_psram(gal_game_id_t game)
{
    return gal_supported(game) && game_ops[game]->needs_psram;
}

const gal_game_ops_t *gal_ops(int game)
{
    return (game >= 0 && game < GAL_GAME_COUNT) ? game_ops[game] : NULL;
}

static int in_flash_store(int game)
{
    const uint8_t *parts[GAL_MAX_PARTS];
    return gal_flash_parts(game, parts);
}

int gal_available(gal_game_id_t game)
{
    return gal_supported(game) && (!game_ops[game]->needs_psram || psram.size > 0 || in_flash_store(game));
}

int gal_set_complete(gal_game_id_t game, uint32_t found)
{
    const int n = gal_games[game].nfiles;
    return found == ((n >= 32) ? 0xffffffffu : ((1u << n) - 1));
}

void gal_stop(void)
{
    gal.game = -1;
    gal.ops = NULL;
    gal_arena_reset();
}

int gal_running(void)
{
    return gal.game;
}

uint32_t gal_start(gal_game_id_t game, const gal_io_t *io, const char *dir)
{
    gal_stop();
    const gal_game_info_t *g = &gal_games[game];
    const gal_game_ops_t *ops = game_ops[game];
    if (!gal_available(game))
        return 0;

    sram.peak = psram.peak = 0;
    uint8_t *regions[GAL_MAX_PARTS] = {0};
    const uint8_t *parts[GAL_MAX_PARTS];
    uint32_t found;
    if (gal_flash_parts(game, parts))
    {
        /* From the flash store. The regions a game only converts are read in
         * place; its program ROMs are copied to SRAM, where the CPUs read them
         * fast and the game may patch them. A game that needs PSRAM takes
         * everything in place and copies what it needs itself. */
        data = &sram;
        for (int r = 0; r < g->nregions; r++)
        {
            if ((g->temp_mask & (1u << r)) || ops->needs_psram)
                regions[r] = (uint8_t *)parts[r];
            else if ((regions[r] = (uint8_t *)gal_alloc(g->region_size[r])) != NULL)
                memcpy(regions[r], parts[r], g->region_size[r]);
            else
            {
                gal_arena_reset();
                return 0;
            }
        }
        for (int d = 0; d < ops->nderived; d++)
            regions[GAL_MAX_REGIONS + d] = (uint8_t *)parts[GAL_MAX_REGIONS + d];
        found = (g->nfiles >= 32) ? 0xffffffffu : ((1u << g->nfiles) - 1);
    }
    else
    {
        data = ops->needs_psram ? &psram : &sram;
        for (int r = 0; r < g->nregions; r++)
        {
            regions[r] = (g->temp_mask & (1u << r)) ? (uint8_t *)gal_temp_alloc(g->region_size[r])
                                                     : (uint8_t *)gal_alloc(g->region_size[r]);
            if (!regions[r])
            {
                gal_arena_reset();
                return 0;
            }
            memset(regions[r], 0, g->region_size[r]);
        }
        found = gal_romload(g, regions, io, dir, 0);
        if (!gal_set_complete(game, found))
        {
            gal_arena_reset();
            return found;
        }
    }

    /* a fresh machine; the configuration survives */
    const gal_fmt_t fmt = gal.fmt;
    const int samplerate = gal.samplerate;
    memset(&gal, 0, sizeof(gal));
    gal.fmt = fmt;
    gal.samplerate = samplerate;
    gal.game = -1;

    if (!ops->init(regions))
    {
        gal_arena_reset();
        return found;
    }
    gal_temp_release();
    gal.ops = ops;
    gal.game = game;
    ops->reset();
    return found;
}

void gal_reset(void)
{
    if (gal.ops)
        gal.ops->reset();
}

void GAL_HOT(gal_run_frame)(uint16_t buttons, int16_t *audio, int n)
{
    if (!gal.ops)
    {
        memset(audio, 0, sizeof(int16_t) * n);
        return;
    }
    gal.buttons = buttons;
    int done = 0;
    for (int q = 0; q < 4; q++)
    {
        gal.ops->run_slice(q);
        const int upto = (n * (q + 1)) / 4;
        gal.ops->sound(audio + done, upto - done);
        done = upto;
    }
}

/* ---------------------------------------------------------------------------
 * Video
 *
 * A game uses rows view_top .. view_top + view_h - 1 of the upright 224x288
 * picture (all 288 on the Namco boards, 256 on the Galaxian-class ones).
 *
 * Upright (Tate off): those rows become 240 by dropping every sixth row (288)
 * or every sixteenth (256); the picture sits at x = 48. Bands are 224x8, top
 * to bottom.
 *
 * Upright full (HSTX): the same bands, no row dropped, on a canvas `stride`
 * wide and view_h tall; the display scales the rows to its height.
 *
 * Tate: the raw raster, view_h x 224, pixel exact, centred. A canvas row is a
 * column of the upright picture, so the bands are upright columns, 8 x view_h,
 * transposed into eight canvas rows each (y = uy - view_top):
 *   Bottom left (CW):   canvas row 8 + (223 - ux), canvas x x0 + y
 *   Bottom right (CCW): canvas row 8 + ux,         canvas x x0 + (view_h - 1 - y)
 * The bands run in the order that writes the canvas top to bottom.
 * ------------------------------------------------------------------------ */

#define UPRIGHT_X0 ((GAL_CANVAS_W - GAL_W) / 2) /* 48 */
#define TATE_Y0 ((GAL_CANVAS_H - GAL_W) / 2)    /* 8 */

static uint16_t __attribute__((aligned(4))) band_buf[8 * GAL_H];

static void view_of(int *top, int *h)
{
    if (gal.ops)
    {
        *top = gal.ops->view_top;
        *h = gal.ops->view_h;
    }
    else
    {
        *top = 0;
        *h = GAL_H; /* the menu */
    }
}

/* every: the row dropped out of `every`, or 0 to keep them all */
static void GAL_HOT(put_band_upright)(const gal_band_t *b, int top, int every, uint16_t *fb, int stride, int x0)
{
    for (int i = 0; i < b->h; i++)
    {
        int y = b->y0 + i - top;
        if (every)
        {
            if (y % every == every - 1)
                continue;
            y -= y / every;
        }
        uint16_t *dst = fb + y * stride + x0;
        const uint16_t *src = b->buf + i * b->w;
        if (((uintptr_t)dst | (uintptr_t)src) & 3)
        {
            for (int x = 0; x < GAL_W; x++)
                dst[x] = src[x];
            continue;
        }
        /* the usual case, both word aligned: two pixels at a time, four
         * words per pass (GAL_W / 2 is a multiple of 4) */
        uint32_t *d = (uint32_t *)dst;
        const uint32_t *s = (const uint32_t *)src;
        for (int x = 0; x < GAL_W / 2; x += 4)
        {
            const uint32_t w0 = s[x], w1 = s[x + 1], w2 = s[x + 2], w3 = s[x + 3];
            d[x] = w0;
            d[x + 1] = w1;
            d[x + 2] = w2;
            d[x + 3] = w3;
        }
    }
}

static void GAL_HOT(put_band_tate)(const gal_band_t *b, gal_orient_t orient, uint16_t *fb, int stride)
{
    const int h = b->h, x0 = (GAL_CANVAS_W - h) / 2;
    for (int k = 0; k < 8; k++)
    {
        /* canvas rows top to bottom */
        const int col = (orient == GAL_ORIENT_TATE_CW) ? 7 - k : k;
        const int ux = b->x0 + col;
        const uint16_t *src = b->buf + col;
        if (orient == GAL_ORIENT_TATE_CW)
        {
            uint16_t *dst = fb + (TATE_Y0 + 223 - ux) * stride + x0;
            for (int y = 0; y < h; y++, src += 8)
                dst[y] = *src;
        }
        else
        {
            uint16_t *dst = fb + (TATE_Y0 + ux) * stride + x0 + h - 1;
            for (int y = 0; y < h; y++, src += 8)
                *dst-- = *src;
        }
    }
}

void GAL_HOT(gal_render)(gal_orient_t orient, uint16_t *fb, int stride)
{
    if (!gal.ops)
        return;
    gal.ops->prepare_frame();

    int top, h;
    view_of(&top, &h);
    gal_band_t b = {.buf = band_buf};
    if (orient == GAL_ORIENT_UPRIGHT || orient == GAL_ORIENT_UPRIGHT_FULL)
    {
        const int full = (orient == GAL_ORIENT_UPRIGHT_FULL);
        const int every = full ? 0 : (h == GAL_H) ? 6 : 16;
        const int x0 = full ? (stride - GAL_W) / 2 : UPRIGHT_X0;
        b.x0 = 0;
        b.w = GAL_W;
        b.h = 8;
        for (b.y0 = top; b.y0 < top + h; b.y0 += 8)
        {
            gal.ops->render_band(&b);
            put_band_upright(&b, top, every, fb, stride, x0);
        }
    }
    else
    {
        b.y0 = top;
        b.w = 8;
        b.h = h;
        for (int k = 0; k < GAL_W / 8; k++)
        {
            const int c = (orient == GAL_ORIENT_TATE_CW) ? GAL_W / 8 - 1 - k : k;
            b.x0 = 8 * c;
            gal.ops->render_band(&b);
            put_band_tate(&b, orient, fb, stride);
        }
    }
}

static void fill_rect(uint16_t *fb, int stride, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
    {
        uint16_t *p = fb + (y + j) * stride + x;
        for (int i = 0; i < w; i++)
            p[i] = 0;
    }
}

int gal_view_height(void)
{
    int top, h;
    view_of(&top, &h);
    return h;
}

void gal_render_borders(gal_orient_t orient, uint16_t *fb, int stride)
{
    int top, h;
    view_of(&top, &h);
    (void)top;
    if (orient == GAL_ORIENT_UPRIGHT)
    {
        fill_rect(fb, stride, 0, 0, UPRIGHT_X0, GAL_CANVAS_H);
        fill_rect(fb, stride, UPRIGHT_X0 + GAL_W, 0, GAL_CANVAS_W - UPRIGHT_X0 - GAL_W, GAL_CANVAS_H);
    }
    else if (orient == GAL_ORIENT_UPRIGHT_FULL)
    {
        const int x0 = (stride - GAL_W) / 2;
        fill_rect(fb, stride, 0, 0, x0, h);
        fill_rect(fb, stride, x0 + GAL_W, 0, stride - x0 - GAL_W, h);
    }
    else
    {
        const int x0 = (GAL_CANVAS_W - h) / 2;
        fill_rect(fb, stride, 0, 0, GAL_CANVAS_W, TATE_Y0);
        fill_rect(fb, stride, 0, TATE_Y0 + GAL_W, GAL_CANVAS_W, GAL_CANVAS_H - TATE_Y0 - GAL_W);
        fill_rect(fb, stride, 0, TATE_Y0, x0, GAL_W);
        fill_rect(fb, stride, x0 + h, TATE_Y0, GAL_CANVAS_W - x0 - h, GAL_W);
    }
}
