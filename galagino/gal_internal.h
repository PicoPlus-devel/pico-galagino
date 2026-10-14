/*
 * Galagino core internals: the machine state shared by the games, the game
 * arena, and the band renderer's drawing helpers.
 *
 * Galagino keeps its state in globals (one machine runs at a time); this keeps
 * the same shape in a single struct, `gal`.
 */
#ifndef GAL_INTERNAL_H
#define GAL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gal_port.h"
#include "galagino.h"
#include "z80.h"

/* The upright picture every game draws. */
#define GAL_W 224
#define GAL_H 288

/* Galagino's RAM buffer, remapped per game; 1942 needs the extra bytes (its
 * 128 bytes of sprite RAM at 2400 are mapped as a whole 256-byte page). */
#define GAL_RAMSIZE (8192 + 1024 + 256)

/* The most AY-3-8910 a game has (Gyruss), each with 16 registers in
 * gal.soundregs. */
#define GAL_AY_MAX 5

/* Converted game data lives in a fixed arena in .bss, so the cost is visible at
 * link time and nothing fragments the heap. Sized for the largest game that is
 * kept in SRAM (Dig Dug); see gal_arena_peak(). */
#ifndef GAL_ARENA_SIZE
#define GAL_ARENA_SIZE (100 * 1024)
#endif

typedef struct
{
    uint8_t code, color, flags;
    int16_t x, y;
} gal_sprite_t;

#define GAL_MAX_SPRITES 128

/* Tables a large game keeps in the flash cache besides its ROM regions. */
#define GAL_MAX_DERIVED 4
#define GAL_MAX_PARTS (GAL_MAX_REGIONS + GAL_MAX_DERIVED)

/* A rectangle of the upright picture being drawn. Pixel (x, y) of the picture
 * is buf[(y - y0) * w + (x - x0)]; pixels are already in the canvas format. */
typedef struct
{
    uint16_t *buf;
    int x0, y0, w, h;
} gal_band_t;

/* A high score region from MAME's hiscore.dat: len bytes at addr in the main
 * CPU's address space; once the game has set up its table, the first byte
 * holds start_byte and the last end_byte. */
typedef struct
{
    uint16_t addr, len;
    uint8_t start_byte, end_byte;
} gal_hiscore_region_t;

typedef struct
{
    /* Converts the loaded ROM regions into the game's data (arena) and sets up
     * the memory map. Returns 0 when out of memory. */
    int (*init)(uint8_t *const *regions);
    /* Power-on reset. */
    void (*reset)(void);
    /* A quarter (q = 0..3) of a 60 Hz frame of the CPUs. The core renders the
     * sound after each quarter, so register changes within a frame are heard;
     * the frame's interrupts belong at the end of quarter 3. */
    void (*run_slice)(int q);
    /* Builds the sprite list for the following render_band calls. */
    void (*prepare_frame)(void);
    /* Clears the band to the background and draws the picture into it. */
    void (*render_band)(const gal_band_t *b);
    /* Renders n samples of the game's sound (n is about a quarter frame). */
    void (*sound)(int16_t *out, int n);
    /* The rows of the upright 224x288 picture the game uses: all of them on
     * the Namco boards; 16..271 on the Galaxian-class boards with a 256-line
     * raster, which then lose only one row in 16 when upright. */
    int16_t view_top, view_h;
    /* The second button (B) is the game's own (GAL_BTN_EXTRA: 1942's loop),
     * not a second fire button. */
    uint8_t extra_button;
    /* The game draws its picture upside down (MAME ROT270 where its board's
     * other games are ROT90: Van-Van Car): render_band is asked for the
     * rectangle turned by 180 degrees, which the core turns back. */
    uint8_t flip180;
    /* The game's data does not fit in SRAM: it runs with the PSRAM block, or
     * from the flash cache on a board without. The cache is filled a region
     * at a time through the SRAM arena, so no region may be larger than the
     * arena less 4 KB. */
    uint8_t large;
    /* Tables built from the ROMs that the flash cache keeps as well (1942's
     * playfield tiles and sprites): their number, the region each is built
     * from, their sizes (multiples of 4 KB), and building bytes ofs..ofs+len
     * (len a multiple of 4 KB) of table i from that region. init() finds them
     * at regions[GAL_MAX_REGIONS + i] when the game starts from flash, NULL
     * otherwise. */
    uint8_t nderived;
    uint8_t derived_src[GAL_MAX_DERIVED];
    uint32_t derived_size[GAL_MAX_DERIVED];
    void (*derive)(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len);
    /* Raised when the layout of the game's regions or tables changes without
     * their sizes changing: a slot of the flash cache written by a build with
     * another value is not used. */
    uint8_t conv_version;
    /* High scores (gal_hiscore.c): the regions, and their access when the main
     * CPU's page table does not reach them. All may be NULL. hs_restored is
     * called after saved scores were put in, for games that only redraw the
     * top score at a given point. */
    const gal_hiscore_region_t *hiscore;
    uint8_t nhiscore;
    uint8_t (*hs_read)(uint16_t addr);
    void (*hs_write)(uint16_t addr, uint8_t value);
    void (*hs_restored)(void);
} gal_game_ops_t;

typedef struct
{
    int game; /* gal_game_id_t, or -1 */
    const gal_game_ops_t *ops;
    gal_fmt_t fmt;
    int samplerate;

    uint16_t buttons; /* GAL_BTN_* of the current frame */
    uint8_t game_started;

    z80_t cpu[3];
    uint8_t ram[GAL_RAMSIZE];
    uint8_t soundregs[16 * GAL_AY_MAX];

    gal_sprite_t sprite[GAL_MAX_SPRITES];
    int active_sprites;
} gal_t;

extern gal_t gal;

/* A page of zeros to read, and one whose writes are ignored, for the page
 * tables of every game */
extern uint8_t gal_zeros[256], gal_ignored[256];

/* --- Profiling ---------------------------------------------------------------
 * In GALAGINO_FRAME_STATS builds: CPU cycles summed per part of the frame into
 * gal_prof[]: 0 run_slice, 1 the sound, 2..7 as the game counts them (its
 * CPUs). main.cpp provides gal_cycles() and prints the sums. */
#if GALAGINO_FRAME_STATS
#define GAL_NPROF 8
extern uint32_t gal_prof[GAL_NPROF];
uint32_t gal_cycles(void);
#define GAL_PROF_START(t) const uint32_t t = gal_cycles()
#define GAL_PROF_ADD(i, t) (gal_prof[i] += gal_cycles() - (t))
#else
#define GAL_PROF_START(t) ((void)0)
#define GAL_PROF_ADD(i, t) ((void)0)
#endif

/* --- Arenas ---------------------------------------------------------------
 * Two-ended: the game's data from the bottom, the raw ROM regions that are
 * only needed while converting from the top. A game is given the SRAM arena,
 * or the PSRAM block the board provides (gal_set_psram) when it is large;
 * gal_fast_alloc takes from the SRAM arena either way, for data such a game
 * reads all the time. */
void *gal_alloc(uint32_t size);      /* NULL when full */
void *gal_fast_alloc(uint32_t size); /* NULL when full */
void *gal_temp_alloc(uint32_t size); /* NULL when full */
void gal_temp_release(void);
void gal_arena_reset(void);

/* --- The flash cache (gal_flash.c) ----------------------------------------- */

/* The game's parts in the cache (regions at their index, derived tables at
 * GAL_MAX_REGIONS + i), or 0 when it does not hold the game. */
int gal_flash_parts(int game, const uint8_t **parts);

/* --- High scores (gal_hiscore.c) ------------------------------------------- */

/* Called by gal_start and gal_stop. */
void gal_hiscore_start(void);
void gal_hiscore_stop(void);

/* Called after every frame. */
void gal_hiscore_frame(void);

/* The ops of a game, NULL when it is not part of this build. */
const gal_game_ops_t *gal_ops(int game);

/* --- Colours --------------------------------------------------------------- */

/* Galagino's colour PROM decodes produce RGB565; this is the canvas format.
 * Black stays 0, and only black: sprites treat colour 0 as transparent, as in
 * Galagino, so a dark colour that would round to 0 becomes 1. */
uint16_t gal_color(uint16_t rgb565);

/* Galagino's bbgggrrr colour PROM decode (Galaga, Pac-Man, Frogger, Dig Dug). */
uint16_t gal_prom_bgr233(uint8_t c);

/* --- Graphics converters (gal_gfx.c), ports of upstream romconv scripts ------- */

/* tileconv.py parse_charmap, 4 KB of Pac-Man/Galaga characters: 256 tiles of
 * 8 rows, two bits per pixel, leftmost pixel in the low bits. */
void gal_conv_chars_namco(const uint8_t *rom, int count, uint16_t *out);

/* spriteconv.py parse_sprite, 64 sprites per 4 KB ROM: 16 rows of 32 bits, two
 * bits per pixel, leftmost pixel in the low bits. `out` gets two variants,
 * normal and flipped in x (unless out_xflip is NULL), each count*16 words; a
 * flip in y reads the rows backwards. */
void gal_conv_sprites_namco(const uint8_t *rom, int count, int pacman_fmt, uint32_t *out_normal, uint32_t *out_xflip);

/* Frogger: tileconv.py parse_charmap_2 (256 characters, a bitplane per ROM)
 * and spriteconv.py parse_sprite_frogger (64 sprites from the same ROMs). */
void gal_conv_chars_2(const uint8_t *rom0, const uint8_t *rom1, uint16_t *out);
void gal_conv_sprites_frogger(const uint8_t *rom0, const uint8_t *rom1, uint32_t *out_normal, uint32_t *out_xflip);

/* One 16x16 sprite flipped in x. */
void gal_sprite_xflip(const uint32_t *in, uint32_t *out);

/* audioconv.py: the Namco WSG waveform PROMs, 8 waves of 32 steps per 256
 * bytes, as signed values -7..8. */
void gal_conv_waves(const uint8_t *prom, int count, int8_t *out);

/* MAME's gfx_layout: the element's pixel (x, y) is made of the bits at
 * n * charincrement + yoffset[y] + xoffset[x] + planeoffset[p], counted from
 * the top bit of the first byte; planeoffset[0] gives the top bit of the pen. */
typedef struct
{
    uint8_t w, h, planes;
    uint32_t planeoffset[4];
    uint16_t xoffset[32], yoffset[32];
    uint32_t charincrement;
} gal_layout_t;

/* The game's MAME orientation, which the converters below turn upright. */
#define GAL_ROT90 0
#define GAL_ROT270 1

int gal_layout_pixel(const uint8_t *rom, const gal_layout_t *l, int n, int x, int y);

/* count 8x8 elements of two bits per pixel, upright, in gal_draw_tile's
 * format (8 rows of 16 bits, leftmost pixel in the low bits) */
void gal_conv_tiles8(const uint8_t *rom, const gal_layout_t *l, int count, int rot, uint16_t *out);

/* count 16x16 elements of two bits per pixel, upright, in gal_draw_sprite's
 * format (16 rows of 32 bits) */
void gal_conv_sprites16(const uint8_t *rom, const gal_layout_t *l, int count, int rot, uint32_t *out);

/* count s x s elements from element `first` (s = l->w: 8, 16 or 32), up to
 * four bits per pixel, upright: per row s / 8 words of eight pixels, the
 * leftmost in the low nibble (gal_draw_4bpp) */
void gal_conv_4bpp(const uint8_t *rom, const gal_layout_t *l, int first, int count, int rot, uint32_t *out);

/* --- Sound (gal_sound.c) ------------------------------------------------- */

/* Namco WSG (Pac-Man, Galaga, Dig Dug), as Galagino renders it. */
void gal_wsg_reset(void);
void gal_wsg_render(const int8_t *waves, uint8_t wave_mask, int16_t *out, int n);
/* The 8048 core (i8048.c) calls game_dkong.c's port functions, which pass
 * the calls on to these when set (Gyruss's 8039); i8048.h reads the ROM
 * through dkong_rom_cpu2. */
struct i8048_state_S;
typedef struct
{
    void (*port_write)(struct i8048_state_S *s, unsigned char port, unsigned char value);
    unsigned char (*port_read)(struct i8048_state_S *s, unsigned char port);
    unsigned char (*xdm_read)(struct i8048_state_S *s, unsigned char addr);
} gal_i8048_io_t;
extern const gal_i8048_io_t *gal_i8048_io;
extern const unsigned char *dkong_rom_cpu2;

/* Namco's custom I/O chips 56XX and 58XX (gal_namcoio.c): in(port) gives
 * the chip's four inputs, active low; out, when set, its output pins (the
 * DIP switch multiplexer). */
typedef struct
{
    uint8_t ram[16];
    uint8_t lastcoins, lastbuttons;
    int credits;
    uint8_t coins[2], coins_per_cred[2], creds_per_coin[2];
    uint8_t (*in)(int port);
    void (*out)(int value);
} gal_namcoio_t;

void gal_namcoio_reset(gal_namcoio_t *c);
void gal_namcoio_run_56xx(gal_namcoio_t *c);
void gal_namcoio_run_58xx(gal_namcoio_t *c);

/* Namco 15XX (Mappy): eight voices, their registers at regs[0..63]. */
void gal_wsg15_reset(void);
void gal_wsg15_render(const uint8_t *regs, const int8_t *waves, int enabled, int16_t *out, int n);

/* AY-3-8910 tone and noise of `chips` chips (registers at soundregs[16 * k]),
 * clocked at `clock` Hz, Galagino's per-channel level ay_vol. */
void gal_ay_reset(void);
void gal_ay_render(int chips, int clock, int ay_vol, int16_t *out, int n);

/* SN76489 / SN76496 */
typedef struct
{
    uint16_t reg[8];
    uint8_t last;
    uint8_t tap1, tap2, negate;
    int32_t period[4], count[4];
    uint8_t out[4];
    int16_t vol[4], vol_table[16];
    uint32_t rng, feedback;
    uint32_t step, phase; /* chip steps (clock / 16) per output sample, 16.16 */
    int32_t dc_x, dc_y;   /* the DC blocker of a mix, in its first chip */
} gal_sn_t;

void gal_sn_init(gal_sn_t *c, int sn76489, uint32_t clock);
void gal_sn_write(gal_sn_t *c, uint8_t data);
/* Renders n samples of nchips chips, their sum times gain_q8 / 256. */
void gal_sn_render(gal_sn_t *chips, int nchips, int gain_q8, int16_t *out, int n);

/* A Z80 CTC as far as the games use it (Pinball Action, Star Force): timer
 * and counter mode, interrupts through IM 2, channel 0 first. */
typedef struct
{
    uint8_t control[4];
    uint16_t constant[4];
    int32_t count[4];   /* timer: CPU cycles left; counter: triggers left */
    uint8_t want_tc[4]; /* the next write is the time constant */
    uint8_t pending;    /* interrupt requests, a bit per channel */
    uint8_t vector;
    uint8_t chain;      /* bit n: channel n's zero count triggers channel n + 1 */
} gal_ctc_t;

void gal_ctc_write(gal_ctc_t *c, int ch, uint8_t value);
uint8_t gal_ctc_read(const gal_ctc_t *c, int ch);
/* timer mode: `cycles` of the CPU's clock */
void gal_ctc_clock(gal_ctc_t *c, int cycles);
/* counter mode: a pulse on the channel's trigger */
void gal_ctc_trigger(gal_ctc_t *c, int ch);
/* a running timer's period in CPU cycles, 0 if the channel is not one */
int32_t gal_ctc_period(const gal_ctc_t *c, int ch);
/* the vector of the highest request, taken off the list; -1 for none */
int gal_ctc_take(gal_ctc_t *c);

/* A recorded sample played once at its own rate, added to the output. */
typedef struct
{
    const int8_t *data; /* NULL when silent */
    uint32_t len, pos, step; /* 16.16 */
    uint8_t shift;
} gal_voice_t;

void gal_voice_start(gal_voice_t *v, const int8_t *data, uint32_t len, int rate, int shift);
void gal_voice_mix(gal_voice_t *v, int16_t *out, int n);

/* --- Games ----------------------------------------------------------------- */

/* Upstream's tileaddr.h: the canvas tile (row, column) of the upright 36x28
 * tile picture to its offset in the Namco video RAM. */
extern const unsigned short tileaddr[36][28];

extern const gal_game_ops_t gal_pacman_ops;
extern const gal_game_ops_t gal_galaga_ops;
extern const gal_game_ops_t gal_frogger_ops;
extern const gal_game_ops_t gal_dkong_ops;
extern const gal_game_ops_t gal_digdug_ops;
extern const gal_game_ops_t gal_1942_ops;
extern const gal_game_ops_t gal_mspacman_ops;
extern const gal_game_ops_t gal_crush_ops;
extern const gal_game_ops_t gal_eyes_ops;
extern const gal_game_ops_t gal_mrtnt_ops;
extern const gal_game_ops_t gal_lizwiz_ops;
extern const gal_game_ops_t gal_theglob_ops;
extern const gal_game_ops_t gal_alibaba_ops;
extern const gal_game_ops_t gal_pengo_ops;
extern const gal_game_ops_t gal_galaxian_ops;
extern const gal_game_ops_t gal_mooncrst_ops;
extern const gal_game_ops_t gal_scramble_ops;
extern const gal_game_ops_t gal_scobra_ops;
extern const gal_game_ops_t gal_anteater_ops;
extern const gal_game_ops_t gal_turtles_ops;
extern const gal_game_ops_t gal_amidar_ops;
extern const gal_game_ops_t gal_vanvan_ops;
extern const gal_game_ops_t gal_ladybug_ops;
extern const gal_game_ops_t gal_mrdo_ops;
extern const gal_game_ops_t gal_phoenix_ops;
extern const gal_game_ops_t gal_invaders_ops;
extern const gal_game_ops_t gal_bombjack_ops;
extern const gal_game_ops_t gal_bagman_ops;
extern const gal_game_ops_t gal_pbaction_ops;
extern const gal_game_ops_t gal_starforc_ops;
extern const gal_game_ops_t gal_dkongjr_ops;
extern const gal_game_ops_t gal_btime_ops;
extern const gal_game_ops_t gal_bnj_ops;
extern const gal_game_ops_t gal_vanguard_ops;
extern const gal_game_ops_t gal_fantasy_ops;
extern const gal_game_ops_t gal_nibbler_ops;
extern const gal_game_ops_t gal_dkong3_ops;
extern const gal_game_ops_t gal_timeplt_ops;
extern const gal_game_ops_t gal_pooyan_ops;
extern const gal_game_ops_t gal_tutankhm_ops;
extern const gal_game_ops_t gal_rocnrope_ops;
extern const gal_game_ops_t gal_circusc_ops;
extern const gal_game_ops_t gal_mappy_ops;
extern const gal_game_ops_t gal_todruaga_ops;
extern const gal_game_ops_t gal_gaplus_ops;
extern const gal_game_ops_t gal_gyruss_ops;
extern const gal_game_ops_t gal_xevious_ops;

#ifdef GAL_CONV_CHECK
/* hosttest/conv_check: the running game's converted data under the names of
 * the arrays upstream's romconv scripts write. */
typedef struct
{
    const char *name;
    const void *data;
    uint32_t bytes;
} gal_table_t;
#define GAL_MAX_TABLES 16
int gal_tables(gal_table_t *t);
int gal_tables_pacman(gal_table_t *t);
int gal_tables_galaga(gal_table_t *t);
int gal_tables_dkong(gal_table_t *t);
int gal_tables_frogger(gal_table_t *t);
int gal_tables_digdug(gal_table_t *t);
int gal_tables_1942(gal_table_t *t);
#endif

/* --- Drawing helpers -------------------------------------------------------- */

/* An 8x8 tile, two bits per pixel; pen 0 is transparent. */
static inline void gal_draw_tile(const gal_band_t *b, int px, int py, const uint16_t *tile, const uint16_t *colors)
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
        if (!pix)
            continue;
        for (int c = 0; c < cols; c++, pix >>= 2)
            if (pix & 3)
                row[c] = colors[pix & 3];
    }
}

/* The same, every pen drawn (pen 0 in colors[0]), for hardware whose tile
 * layer is opaque (the Pac-Man boards). */
static inline void gal_draw_tile_opaque(const gal_band_t *b, int px, int py, const uint16_t *tile,
                                        const uint16_t *colors)
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
            row[c] = colors[pix & 3];
    }
}

/* The 16 two-bit pixels of a sprite row in the opposite order: the row
 * flipped in x. */
static inline uint32_t gal_rev2(uint32_t v)
{
    v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
    v = ((v >> 4) & 0x0f0f0f0fu) | ((v & 0x0f0f0f0fu) << 4);
    v = ((v >> 8) & 0x00ff00ffu) | ((v & 0x00ff00ffu) << 8);
    return (v >> 16) | (v << 16);
}

/* The eight four-bit pixels of a word in the opposite order. */
static inline uint32_t gal_rev4(uint32_t v)
{
    v = __builtin_bswap32(v);
    return ((v >> 4) & 0x0f0f0f0fu) | ((v & 0x0f0f0f0fu) << 4);
}

/* A 16x16 sprite, two bits per pixel (rows of 32 bits, leftmost pixel in the
 * low bits). Colour 0 is transparent (Pac-Man, Galaga, Dig Dug), or pen 0
 * (pen0 set: Frogger, Donkey Kong), as in Galagino. flipy reads the rows
 * bottom up; flipx reverses each row (gal_draw_sprite_ex keeps a flipped copy
 * instead). */
static inline void gal_draw_sprite_fx(const gal_band_t *b, int sx, int sy, const uint32_t *rows, int flipx,
                                      int flipy, const uint16_t *colors, int pen0)
{
    int ys = sy, ye = sy + 16, xs = sx, xe = sx + 16;
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
    const int skip = 2 * (xs - sx), cols = xe - xs;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        int r = y - sy;
        uint32_t pix = rows[flipy ? 15 - r : r];
        if (flipx)
            pix = gal_rev2(pix);
        pix >>= skip;
        if (!pix)
            continue;
        if (pen0)
        {
            for (int c = 0; c < cols; c++, pix >>= 2)
                if (pix & 3)
                    row[c] = colors[pix & 3];
        }
        else
        {
            for (int c = 0; c < cols; c++, pix >>= 2)
            {
                uint16_t col = colors[pix & 3];
                if (col)
                    row[c] = col;
            }
        }
    }
}

static inline void gal_draw_sprite_ex(const gal_band_t *b, int sx, int sy, const uint32_t *rows, int flipy,
                                      const uint16_t *colors, int pen0)
{
    gal_draw_sprite_fx(b, sx, sy, rows, 0, flipy, colors, pen0);
}

static inline void gal_draw_sprite(const gal_band_t *b, int sx, int sy, const uint32_t *rows, int flipy,
                                   const uint16_t *colors)
{
    gal_draw_sprite_ex(b, sx, sy, rows, flipy, colors, 0);
}

/* An s x s element of gal_conv_4bpp (s = 8, 16 or 32), flipped as asked;
 * pen 0 is transparent unless opaque is set. A row the band does not clip is
 * drawn a word (eight pixels) at a time, a word of transparent pixels
 * skipped: most of what the layered games draw (Star Force) is. */
static inline void gal_draw_4bpp(const gal_band_t *b, int px, int py, int s, const uint32_t *rows, int flipx,
                                 int flipy, const uint16_t *colors, int opaque)
{
    int ys = py, ye = py + s, xs = px, xe = px + s;
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
    const int wpr = s >> 3, whole = xs == px && xe == px + s;
    uint16_t *row = b->buf + (ys - b->y0) * b->w - b->x0;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        const int r = flipy ? py + s - 1 - y : y - py;
        const uint32_t *w = rows + r * wpr;
        if (whole)
        {
            uint16_t *d = row + px;
            for (int i = 0; i < wpr; i++, d += 8)
            {
                uint32_t pix = w[flipx ? wpr - 1 - i : i];
                if (!pix && !opaque)
                    continue;
                if (flipx)
                    pix = gal_rev4(pix);
#pragma GCC unroll 8
                for (int c = 0; c < 8; c++, pix >>= 4)
                {
                    const unsigned pen = pix & 15;
                    if (pen || opaque)
                        d[c] = colors[pen];
                }
            }
            continue;
        }
        for (int x = xs; x < xe; x++)
        {
            const int c = flipx ? px + s - 1 - x : x - px;
            const unsigned pen = (w[c >> 3] >> ((c & 7) << 2)) & 15;
            if (pen || opaque)
                row[x] = colors[pen];
        }
    }
}

/* The same, the pens whose bit is set in `transparent` left out (MAME's
 * transmask: Pooyan's sprites, transparent where their lookup gives colour 0).
 * A row is read a word (eight pixels) at a time, the part of each word the
 * band clips left out; with pen 0 transparent, a word of pen 0 is skipped. */
static inline void gal_draw_4bpp_mask(const gal_band_t *b, int px, int py, int s, const uint32_t *rows, int flipx,
                                      int flipy, const uint16_t *colors, uint16_t transparent)
{
    int ys = py, ye = py + s, xs = px, xe = px + s;
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
    const int wpr = s >> 3, c0 = xs - px, c1 = xe - px, skip0 = transparent & 1;
    uint16_t *row = b->buf + (ys - b->y0) * b->w - b->x0 + px;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        const int r = flipy ? py + s - 1 - y : y - py;
        const uint32_t *w = rows + r * wpr;
        /* columns c0 .. c1 - 1 of the element; flipped in x, word i of the
         * row is the last but i reversed */
        for (int i = c0 >> 3; i <= (c1 - 1) >> 3; i++)
        {
            uint32_t pix = w[flipx ? wpr - 1 - i : i];
            if (!pix && skip0)
                continue;
            if (flipx)
                pix = gal_rev4(pix);
            const int k0 = c0 > 8 * i ? c0 - 8 * i : 0, k1 = c1 < 8 * i + 8 ? c1 - 8 * i : 8;
            uint16_t *d = row + 8 * i;
            if (k0 == 0 && k1 == 8)
            {
#pragma GCC unroll 8
                for (int k = 0; k < 8; k++, pix >>= 4)
                {
                    const unsigned pen = pix & 15;
                    if (!((transparent >> pen) & 1))
                        d[k] = colors[pen];
                }
                continue;
            }
            pix >>= 4 * k0;
            for (int k = k0; k < k1; k++, pix >>= 4)
            {
                const unsigned pen = pix & 15;
                if (!((transparent >> pen) & 1))
                    d[k] = colors[pen];
            }
        }
    }
}

static inline void gal_fill_band(const gal_band_t *b, uint16_t color)
{
    uint16_t *p = b->buf;
    for (int i = b->w * b->h; i > 0; i--)
        *p++ = color;
}

#endif
