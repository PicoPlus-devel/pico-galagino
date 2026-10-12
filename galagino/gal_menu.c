/*
 * Galagino's game menu (galagino.ino render_line/render_logo and the menu part
 * of emulation.c's emulate_frame): a column of 224x96 logos, the selected one
 * in colour and the others grey. Up to three games stand still; with more the
 * column scrolls so that the selection sits in the middle.
 *
 * Only the installed games are listed that can run on the board. Up/down
 * selects, left/right moves a page (three logos), fire or start launches.
 * After 20 seconds without input a random game starts (Galagino's "master
 * attract" mode); any input switches that off for good.
 *
 * The menu is drawn straight into the canvas, a row of the upright picture at a
 * time, and only when it changed.
 */
#include "gal_internal.h"

#include "data/1942_logo.h"
#include "data/digdug_logo.h"
#include "data/dkong_logo.h"
#include "data/frogger_logo.h"
#include "data/galaga_logo.h"
#include "data/pacman_logo.h"
#include "data/alibaba_logo.h"
#include "data/crush_logo.h"
#include "data/eyes_logo.h"
#include "data/lizwiz_logo.h"
#include "data/mrtnt_logo.h"
#include "data/mspacman_logo.h"
#include "data/theglob_logo.h"
#include "data/pengo_logo.h"
#include "data/amidar_logo.h"
#include "data/anteater_logo.h"
#include "data/galaxian_logo.h"
#include "data/mooncresta_logo.h"
#include "data/scramble_logo.h"
#include "data/supercobra_logo.h"
#include "data/turtles_logo.h"
#include "data/vanvan_logo.h"
#include "data/ladybug_logo.h"
#include "data/mrdo_logo.h"
#include "data/phoenix_logo.h"
#include "data/spaceinvaders_logo.h"
#include "data/bombjack_logo.h"
#include "data/bagman_logo.h"
#include "data/pbaction_logo.h"
#include "data/starforce_logo.h"
#include "data/dkongjr_logo.h"
#include "data/btime_logo.h"
#include "data/bnj_logo.h"
#include "data/vanguard_logo.h"
#include "data/fantasy_logo.h"
#include "data/nibbler_logo.h"
#include "data/dkong3_logo.h"
#include "data/timeplt_logo.h"
#include "data/pooyan_logo.h"
#include "data/tutankhm_logo.h"
#include "data/rocnrope_logo.h"
#include "data/circusc_logo.h"
#include "data/mappy_logo.h"
#include "data/todruaga_logo.h"
#include "data/gaplus_logo.h"
#include "data/gyruss_logo.h"
#include "data/xevious_logo.h"

#define LOGO_H 96
#define ATTRACT_FRAMES (20 * 60)

static const unsigned short *const logos[GAL_GAME_COUNT] = {
    [GAL_GAME_PACMAN] = pacman_logo,     [GAL_GAME_GALAGA] = galaga_logo,     [GAL_GAME_DKONG] = dkong_logo,
    [GAL_GAME_FROGGER] = frogger_logo,   [GAL_GAME_DIGDUG] = digdug_logo,     [GAL_GAME_1942] = _1942_logo,
    [GAL_GAME_ALIBABA] = alibaba_logo,   [GAL_GAME_CRUSH] = crush_logo,       [GAL_GAME_EYES] = eyes_logo,
    [GAL_GAME_LIZWIZ] = lizwiz_logo,     [GAL_GAME_MRTNT] = mrtnt_logo,       [GAL_GAME_MSPACMAN] = mspacman_logo,
    [GAL_GAME_THEGLOB] = theglob_logo,   [GAL_GAME_PENGO] = pengo_logo,       [GAL_GAME_AMIDAR] = amidar_logo,
    [GAL_GAME_ANTEATER] = anteater_logo, [GAL_GAME_GALAXIAN] = galaxian_logo, [GAL_GAME_MOONCRST] = mooncresta_logo,
    [GAL_GAME_SCRAMBLE] = scramble_logo, [GAL_GAME_SCOBRA] = supercobra_logo, [GAL_GAME_TURTLES] = turtles_logo,
    [GAL_GAME_VANVAN] = vanvan_logo,     [GAL_GAME_LADYBUG] = ladybug_logo,   [GAL_GAME_MRDO] = mrdo_logo,
    [GAL_GAME_PHOENIX] = phoenix_logo,   [GAL_GAME_INVADERS] = spaceinvaders_logo,
    [GAL_GAME_BOMBJACK] = bombjack_logo,   [GAL_GAME_BAGMAN] = bagman_logo,
    [GAL_GAME_PBACTION] = pbaction_logo,   [GAL_GAME_STARFORC] = starforce_logo,
    [GAL_GAME_DKONGJR] = dkongjr_logo,     [GAL_GAME_BTIME] = btime_logo,
    [GAL_GAME_BNJ] = bnj_logo,             [GAL_GAME_VANGUARD] = vanguard_logo,
    [GAL_GAME_FANTASY] = fantasy_logo,     [GAL_GAME_NIBBLER] = nibbler_logo,
    [GAL_GAME_DKONG3] = dkong3_logo,       [GAL_GAME_TIMEPLT] = timeplt_logo,
    [GAL_GAME_POOYAN] = pooyan_logo,       [GAL_GAME_TUTANKHM] = tutankhm_logo,
    [GAL_GAME_ROCNROPE] = rocnrope_logo,   [GAL_GAME_CIRCUSC] = circusc_logo,
    [GAL_GAME_MAPPY] = mappy_logo,         [GAL_GAME_TODRUAGA] = todruaga_logo,
    [GAL_GAME_GAPLUS] = gaplus_logo,       [GAL_GAME_GYRUSS] = gyruss_logo,
    [GAL_GAME_XEVIOUS] = xevious_logo,
};

static struct
{
    uint8_t games[GAL_GAME_COUNT];
    int n;
    int sel;    /* index into games */
    int offset; /* scroll position in pixels, 0 .. n*96-1 (n > 3 only) */
    int dirty;
    uint16_t last_buttons;
    int idle_frames;
    int attract; /* the random start is still armed */
    uint32_t rng;
} menu;

static int target_offset(void)
{
    /* the selection in the middle of the three logos on screen */
    return (LOGO_H * (menu.sel - 1 + menu.n)) % (menu.n * LOGO_H);
}

void gal_menu_init(gal_games_t installed)
{
    menu.n = 0;
    menu.sel = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        if ((installed & GAL_GAME_BIT(g)) && gal_available((gal_game_id_t)g) && logos[g])
        {
            if (g == GAL_GAME_GALAGA)
                menu.sel = menu.n; /* Galaga is the default, as in Galagino */
            menu.games[menu.n++] = (uint8_t)g;
        }
    }
    menu.offset = menu.n > 3 ? target_offset() : 0;
    menu.dirty = 1;
    /* buttons still held from the game do not count until released */
    menu.last_buttons = 0xffff;
    menu.idle_frames = 0;
    menu.attract = 1;
    if (!menu.rng)
        menu.rng = 0x2545f491;
}

int gal_menu_frame(uint16_t buttons)
{
    if (menu.n == 0)
        return -1;
    const uint16_t pressed = buttons & ~menu.last_buttons;
    menu.last_buttons = buttons;
    menu.rng = menu.rng * 1664525u + 1013904223u;

    if (buttons)
        menu.attract = 0;

    int sel = menu.sel;
    if (pressed & GAL_BTN_UP)
        sel--;
    if (pressed & GAL_BTN_DOWN)
        sel++;
    if (menu.n <= 3)
    {
        if (sel < 0)
            sel = 0;
        if (sel >= menu.n)
            sel = menu.n - 1;
    }
    else
    {
        /* a page: no further than the first or last game */
        if (pressed & GAL_BTN_LEFT)
            sel = (sel == 0) ? menu.n - 1 : (sel < 3 ? 0 : sel - 3);
        if (pressed & GAL_BTN_RIGHT)
            sel = (sel == menu.n - 1) ? 0 : (sel + 3 >= menu.n ? menu.n - 1 : sel + 3);
        sel = (sel + menu.n) % menu.n;
    }
    if (sel != menu.sel)
    {
        menu.sel = sel;
        menu.dirty = 1;
    }

    if (menu.n > 3)
    {
        /* scroll 8 pixels a frame towards the selection, the short way round */
        const int len = menu.n * LOGO_H;
        const int target = target_offset();
        if (menu.offset != target)
        {
            /* a page further away (left/right) scrolls three times as fast */
            const int diff = ((target - menu.offset) % len + len) % len;
            const int fwd = diff < len / 2, dist = fwd ? diff : len - diff;
            const int step = dist > LOGO_H ? 24 : 8;
            const int move = dist < step ? dist : step;
            menu.offset = fwd ? (menu.offset + move) % len : (menu.offset - move + len) % len;
            menu.dirty = 1;
        }
    }

    if (pressed & (GAL_BTN_FIRE | GAL_BTN_START))
        return menu.games[menu.sel];

    if (menu.attract && ++menu.idle_frames >= ATTRACT_FRAMES)
    {
        menu.attract = 0;
        return menu.games[(menu.rng >> 16) % menu.n];
    }
    return -1;
}

/* --- Drawing ----------------------------------------------------------------- */

/* galagino.ino greyscale(), on plain RGB565: a 6-bit grey level */
static uint16_t grey565(uint16_t c)
{
    const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    const unsigned avg = (2 * r + g + 2 * b) / 4;
    return (uint16_t)(((avg >> 1) << 11) | (avg << 5) | (avg >> 1));
}

/* A run-length decoder over one logo: literal pixels, or the marker followed
 * by (count - 1) and the colour. Pixels are byte-swapped RGB565. */
typedef struct
{
    const unsigned short *data;
    unsigned short marker;
    int run;
    uint16_t run_color;
} rle_t;

static void rle_start(rle_t *d, const unsigned short *logo)
{
    d->marker = logo[0];
    d->data = logo + 1;
    d->run = 0;
}

static uint16_t rle_next(rle_t *d)
{
    if (!d->run)
    {
        if (d->data[0] != d->marker)
            return *d->data++;
        d->run = d->data[1] + 1;
        d->run_color = d->data[2];
        d->data += 3;
    }
    d->run--;
    return d->run_color;
}

static void put_row(gal_orient_t orient, uint16_t *fb, int stride, int y, const uint16_t *line)
{
    if (orient == GAL_ORIENT_UPRIGHT)
    {
        if (y % 6 == 5)
            return;
        uint16_t *dst = fb + (y - y / 6) * stride + (GAL_CANVAS_W - GAL_W) / 2;
        memcpy(dst, line, GAL_W * sizeof(uint16_t));
        return;
    }
    if (orient == GAL_ORIENT_UPRIGHT_FULL)
    {
        memcpy(fb + y * stride + (stride - GAL_W) / 2, line, GAL_W * sizeof(uint16_t));
        return;
    }
    const int x0 = (GAL_CANVAS_W - GAL_H) / 2, y0 = (GAL_CANVAS_H - GAL_W) / 2;
    for (int ux = 0; ux < GAL_W; ux++)
    {
        if (orient == GAL_ORIENT_TATE_CW)
            fb[(y0 + 223 - ux) * stride + x0 + y] = line[ux];
        else
            fb[(y0 + ux) * stride + x0 + GAL_H - 1 - y] = line[ux];
    }
}

void gal_menu_render(gal_orient_t orient, uint16_t *fb, int stride, int force)
{
    if (!menu.dirty && !force)
        return;
    menu.dirty = 0;

    uint16_t line[GAL_W];
    rle_t dec = {0};
    int cur_logo = -1, cur_ly = -1;

    /* static layout: the logos centred in the 36 tile rows */
    const int top = (menu.n <= 3) ? 8 * ((36 - 12 * menu.n) / 2) : 0;

    for (int y = 0; y < GAL_H; y++)
    {
        int slot, ly;
        if (menu.n <= 3)
        {
            const int rel = y - top;
            slot = (rel >= 0 && rel < menu.n * LOGO_H) ? rel / LOGO_H : -1;
            ly = rel - slot * LOGO_H;
        }
        else
        {
            const int pos = (y + menu.offset) % (menu.n * LOGO_H);
            slot = pos / LOGO_H;
            ly = pos % LOGO_H;
        }

        if (slot < 0)
        {
            memset(line, 0, sizeof(line));
        }
        else
        {
            const int game = menu.games[slot];
            if (game != cur_logo || ly != cur_ly + 1)
            {
                rle_start(&dec, logos[game]);
                for (int skip = ly * GAL_W; skip > 0; skip--)
                    rle_next(&dec);
                cur_logo = game;
            }
            cur_ly = ly;
            const int active = (slot == menu.sel);
            for (int x = 0; x < GAL_W; x++)
            {
                const uint16_t raw = rle_next(&dec);
                const uint16_t c565 = (uint16_t)((raw >> 8) | (raw << 8));
                line[x] = gal_color(active ? c565 : grey565(c565));
            }
        }
        put_row(orient, fb, stride, y, line);
    }
}
