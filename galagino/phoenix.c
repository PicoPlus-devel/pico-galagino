// license:BSD-3-Clause
// copyright-holders:Richard Davies
/*
 * Phoenix machine and video, ported from MAME's phoenix.cpp / phoenix_v.cpp.
 * See phoenix.h.
 */
#include "phoenix.h"

#include <string.h>

#include "phx_port.h"

/* DSW0 defaults for `phoenix`: 3 lives, bonus 3K/30K, 1 coin 1 credit, the two
 * unknown switches off. Bit 7 is VBLANK, active low. */
#define PHX_DSW0_DEFAULT 0x60

/* ---------------------------------------------------------------------------
 * Memory map
 *
 *   0000-3fff  ROM
 *   4000-4fff  video RAM, 2 pages selected by bit 0 of the video register
 *   5000-53ff  video register (bit 0 page, bit 1 palette bank)
 *   5800-5bff  background scroll
 *   6000-63ff  sound control A
 *   6800-6bff  sound control B
 *   7000-73ff  IN0
 *   7800-7bff  DSW0
 * ------------------------------------------------------------------------ */

static void map_vram_page(phx_t *m)
{
    for (int p = 0; p < 16; p++)
    {
        m->cpu.rd_page[0x40 + p] = &m->vram[m->vram_page][p << 8];
        m->cpu.wr_page[0x40 + p] = &m->vram[m->vram_page][p << 8];
    }
}

/* Bring the sound stream up to the CPU's current position in the frame. */
static void PHX_HOT(sound_catchup)(phx_t *m)
{
    if (!m->audio)
        return;
    int cycle = m->slice_end - m->cpu.icount;
    if (cycle < 0)
        cycle = 0;
    int target = (int)(((int64_t)cycle * m->audio_samples) / PHX_CYCLES_PER_FRAME);
    if (target > m->audio_samples)
        target = m->audio_samples;
    if (target > m->audio_pos)
    {
        phx_sound_render(&m->sound, m->audio + m->audio_pos, target - m->audio_pos);
        m->audio_pos = target;
    }
}

static uint8_t PHX_HOT(mem_read)(i8085_t *cpu, uint16_t addr)
{
    phx_t *m = (phx_t *)cpu->user;
    switch (addr & 0xfc00)
    {
    case 0x7000:
    {
        /* IN0: coin, start1, start2 (active low), bit 3 unused (high), then
         * the upright player's controls from CTRL bits 0-3 (active low):
         * fire, right, left, shield */
        uint8_t v = 0xff;
        if (m->inputs & PHX_IN_COIN)
            v &= ~0x01;
        if (m->inputs & PHX_IN_START1)
            v &= ~0x02;
        if (m->inputs & PHX_IN_START2)
            v &= ~0x04;
        if (m->inputs & PHX_IN_FIRE)
            v &= ~0x10;
        if (m->inputs & PHX_IN_RIGHT)
            v &= ~0x20;
        if (m->inputs & PHX_IN_LEFT)
            v &= ~0x40;
        if (m->inputs & PHX_IN_SHIELD)
            v &= ~0x80;
        return v;
    }
    case 0x7800:
        return (m->dsw & 0x7f) | (m->vblank ? 0x00 : 0x80);
    default:
        return 0; /* unmapped */
    }
}

static void PHX_HOT(mem_write)(i8085_t *cpu, uint16_t addr, uint8_t data)
{
    phx_t *m = (phx_t *)cpu->user;
    switch (addr & 0xfc00)
    {
    case 0x5000:
        /* video register: bit 0 selects the RAM page (the player), bit 1 the
         * palette bank. Cocktail flipping is not emulated (upright cabinet). */
        if (m->vram_page != (data & 1))
        {
            m->vram_page = data & 1;
            map_vram_page(m);
        }
        m->palette_bank = (data >> 1) & 1;
        break;
    case 0x5800:
        m->scroll = data;
        break;
    case 0x6000:
        sound_catchup(m);
        phx_sound_control_a(&m->sound, data);
        break;
    case 0x6800:
        sound_catchup(m);
        phx_sound_control_b(&m->sound, data);
        break;
    default:
        break; /* ROM and unmapped */
    }
}

static uint8_t io_in(i8085_t *cpu, uint8_t port)
{
    (void)cpu;
    (void)port;
    return 0;
}

static void io_out(i8085_t *cpu, uint8_t port, uint8_t data)
{
    (void)cpu;
    (void)port;
    (void)data;
}

void phx_init(phx_t *m, const phx_roms_t *roms, int samplerate, int samples_per_frame)
{
    memset(m, 0, sizeof(*m));
    m->roms = roms;
    m->dsw = PHX_DSW0_DEFAULT;
    m->audio_samples = samples_per_frame;

    i8085_init(&m->cpu, 1);
    m->cpu.user = m;
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.in = io_in;
    m->cpu.out = io_out;
    for (int p = 0; p < 0x40; p++)
        m->cpu.rd_page[p] = &roms->maincpu[p << 8];
    map_vram_page(m);

    phx_sound_init(&m->sound, samplerate);
    phx_reset(m);
}

void phx_reset(phx_t *m)
{
    memset(m->vram, 0, sizeof(m->vram));
    m->vram_page = 0;
    m->palette_bank = 0;
    m->scroll = 0;
    map_vram_page(m);
    m->cpu.icount = 0;
    i8085_reset(&m->cpu);
    phx_sound_control_a(&m->sound, 0);
    phx_sound_control_b(&m->sound, 0);
    memset(&m->video, 0, sizeof(m->video));
}

static void PHX_HOT(run_until)(phx_t *m, int cycle)
{
    int start = m->slice_end;
    m->slice_end = cycle;
    i8085_run(&m->cpu, cycle - start);
}

/* Word copy for the per-frame paths: memcpy lives in flash. The core is built
 * with -fno-tree-loop-distribute-patterns so this stays a loop. */
static inline void copy_words(void *dst, const void *src, int bytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    for (int i = 0; i < bytes / 4; i++)
        d[i] = s[i];
}

void PHX_HOT(phx_run_frame)(phx_t *m, int16_t *audio)
{
    m->audio = audio;
    m->audio_pos = 0;
    m->slice_end = 0;

    /* visible lines */
    m->vblank = 0;
    run_until(m, PHX_VISIBLE_LINES * PHX_CYCLES_PER_LINE);

    /* VBLANK starts: this is when MAME draws the screen */
    const uint8_t *page = m->vram[m->vram_page];
    copy_words(m->video.fg, page, sizeof(m->video.fg));
    copy_words(m->video.bg, page + 0x800, sizeof(m->video.bg));
    m->video.scroll = m->scroll;
    m->video.palette_bank = m->palette_bank;

    m->vblank = 1;
    run_until(m, PHX_CYCLES_PER_FRAME);

    /* finish the frame's audio; the CPU's overshoot belongs to the next one */
    m->slice_end = PHX_CYCLES_PER_FRAME;
    m->cpu.icount = m->cpu.icount > 0 ? 0 : m->cpu.icount;
    if (audio && m->audio_pos < m->audio_samples)
        phx_sound_render(&m->sound, audio + m->audio_pos, m->audio_samples - m->audio_pos);
    m->audio = NULL;
}

/* ---------------------------------------------------------------------------
 * Palette: two 256x4 PROMs through a resistor network (MAME res_net), then
 * normalised to the full range the way MAME's palette.normalize_range does.
 * ------------------------------------------------------------------------ */

/* compute_res_net() for phoenix_net_info: RES_NET_VCC_5V | RES_NET_VBIAS_5V |
 * RES_NET_VIN_OPEN_COL, per channel { RES_NET_AMP_NONE, 100, 270, 2, {270, 1} } */
static int res_net_level(int inputs)
{
    const double vOL = 0.05; /* TTL_VOL, open collector */
    const double vBias = 5.0, vcc = 5.0;
    const double rBias = 100, rGnd = 270;
    const double R[2] = {270, 1};
    double rTotal = 0, v = 0;

    for (int i = 0; i < 2; i++)
    {
        if (!((inputs >> i) & 1))
        {
            rTotal += 1.0 / R[i];
            v += vOL / R[i];
        }
    }
    rTotal += 1.0 / rBias;
    v += vBias / rBias;
    rTotal += 1.0 / rGnd;
    /* high inputs are open collector: no contribution */
    rTotal = 1.0 / rTotal;
    v *= rTotal;
    if (v < 0)
        v = 0;
    return (int)(v * 255 / vcc + 0.4);
}

static uint8_t clamp255(int32_t v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

static void build_palette(phx_gfx_t *g, const uint8_t *prom, phx_pixfmt_t fmt)
{
    int level[4];
    for (int t = 0; t < 4; t++)
        level[t] = res_net_level(t);

    /* compute_res_net_all with phoenix_decode_info: PROM 0 holds the low bit of
     * R (bit 0), G (bit 2) and B (bit 1); PROM 1 the high bit of R (bit 0),
     * G (bit 2) and B (bit 1). */
    uint8_t rgb[256][3];
    for (int i = 0; i < 256; i++)
    {
        uint8_t lo = prom[i], hi = prom[i + 256];
        int r = (lo & 1) | ((hi << 1) & 2);
        int gg = ((lo >> 2) & 1) | ((hi >> 1) & 2);
        int b = ((lo >> 1) & 1) | (hi & 2);
        rgb[i][0] = level[r];
        rgb[i][1] = level[gg];
        rgb[i][2] = level[b];
    }

    /* native order: pen i takes colour bitswap<7>(i, 6,5,1,0,4,3,2) */
    uint8_t pen[256][3];
    for (int i = 0; i < 256; i++)
    {
        int col = (((i >> 6) & 1) << 6) | (((i >> 5) & 1) << 5) | (((i >> 1) & 1) << 4) | (((i >> 0) & 1) << 3) |
                  (((i >> 4) & 1) << 2) | (((i >> 3) & 1) << 1) | (((i >> 2) & 1) << 0);
        memcpy(pen[i], rgb[col], 3);
    }

    /* palette.normalize_range(0, 255) */
    int32_t ymin = 1000 * 255, ymax = 0;
    for (int i = 0; i < 256; i++)
    {
        int32_t y = 299 * pen[i][0] + 587 * pen[i][1] + 114 * pen[i][2];
        if (y < ymin)
            ymin = y;
        if (y > ymax)
            ymax = y;
    }
    const int32_t tmin = 0, tmax = 255;
    for (int i = 0; i < 256; i++)
    {
        int32_t y = 299 * pen[i][0] + 587 * pen[i][1] + 114 * pen[i][2];
        int32_t u = ((int32_t)pen[i][2] - y / 1000) * 492 / 1000;
        int32_t v = ((int32_t)pen[i][0] - y / 1000) * 877 / 1000;
        int32_t target = (ymax > ymin) ? tmin + ((y - ymin) * (tmax - tmin + 1)) / (ymax - ymin) : 0;
        uint8_t r = clamp255(target + 1140 * v / 1000);
        uint8_t gr = clamp255(target - 395 * u / 1000 - 581 * v / 1000);
        uint8_t b = clamp255(target + 2032 * u / 1000);
        g->palette_rgb[i][0] = r;
        g->palette_rgb[i][1] = gr;
        g->palette_rgb[i][2] = b;

        switch (fmt)
        {
        case PHX_FMT_RGB444:
            g->pal[i] = ((r >> 4) << 8) | ((gr >> 4) << 4) | (b >> 4);
            break;
        case PHX_FMT_RGB555:
            g->pal[i] = ((r >> 3) << 10) | ((gr >> 3) << 5) | (b >> 3);
            break;
        default:
            g->pal[i] = ((r >> 3) << 11) | ((gr >> 2) << 5) | (b >> 3);
            break;
        }
    }
    g->black = 0;
}

/* charlayout: 8x8, 256 chars, 2 planes {256*8*8, 0}, x offsets {7..0}. MAME's
 * first plane is the most significant bit, so plane bit 1 comes from the
 * second half of the ROM. x offset 7 reads bit 0x01 of the byte. */
static int tile_pixel(const uint8_t *rom, int code, int x, int y)
{
    int mask = 1 << x;
    int lo = (rom[code * 8 + y] & mask) ? 1 : 0;
    int hi = (rom[0x800 + code * 8 + y] & mask) ? 2 : 0;
    return hi | lo;
}

static void decode_tiles(const uint8_t *rom, uint16_t rows[256][8], uint16_t cols[256][8])
{
    for (int code = 0; code < 256; code++)
    {
        for (int i = 0; i < 8; i++)
        {
            uint16_t r = 0, c = 0;
            for (int j = 0; j < 8; j++)
            {
                r |= tile_pixel(rom, code, j, i) << (2 * j); /* row i, along x */
                c |= tile_pixel(rom, code, i, j) << (2 * j); /* column i, along y */
            }
            rows[code][i] = r;
            cols[code][i] = c;
        }
    }
}

void phx_gfx_init(phx_gfx_t *g, const phx_roms_t *roms, phx_pixfmt_t fmt)
{
    memset(g, 0, sizeof(*g));
    build_palette(g, roms->proms, fmt);
    decode_tiles(roms->fgtiles, g->fg_rows, g->fg_cols);
    decode_tiles(roms->bgtiles, g->bg_rows, g->bg_cols);
}

/* ---------------------------------------------------------------------------
 * Renderer
 * ------------------------------------------------------------------------ */

#define CANVAS_W 320
#define CANVAS_H 240

static inline void fill(uint16_t *p, int n, uint16_t c)
{
    while (n-- > 0)
        *p++ = c;
}

/* One raw line (256 pixels, raw y) of background with scroll, foreground on
 * top. */
static void PHX_HOT(render_raw_line)(const phx_gfx_t *g, const phx_video_t *v, int y, uint16_t *line)
{
    const int ty = y >> 3, py = y & 7;
    const int pb = v->palette_bank << 4;

    /* background, scrolled along raw x */
    for (int x = 0; x < PHX_RAW_WIDTH; x++)
    {
        int bx = (x + v->scroll) & 0xff;
        uint8_t code = v->bg[ty * 32 + (bx >> 3)];
        int pix = (g->bg_rows[code][py] >> ((bx & 7) * 2)) & 3;
        line[x] = g->pal[(((code >> 5) | pb) << 2) + pix];
    }

    /* foreground, pen 0 transparent */
    for (int tx = 0; tx < 32; tx++)
    {
        uint8_t code = v->fg[ty * 32 + tx];
        uint16_t w = g->fg_rows[code][py];
        if (!w)
            continue;
        const uint16_t *pal = &g->pal[((code >> 5) | 8 | pb) << 2];
        uint16_t *dst = &line[tx * 8];
        for (int px = 0; px < 8; px++, w >>= 2)
            if (w & 3)
                dst[px] = pal[w & 3];
    }
}

/* The canvas of each orientation (320x240, or stride x 256 for rotated
 * full) and the game area in it */
static void game_area(phx_orient_t orient, int stride, int *cw, int *ch, int *x0, int *y0, int *w, int *h)
{
    *cw = CANVAS_W;
    *ch = CANVAS_H;
    if (orient == PHX_ORIENT_ROTATED_FULL)
    {
        *cw = stride;
        *ch = PHX_RAW_WIDTH; /* 256 */
        *w = PHX_RAW_HEIGHT; /* 208 */
        *h = PHX_RAW_WIDTH;  /* 256 */
    }
    else if (orient == PHX_ORIENT_ROTATED)
    {
        *w = PHX_RAW_HEIGHT; /* 208 */
        *h = CANVAS_H;       /* 240 */
    }
    else
    {
        *w = PHX_RAW_WIDTH;  /* 256 */
        *h = PHX_RAW_HEIGHT; /* 208 */
    }
    *x0 = (*cw - *w) / 2;
    *y0 = (*ch - *h) / 2;
}

void phx_render_borders(const phx_gfx_t *g, phx_orient_t orient, uint16_t *fb, int stride)
{
    int cw, ch, x0, y0, w, h;
    game_area(orient, stride, &cw, &ch, &x0, &y0, &w, &h);
    for (int r = 0; r < ch; r++)
    {
        uint16_t *row = fb + r * stride;
        if (r < y0 || r >= y0 + h)
        {
            fill(row, cw, g->black);
        }
        else
        {
            fill(row, x0, g->black);
            fill(row + x0 + w, cw - x0 - w, g->black);
        }
    }
}

void PHX_HOT(phx_render)(const phx_gfx_t *g, const phx_video_t *v, phx_orient_t orient, uint16_t *fb, int stride)
{
    if (orient == PHX_ORIENT_ROTATED || orient == PHX_ORIENT_ROTATED_FULL)
    {
        /* Upright 208 x 256 picture: display (dx, dy) shows raw (x = dy,
         * y = 207 - dx). Rotated full shows all 256 rows (dy = r); rotated
         * fits them into 240: output row r shows dy = r*16/15, so every 16th
         * row (dy = 15, 31, ...) is dropped. */
        const int full = (orient == PHX_ORIENT_ROTATED_FULL);
        int cw, ch, left, y0, w, h;
        game_area(orient, stride, &cw, &ch, &left, &y0, &w, &h); /* left 56, or 24 at stride 256 */
        const int pb = v->palette_bank << 4;
        for (int r = 0; r < h; r++)
        {
            const int x = full ? r : (r * 16) / 15;
            const int tx = x >> 3, px = x & 7;
            const int bx = (x + v->scroll) & 0xff;
            const int btx = bx >> 3, bpx = bx & 7;
            uint16_t *dst = fb + r * stride + left;
            for (int ty = PHX_RAW_HEIGHT / 8 - 1; ty >= 0; ty--)
            {
                uint8_t fc = v->fg[ty * 32 + tx];
                uint8_t bc = v->bg[ty * 32 + btx];
                uint16_t fw = g->fg_cols[fc][px];
                uint16_t bw = g->bg_cols[bc][bpx];
                const uint16_t *fpal = &g->pal[((fc >> 5) | 8 | pb) << 2];
                const uint16_t *bpal = &g->pal[((bc >> 5) | pb) << 2];
                for (int py = 7; py >= 0; py--)
                {
                    int f = (fw >> (py * 2)) & 3;
                    *dst++ = f ? fpal[f] : bpal[(bw >> (py * 2)) & 3];
                }
            }
        }
        return;
    }

    /* Tate: raw 256 x 208, centred */
    const int left = (CANVAS_W - PHX_RAW_WIDTH) / 2; /* 32 */
    const int top = (CANVAS_H - PHX_RAW_HEIGHT) / 2; /* 16 */
    uint16_t line[PHX_RAW_WIDTH] __attribute__((aligned(4)));
    for (int y = 0; y < PHX_RAW_HEIGHT; y++)
    {
        render_raw_line(g, v, y, line);
        if (orient == PHX_ORIENT_TATE_CW)
        {
            copy_words(fb + (top + y) * stride + left, line, sizeof(line));
        }
        else
        {
            uint16_t *row = fb + (top + PHX_RAW_HEIGHT - 1 - y) * stride + left;
            for (int x = 0; x < PHX_RAW_WIDTH; x++)
                row[x] = line[PHX_RAW_WIDTH - 1 - x];
        }
    }
}
