/*
 * Galagino's game menu (galagino.ino render_line/render_logo and the menu part
 * of emulation.c's emulate_frame): a column of 224x96 logos, the selected one
 * in colour and the others grey. Up to three games stand still; with more the
 * column scrolls so that the selection sits in the middle.
 *
 * Only the installed games are listed that can run on the board. Up/down
 * selects, left/right moves a page (three logos), fire or start launches.
 * After 20 seconds without input a game starts by itself (Galagino's "master
 * attract" mode); a button press switches that off until the menu is shown
 * again. The games come in rounds: all of them shuffled, each once, then a new
 * shuffle. The pick is shown: the column runs on like a reel, the logo in its
 * middle in colour, slows down and stops on the game, whose logo blinks before
 * it starts. gal_menu_attract_now() does the same at once, for the next game
 * of an attract mode that has been running a while.
 *
 * The menu is drawn straight into the canvas, only when it changed, and like
 * gal_render top to bottom, ahead of the scan-out. In tate a canvas row is a
 * column of the upright picture, so it is drawn eight columns at a time; drawn
 * a row of the upright picture at a time instead, the scan-out showed the
 * picture torn along a diagonal while it scrolled. A logo is run-length coded
 * and can only be decoded from its start, so each logo in view has a table of
 * where the decoder stands at every eighth pixel, made when it comes into view.
 */
#include "gal_internal.h"

#include <stdlib.h>

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
#define REEL_MIN (8 * LOGO_H) /* the reel runs at least this far: laps with few games */
#define REEL_FRAMES 120       /* about as long as it runs at full speed */
#define REEL_TICK 6           /* static layout: frames a logo, before it slows down */
#define BLINK_FRAMES 72       /* four blinks of 16 frames, then colour */
#define BANDS (GAL_W / 8) /* a logo row in pieces of eight pixels */
#define TABLES 4          /* logos in view at most: 288 rows of 96, scrolled */

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

/* A logo is run-length coded: after the marker word, literal pixels, or the
 * marker followed by (count - 1) and the colour. Pixels are byte-swapped
 * RGB565. A run can carry on into the next row.
 *
 * A mark is where decoding stands at a pixel: the word read next, and the
 * pixels left of the run it is in (its colour the word before). Both fit: the
 * largest logo is 14164 words. */
typedef struct
{
    uint16_t pos, run;
} rle_mark_t;

/* A logo's marks at every eighth pixel, BANDS a row. */
static void make_marks(rle_mark_t *m, const unsigned short *logo)
{
    const unsigned short marker = logo[0], *data = logo + 1;
    int run = 0;
    for (int i = 0; i < LOGO_H * BANDS; i++)
    {
        m[i].pos = (uint16_t)(data - logo);
        m[i].run = (uint16_t)run;
        for (int left = 8; left;)
        {
            if (run)
            {
                const int k = run < left ? run : left;
                run -= k;
                left -= k;
            }
            else if (*data != marker)
            {
                data++;
                left--;
            }
            else
            {
                run = data[1] + 1;
                data += 3;
            }
        }
    }
}

static struct
{
    uint8_t games[GAL_GAME_COUNT];
    int n;
    int sel;    /* index into games */
    int offset; /* scroll position in pixels, 0 .. n*96-1 (n > 3 only) */
    int dirty;
    uint16_t last_buttons;
    int idle_frames;
    int attract;            /* the random start is still armed */
    int reel;               /* attract mode's pick on its way: REEL_SPIN, REEL_BLINK or 0 */
    int reel_target;        /* the pick, an index into games */
    int reel_left;          /* still to go: pixels, or logos in the static layout */
    int reel_frames;
    int reel_speed;         /* full speed, in pixels a frame */
    int blink_off;          /* the selected logo shows grey */
    rle_mark_t *marks;      /* TABLES tables of LOGO_H * BANDS, in the game arena */
    int table_slot[TABLES]; /* the index into games each table is for, or -1 */
} menu;

_Static_assert(TABLES * LOGO_H * BANDS * sizeof(rle_mark_t) <= GAL_ARENA_SIZE, "the logo tables must fit the arena");

enum
{
    REEL_SPIN = 1,
    REEL_BLINK,
};

/* The scroll position with logo `sel` in the middle of the three on screen */
static int offset_of(int sel)
{
    return (LOGO_H * (sel - 1 + menu.n)) % (menu.n * LOGO_H);
}

static int target_offset(void)
{
    return offset_of(menu.sel);
}

/* The logo on row y of the upright picture, as an index into games (-1: none),
 * and its row in the logo. */
static int row_at(int y, int *ly)
{
    int pos;
    if (menu.n <= 3)
    {
        /* static layout: the logos centred in the 36 tile rows */
        pos = y - 8 * ((36 - 12 * menu.n) / 2);
        if (pos < 0 || pos >= menu.n * LOGO_H)
            return -1;
    }
    else
    {
        pos = (y + menu.offset) % (menu.n * LOGO_H);
    }
    *ly = pos % LOGO_H;
    return pos / LOGO_H;
}

/* Whether any row of logo `slot` (an index into games) is on screen. */
static int in_view(int slot)
{
    if (menu.n <= 3)
        return 1;
    const int len = menu.n * LOGO_H;
    const int rel = (slot * LOGO_H - menu.offset + len) % len; /* its first row */
    return rel < GAL_H || rel > len - LOGO_H;
}

/* The marks of a logo in view. Without a table yet it gets one that no logo
 * in view uses; there always is one, as at most TABLES are in view. */
static const rle_mark_t *marks_of(int slot)
{
    int spare = 0;
    for (int t = 0; t < TABLES; t++)
    {
        if (menu.table_slot[t] == slot)
            return menu.marks + t * LOGO_H * BANDS;
        if (menu.table_slot[t] < 0 || !in_view(menu.table_slot[t]))
            spare = t;
    }
    rle_mark_t *m = menu.marks + spare * LOGO_H * BANDS;
    make_marks(m, logos[menu.games[slot]]);
    menu.table_slot[spare] = slot;
    return m;
}

/* The tables of the logos in view, made here rather than while drawing, which
 * has to keep ahead of the scan-out. */
static void make_tables(void)
{
    for (int s = 0; s < menu.n; s++)
        if (in_view(s))
            marks_of(s);
}

void gal_menu_init(gal_games_t installed, int select)
{
    /* the tables take the game's place in the arena */
    gal_stop();
    menu.marks = (rle_mark_t *)gal_fast_alloc(TABLES * LOGO_H * BANDS * sizeof(rle_mark_t));
    for (int t = 0; t < TABLES; t++)
        menu.table_slot[t] = -1;

    menu.n = 0;
    menu.sel = 0;
    int selected = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        if ((installed & GAL_GAME_BIT(g)) && gal_available((gal_game_id_t)g) && logos[g])
        {
            if (g == select)
            {
                menu.sel = menu.n;
                selected = 1;
            }
            else if (g == GAL_GAME_GALAGA && !selected)
                menu.sel = menu.n; /* Galaga is the default, as in Galagino */
            menu.games[menu.n++] = (uint8_t)g;
        }
    }
    menu.offset = menu.n > 3 ? target_offset() : 0;
    make_tables();
    menu.dirty = 1;
    /* buttons still held from the game do not count until released */
    menu.last_buttons = 0xffff;
    menu.idle_frames = 0;
    menu.attract = 1;
    menu.reel = 0;
    menu.blink_off = 0;
}

/* Attract mode's round: every game id shuffled, walked in order, the ids the
 * menu does not list skipped; at the end a new shuffle. It outlives
 * gal_menu_init. The order follows from the seed alone, so that the seed and
 * the position carry a round over a restart (gal_menu_set_attract_order). */
static struct
{
    uint32_t seed;
    int pos; /* the next entry; GAL_GAME_COUNT: a new round is due */
    uint8_t order[GAL_GAME_COUNT];
} deck = {.pos = GAL_GAME_COUNT};

static void deck_shuffle(void)
{
    uint32_t s = deck.seed;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
        deck.order[g] = (uint8_t)g;
    for (int i = GAL_GAME_COUNT - 1; i > 0; i--)
    {
        s = s * 1664525u + 1013904223u; /* an LCG, its high bits */
        const int j = (int)((s >> 16) % (uint32_t)(i + 1));
        const uint8_t t = deck.order[i];
        deck.order[i] = deck.order[j];
        deck.order[j] = t;
    }
}

/* Game g as an index into games, or -1 */
static int index_of(int g)
{
    for (int i = 0; i < menu.n; i++)
        if (menu.games[i] == g)
            return i;
    return -1;
}

/* The first game of the round from entry pos on, as an index into games */
static int deck_find(int *pos)
{
    while (*pos < GAL_GAME_COUNT)
    {
        const int i = index_of(deck.order[(*pos)++]);
        if (i >= 0)
            return i;
    }
    return -1;
}

/* The next game of the round, as an index into games (menu.n > 0). A round
 * does not start with the selected game, the one just shown; the seed is drawn
 * again rather than the order changed, so that the seed still gives the order. */
static int deck_next(void)
{
    if (deck.pos < GAL_GAME_COUNT)
    {
        const int i = deck_find(&deck.pos);
        if (i >= 0)
            return i;
    }
    int first;
    do
    {
        /* rand() is seeded from the hardware RNG by Frens::initAll() */
        deck.seed = (uint32_t)rand();
        deck_shuffle();
        deck.pos = 0;
        int p = 0;
        first = deck_find(&p);
    } while (menu.n > 1 && first == menu.sel);
    return deck_find(&deck.pos);
}

void gal_menu_attract_order(uint32_t *seed, uint32_t *pos)
{
    *seed = deck.seed;
    *pos = (uint32_t)deck.pos;
}

void gal_menu_set_attract_order(uint32_t seed, uint32_t pos)
{
    deck.seed = seed;
    deck_shuffle();
    deck.pos = pos < GAL_GAME_COUNT ? (int)pos : GAL_GAME_COUNT;
}

/* Attract mode picks a game, the next of its round, which the reel runs to.
 * That is not the selected game, unless someone just played it. */
static void reel_start(void)
{
    menu.attract = 0;
    menu.reel_frames = 0;
    const int t = deck_next();
    menu.reel_target = t;
    if (menu.n == 1)
    {
        menu.reel = REEL_BLINK;
        return;
    }
    menu.reel = REEL_SPIN;
    if (menu.n > 3)
    {
        /* forward only, as with down, so the logos run up the screen */
        const int len = menu.n * LOGO_H;
        menu.reel_left = ((offset_of(t) - menu.offset) % len + len) % len;
        while (menu.reel_left < REEL_MIN)
            menu.reel_left += len;
        /* about as long whatever the distance: up to half a logo a frame,
         * which takes at most one new logo table a frame */
        const int speed = menu.reel_left / REEL_FRAMES;
        menu.reel_speed = speed < 12 ? 12 : speed > LOGO_H / 2 ? LOGO_H / 2 : speed;
    }
    else
    {
        menu.reel_left = (t - menu.sel + menu.n) % menu.n;
        while (menu.reel_left < 6)
            menu.reel_left += menu.n;
    }
}

void gal_menu_attract_now(void)
{
    if (menu.n)
        reel_start();
}

/* A frame of the reel. Returns the game to start, or -1. */
static int reel_frame(void)
{
    if (menu.reel == REEL_BLINK)
    {
        const int off = (++menu.reel_frames / 8) & 1;
        if (off != menu.blink_off)
        {
            menu.blink_off = off;
            menu.dirty = 1;
        }
        if (menu.reel_frames < BLINK_FRAMES)
            return -1;
        menu.reel = 0;
        menu.blink_off = 0;
        return menu.games[menu.sel];
    }

    if (menu.n > 3)
    {
        /* full speed, then slowing down over the last logos; the logo in the
         * middle of the screen is the selection */
        int step = menu.reel_left / 8;
        step = step > menu.reel_speed ? menu.reel_speed : step < 2 ? 2 : step;
        if (step > menu.reel_left)
            step = menu.reel_left;
        menu.offset = (menu.offset + step) % (menu.n * LOGO_H);
        menu.reel_left -= step;
        int ly;
        menu.sel = row_at(GAL_H / 2, &ly);
        make_tables();
        menu.dirty = 1;
    }
    else
    {
        /* the colour moves down a logo at a time, slower over the last three */
        const int tick = REEL_TICK + (menu.reel_left <= 3 ? (4 - menu.reel_left) * 4 : 0);
        if (++menu.reel_frames < tick)
            return -1;
        menu.reel_frames = 0;
        menu.sel = (menu.sel + 1) % menu.n;
        menu.reel_left--;
        menu.dirty = 1;
    }
    if (menu.reel_left == 0)
    {
        menu.sel = menu.reel_target;
        menu.reel = REEL_BLINK;
        menu.reel_frames = 0;
    }
    return -1;
}

int gal_menu_frame(uint16_t buttons)
{
    if (menu.n == 0)
        return -1;
    const uint16_t pressed = buttons & ~menu.last_buttons;
    menu.last_buttons = buttons;

    if (menu.reel)
    {
        if (!buttons)
            return reel_frame();
        /* Someone is at the board: the reel stops at the logo in the middle,
         * which the scroll below settles on. The press does nothing else. */
        menu.reel = 0;
        menu.blink_off = 0;
        menu.dirty = 1;
        return -1;
    }

    /* A button still held from the game (or from the settings menu, which
     * returns before the button that chose "Quit game" is released) is no
     * choice: it holds the random start off, but does not cancel it. */
    if (pressed)
        menu.attract = 0;
    else if (buttons)
        menu.idle_frames = 0;

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
            make_tables();
            menu.dirty = 1;
        }
    }

    if (pressed & (GAL_BTN_FIRE | GAL_BTN_START))
        return menu.games[menu.sel];

    if (menu.attract && ++menu.idle_frames >= ATTRACT_FRAMES)
        reel_start(); /* the next game of attract mode's round */
    return -1;
}

/* --- Drawing ----------------------------------------------------------------- */

/* galagino.ino greyscale(), on plain RGB565: a 6-bit grey level */
static inline uint16_t grey565(uint16_t c)
{
    const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    const unsigned avg = (2 * r + g + 2 * b) / 4;
    return (uint16_t)(((avg >> 1) << 11) | (avg << 5) | (avg >> 1));
}

/* A logo pixel in the canvas format; grey unless the logo is the selected one */
static inline uint16_t pixel(uint16_t raw, int active)
{
    const uint16_t c565 = (uint16_t)((raw >> 8) | (raw << 8));
    return gal_color(active ? c565 : grey565(c565));
}

/* Rows of the upright picture from one logo, or black (logo NULL). Four at
 * most: four logos scrolled, or two logos with black above and below. */
typedef struct
{
    int slot; /* index into games, -1: black */
    int rows;
    const unsigned short *logo;
    unsigned short marker;   /* logo[0], not read from flash again for each piece */
    const rle_mark_t *marks; /* of its first row */
    int active;
} span_t;

/* n pixels of a span's logo from a mark. A run's colour is converted once;
 * runs are most of the pixels. */
static void GAL_HOT(decode)(uint16_t *out, int n, const span_t *s, rle_mark_t m)
{
    const unsigned short *data = s->logo + m.pos;
    const int active = s->active;
    int run = m.run;
    uint16_t color = run ? pixel(data[-1], active) : 0;
    const uint16_t *end = out + n;
    while (out < end)
    {
        if (!run)
        {
            if (*data != s->marker)
            {
                *out++ = pixel(*data++, active);
                continue;
            }
            run = data[1] + 1;
            color = pixel(data[2], active);
            data += 3;
        }
        int k = (int)(end - out);
        if (run < k)
            k = run;
        run -= k;
        while (k--)
            *out++ = color;
    }
}

static int spans_of(span_t *s)
{
    int k = 0;
    for (int y = 0; y < GAL_H; y++)
    {
        int ly = 0;
        const int slot = row_at(y, &ly);
        if (k && s[k - 1].slot == slot)
        {
            s[k - 1].rows++;
            continue;
        }
        s[k].slot = slot;
        s[k].rows = 1;
        s[k].logo = slot < 0 ? NULL : logos[menu.games[slot]];
        s[k].marker = slot < 0 ? 0 : s[k].logo[0];
        s[k].marks = slot < 0 ? NULL : marks_of(slot) + ly * BANDS;
        s[k].active = (slot == menu.sel) && !menu.blink_off;
        k++;
    }
    return k;
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
    memcpy(fb + y * stride + (stride - GAL_W) / 2, line, GAL_W * sizeof(uint16_t));
}

void GAL_HOT(gal_menu_render)(gal_orient_t orient, uint16_t *fb, int stride, int force)
{
    if (!menu.dirty && !force)
        return;
    menu.dirty = 0;

    span_t spans[4];
    const int nspans = spans_of(spans);

    if (orient == GAL_ORIENT_UPRIGHT || orient == GAL_ORIENT_UPRIGHT_FULL)
    {
        uint16_t line[GAL_W];
        int y = 0;
        for (const span_t *s = spans; s < spans + nspans; s++)
        {
            for (int i = 0; i < s->rows; i++, y++)
            {
                if (s->logo)
                    decode(line, GAL_W, s, s->marks[i * BANDS]);
                else
                    memset(line, 0, sizeof(line));
                put_row(orient, fb, stride, y, line);
            }
        }
        return;
    }

    /* Tate, as gal_render's bands: eight columns of the upright picture are
     * eight canvas rows, the canvas x of upright row y is x0 + y (CW) or
     * x0 + 287 - y (CCW) */
    const int x0 = (GAL_CANVAS_W - GAL_H) / 2, y0 = (GAL_CANVAS_H - GAL_W) / 2;
    const int cw = (orient == GAL_ORIENT_TATE_CW);
    const int dx = cw ? 1 : -1, drow = cw ? -stride : stride;
    for (int k = 0; k < BANDS; k++)
    {
        const int c = cw ? BANDS - 1 - k : k; /* upright columns 8c .. 8c + 7 */
        uint16_t *dst = cw ? fb + (y0 + GAL_W - 1 - 8 * c) * stride + x0  /* ux 8c is the band's bottom row */
                           : fb + (y0 + 8 * c) * stride + x0 + GAL_H - 1; /* ux 8c is its top row */
        for (const span_t *s = spans; s < spans + nspans; s++)
        {
            for (int i = 0; i < s->rows; i++, dst += dx)
            {
                uint16_t px[8] = {0};
                if (s->logo)
                    decode(px, 8, s, s->marks[i * BANDS + c]);
                for (int j = 0; j < 8; j++)
                    dst[j * drow] = px[j];
            }
        }
    }
}
